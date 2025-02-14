#include <stdio.h>
#include "redis.h"
#include "rdb.h"

/**
 * @brief 对象类型、RDB操作符
 * 
 * @param [in] fp 
 * @param [in] type 
 */
void _rdbSaveType(FILE *fp, int type)
{
    fwrite(&type, sizeof(int), 1, fp);
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
 * @return void
 * 
 * @note 
 *  len < 64 : 占用1字节，6位存储
 *  len < 1<<14 : 占用2字节，14位存储
 *  len : 占用5字节，#1标记，剩余存储。    
 */
void _rdbSaveLen(FILE* fp, unsigned long len)
{
    unsigned char buf[5];
    if (len < (1<<6)) { 
        buf[0] = len & 0xFF; 
        fwrite(buf, 1, 1, fp);
    } else if (len < (1<<14)) { 
        buf[0] = (len >> 8) & 0xFF;
        buf[1] = len & 0xFF;
        fwrite(buf, 1, 2, fp);
    } else { 
        buf[0] = 0XFE;
        memcpy(buf + 1, &len, 4);
        fwrite(buf, 1, 5, fp);
    }
}

void _rdbSaveStringObject(FILE *fp, robj* obj)
{
    int len = sdslen(obj->ptr);
    _rdbSaveLen(fp, len);
    fwrite(((sds*)(obj->ptr))->buf, 1, len, fp);
}

void _rdbSaveObject(FILE* fp, robj *obj)
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

void rdbSave()
{
    FILE* fp = fopen(server->rdbFileName, "w");
    if (!fp) {
        perror("rdbSave can't open file"); 
        return;
    }
    fwrite("REDIS0001", 1, 9, fp);

    for (int i = 0; i < server->dbnum; i++) {
        redisDb* db = server->db + i;
        if (dictIsEmpty(db->dict)) continue;

        _rdbSaveType(fp, RDB_SELECTDB);
        _rdbSaveLen(fp, i);

        dictIterator* di = dictGetIterator(db->dict);
        dictEntry* entry;
        while ((entry = dictIterNext(di))!= NULL) {
            robj *key = entry->key;
            robj *val = entry->v.val;

            _rdbSaveType(fp, key->type);
            _rdbSaveStringObject(fp, key);
            _rdbSaveObject(fp, val);
        }
        dictReleaseIterator(di);
    }
    // 3. 写入EOF
    _rdbSaveType(fp, RDB_EOF);

    // 4. 校验和 TODO
    uint64_t crc = 0;
    fwrite(&crc, sizeof(uint64_t), 1, fp);

    fclose(fp);
    printf("Save the RDB file success\n");
}

void bgsave()
{
    pid_t pid = fork();
    if (pid == 0) {
        sleep(1);   // 模拟落盘时间
        rdbSave();
        exit(0);
    } else if (pid < 0) {
        printf("Error: fork failed");
    }
    // 父亲进程continue
    server->rdbChildPid = pid;
    server->isBgSaving = 1;
}

void bgSaveIfNeeded()
{
    // 检查是否在BGSAVE
    if (server->isBgSaving) return;

    for(int i = 0; i < server->saveCondSize; i++) {
        time_t interval = time(NULL) - server->lastSave;
        if (interval >= server->saveParams[i].seconds && 
            server->dirty >= server->saveParams[i].changes
        ) {
            printf("CHECK BGSAVE OK, %d, %d\n", interval, server->dirty);
            bgsave();
            break;
        }
    }
}