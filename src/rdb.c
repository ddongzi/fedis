/**
 * RDB 快照。
 *
 * RDB 文件格式
 * REDIS 标识
 * 0001 版本
 * ---database 0 ---
 * SELECTDB 标识
 * 1 数据库编号1字节
 * type,key,val
 * ----------
 * EOF
 * CHECKSUM
 */


#include <stdio.h>
#include "redis.h"
#include "rdb.h"

#include <fcntl.h>
#include <stdint.h>
#include "log.h"
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include "crypto.h"
#include "util.h"
/**
 * @brief 对象类型、RDB操作符
 * 
 * @param [in] fp 
 * @param [in] type 
 */
void _rdbSaveType(FILE *fp, unsigned char type)
{
    fwrite(&type, 1, 1, fp);
}

/**
 * @brief Save the length of the given unsigned long value to the RDB file.
 *
 * This function writes the length of the given unsigned long value to the specified file pointer.
 * The length is encoded in a variable-length format, using 1, 2, or 5 bytes depending on the value.
 *
 * @param [in] fp The file pointer to write the length to.
 * @param [in] len The unsigned long value to save.
 *
 * @return int
 * 
 * @note len最长为4字节
 *  len < 64 : 占用1字节，6位存储， 标记00xxxxxx
 *  len < 1<<14 : 占用2字节，14位存储，标记01xxxxxx xxxxxxxx
 *  len : 占用5字节，#1标记，剩余存储。    标记11111110 xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
 *  非法：标记11000000
 */
int _rdbSaveLen(FILE* fp, uint32_t len)
{
    unsigned char buf[5];
    if (len < (1<<6)) { 
        buf[0] = len & 0x3F; // 获取低6位 
        return fwrite(buf, 1, 1, fp);
    } else if (len < (1<<14)) { 
        buf[0] = 0x80 |((len >> 8) & 0x3F);
        buf[1] = len & 0xFF;
        return fwrite(buf, 1, 2, fp);
    } else { 
        buf[0] = 0XFE;
        
        memcpy(buf + 1, &len, 4);
        return fwrite(buf, 1, 5, fp);
    }
}


static void _rdbSaveStringObject(FILE *fp, robj* obj)
{
    switch (obj->encoding)
    {
    case REDIS_ENCODING_INT:
        int val = (int)(obj->ptr);
        if (val <= (1<<7)) {
            // 8bit
            _rdbSaveType(fp, RDB_ENC_INT8);
            fwrite(&val, 1, 1, fp);
        } else if (val <= (1<< 15)) {
            // 16bit
            _rdbSaveType(fp, RDB_ENC_INT16);
            fwrite(&val, 1, 2, fp);
        } else {
            // 32bit
            _rdbSaveType(fp, RDB_ENC_INT32);
            fwrite(&val, 1, 4, fp);
        }
        break;
    case REDIS_ENCODING_EMBSTR:
    case REDIS_ENCODING_RAW:
        int len = sdslen(obj->ptr);
        _rdbSaveLen(fp, len);
        fwrite(((sds*)(obj->ptr))->buf, 1, len, fp);
        break;
    default:
        break;
    }

}

void _rdbSaveValue(FILE* fp, robj *obj)
{
    switch (obj->type)
    {
    case REDIS_STRING:
        _rdbSaveStringObject(fp, obj);
        break;
    
    default:
    
        break;
    }
}
void _rdbSaveKey(FILE *fp, sds* key)
{
    _rdbSaveLen(fp, sdslen(key));
    fwrite(key->buf, 1, sdslen(key), fp);
}


// 全量保存
void rdbSave()
{
    log_debug("======RDB Save(child:%u)======", getpid());
    int nwritten = 0;
    int nread = 0;
    FILE* fp = fopen(server->rdbfile, "w+");
    if (!fp) {
        perror("rdbSave can't open file"); 
        return;
    }
    if ((nwritten = fwrite("REDIS0001", 1, 9, fp))< 9) {
        perror("rdbSave can't write : REDIS0001");
        exit(1);
        return;
    }

    for (int i = 0; i < server->dbnum; i++) {
        redisDb* db = server->db + i;
        if (dictIsEmpty(db->kv)) continue;

        _rdbSaveType(fp, RDB_SELECTDB); // 1字节

        fwrite(&db->id, 1, 1, fp); //

        dictIterator* di = dictGetIterator(db->kv);
        dictEntry* entry;
        while ((entry = dictIterNext(di))!= NULL) {
            sds *key = entry->key;
            robj *val = entry->v.val;

            if (dictContains(db->expires, key))
            {
                uint8_t flag = 0xFD;
                fwrite(&flag, 1, 1, fp);
                long time = (long)dictFetchValue(db->expires, key);
                fwrite(&time, 8, 1, fp);
            }

            _rdbSaveType(fp, val->type);
            _rdbSaveKey(fp, key);
            _rdbSaveValue(fp, val);
        }
        dictReleaseIterator(di);
    }
    // 3. 写入EOF
    _rdbSaveType(fp, RDB_EOF); // 1字节

    fflush(fp); // 确保写入文件
    fseek(fp, 0, SEEK_SET); // 移动到开头

    // 4. 校验和 
    char buf[RDB_MAX_SIZE] = {0};
    nread = fread(buf,1, RDB_MAX_SIZE, fp);
    assert(nread > 0);
    char hash[RDB_CHECKSUM_LEN];
    compute_sha256(buf, nread, hash); 
    
    int ret = verify_sha256(buf, nread, hash);
    if (ret == 1) log_debug("Verify success!");
    else log_error("Verify failed!!");

    fseek(fp, 0, SEEK_END); // 移动到末尾写入
    fwrite(hash, 1, RDB_CHECKSUM_LEN, fp);

    fflush(fp);
    fclose(fp);
    log_debug("Save the RDB file success");
}

/**
 * 开启一个子进程，做rdbsave
 * @warning 需要控制资源开销，调整bgsave。
 * @details 每次fork都相当于内存快照。
 */
void bgsave()
{
    pid_t pid = fork();
    if (pid == 0) {
        rdbSave();
        exit(0);
    } else if (pid < 0) {
        log_debug("Error: fork failed");
    }
    // 父亲进程continue
    server->rdbChildPid = pid;
    server->isBgSaving = 1;
}

void bgSaveIfNeeded()
{
    // 检查是否在BGSAVE
    if (server->isBgSaving) {
        log_debug("BGSAVE is running, no need....");
        return;
    }

    /*  bgsave：  满足一个就可以bgsave
     *  服务器在距离上次save秒内，对数据库进行了至少n此修改。就
     */
    for(int i = 0; i < server->saveCondSize; i++) {
        time_t interval = time(NULL) - server->lastSave;
        if (interval >= server->saveParams[i].seconds && 
            server->dirty >= server->saveParams[i].changes
        ) {
            log_debug("CHECK BGSAVE OK, %d, %d", interval, server->dirty);
            bgsave();
            break;
        }
    }
}

int eof(FILE* fp)
{
    unsigned char c;
    fread(&c, 1, 1, fp);
    return c == RDB_EOF; // 1字节
}

uint32_t _rdbLoadLen(FILE* fp)
{
    unsigned char buf[5];
    memset(buf, 0, sizeof(buf));
    if (fread(buf, 1, 1, fp) == 0) return 0;  // 读取失败

    // 0xC0:11000000 提取高两位
    if ((buf[0] & 0xC0) == 0x00) {  // 1 字节编码 (00xxxxxx)
        return buf[0] & 0x3F;
    } else if ((buf[0] & 0xC0) == 0x80) {  // 2 字节编码 (10xxxxxx xxxxxxxx)
        if (fread(buf + 1, 1, 1, fp) == 0) return 0;
        return ((buf[0] & 0x3F) << 8) | buf[1];
    } else if (buf[0] == 0xFE) {  // 5 字节编码 (11111110 + 4 字节数据)
        if (fread(buf + 1, 4, 1, fp) == 0) return 0;
        uint32_t len;
        memcpy(&len, buf + 1, 4);
        return len;
    } else {
        return 0;  // 错误
    }

}

robj* _rdbLoadStringObject(FILE* fp)
{
    unsigned char c;
    fread(&c, 1, 1, fp);
    if (c == RDB_ENC_INT8) {
        unsigned char val;
        char buf[12];
        fread(&val, 1, 1, fp);
        snprintf(buf, 11, "%d", val);
        // log_debug("Load STRING(int) %d", val);
        return robjCreateStringObject(buf);
    } else if (c == RDB_ENC_INT16) {
        /* code */
        int16_t val;
        char buf[12];
        fread(&val, 1, 2, fp);
        snprintf(buf, 11, "%d", val);
        // log_debug("Load STRING(int) %d", val);

        return robjCreateStringObject(buf);
    } else if (c == RDB_ENC_INT32) {
        /* code */
        int val;
        char buf[12];
        fread(&val, 1, 4, fp);
        snprintf(buf, 11, "%d", val);
        // log_debug("Load STRING(int) %d", val);

        return robjCreateStringObject(buf);
    }else {
        // RAW, EMBSTR字符串
        fseek(fp, -1, SEEK_CUR);    // 回退一个字节，
        uint32_t len = _rdbLoadLen(fp);
        char * buf = malloc(len + 1);
        fread(buf, 1, len, fp);
        buf[len] = '\0';
        // log_debug("Load STRING() %s", buf);

        robj* obj = robjCreateStringObject(buf);
        free(buf);
        return obj;
    } 
}

unsigned char _rdbLoadType(FILE* fp)
{
    unsigned char c;
    fread(&c, 1, 1, fp);
    return c; // 1字节
    
}
sds*  _rdbLoadKey(FILE *fp)
{
    uint32_t len = _rdbLoadLen(fp);
    sds* key = sdsempty();
    char buf[1024];
    size_t nread= fread(buf, 1, len, fp);
    sdscatlen(key, buf, nread);
    return key;
}
robj* _rdbLoadObject(FILE* fp, unsigned char type)
{
    robj* obj;
    switch (type)
    {
    case RDB_TYPE_STRING:
        obj = _rdbLoadStringObject(fp);
        break;
    
    default:
        break;
    }
}

/**
 * @brief 将本地.rdb加载到数据库
 * 
 */
void rdbLoad()
{

    FILE *fp = fopen(server->rdbfile, "w+");
    if (fp == NULL)
    {
        log_error("rdb load failed. %s, %s", server->rdbfile, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    // 1. read magic
    char buf[9];
    fread(buf, 1, 9, fp);
    buf[9] = '\0';
    if (strcmp(buf, "REDIS0001") != 0) return;

    // 2. read the dbs
    uint8_t dbid;
    long expire = 0;
    while (1) {
        unsigned char type = _rdbLoadType(fp); // 
        
        if (type == RDB_EOF) break;

        if (type == RDB_SELECTDB) {

            // 读取数据库num
            fread(&dbid, 1, 1, fp);
            if (dbid < 0 || dbid > server->dbnum) {
                log_debug("Error loading dbid %d", dbid);;
                exit(1);
            }
            continue;
        }
        if (type == RDB_EXPIRETIME)
        {
            fread(&expire, 8, 1, fp);
            continue;
        }
        // 正常数据
        sds* key = _rdbLoadKey(fp);
        robj* val = _rdbLoadObject(fp, type);
        dbAdd(server->db + dbid, key, val);
        if (expire > 0)
        {
            dbSetExpire(server->db + dbid, key, expire);
            expire = 0;
        }
    }

    // 校验
    size_t nread = 0;
    size_t datalen = ftell(fp); // 当前长度
    // 读取校验码
    unsigned char hash[RDB_CHECKSUM_LEN] = {0};
    nread = fread(hash, 1, RDB_CHECKSUM_LEN, fp);
    assert(RDB_CHECKSUM_LEN == nread);

    fseek(fp, 0, SEEK_SET);
    char data[RDB_MAX_SIZE];
    nread = fread(data, 1, datalen, fp);
    assert(nread == datalen);
    
    int ret = verify_sha256(data, datalen, hash);
    assert(ret == 1);
}

/**
 * @brief 将主的RDB文件复制为自己的RDB
 * 
 */
void receiveRDBfile(char* buf, int n)
{
    // TODO 不应该直接覆盖，应该通过一个临时完整文件 去覆盖
    log_debug("Receive RDB to: %s", server->rdbfile);
    FILE* fp = fopen(server->rdbfile, "w+");
    if (fp == NULL)
    {
        log_error("Open rdb failed. %s, %s", server->rdbfile, strerror(errno));
        exit(EXIT_FAILURE);
    }
    size_t  nwrite =  fwrite(buf, 1, n, fp);
    if (nwrite != n)
    {
        log_error("Save buf to rdb uncomplete!");
    }
    log_debug("Save the RDB file success");
}
