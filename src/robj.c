/**
 * 对象模型
 *  数据结构：sds, 链表...
 *  对象类型：字符串、列表、哈希、集合
 *
 */

#include "robj.h"
#include <stdlib.h>
#include "sds.h"
#include <errno.h>
#include "redis.h"
#include <limits.h>
#include "log.h"

struct sharedObjects shared;

/**
 * @brief 根据encoding类型调用释放
 * 
 * @param [in] obj 
 */
static void _freeStringObject(robj *obj) 
{
    switch (obj->encoding) {
        case REDIS_ENCODING_RAW:
            sdsfree((sds*)(obj->ptr));
            break;
        case REDIS_ENCODING_INT:
            break;
        case REDIS_ENCODING_HT:
            break;
        default:
            break;
    }
}
/**
 * @brief 创建embedded str 编码的字符串对象. 嵌入的是sds
 * | robj 结构体 | sds 头部 | 字符串缓冲区 (len + 1) |
    ^             ^          ^
    obj           obj->ptr   ss->buf
 * @param [in] s 
 * @return robj* 
 */
robj* _createEmbeddedString(const char*s)
{
    sds* ss = NULL;
    unsigned long len = strlen(s);
    robj* obj = calloc(1, sizeof(robj) + sizeof(sds) + len + 1);
    obj->type = REDIS_STRING;
    obj->encoding = REDIS_ENCODING_EMBSTR;
    obj->ptr = (char*)obj + sizeof(robj);
    ss = obj->ptr;
    ss->len = 0;
    ss->free = len;
    ss->buf = (char*)obj + sizeof(robj) + sizeof(sds);

    sdscpy(ss, s);
    return obj;
}
robj* _createRawString(const char* s)
{
    robj* obj = malloc(sizeof(robj));
    obj->type = REDIS_STRING;
    obj->encoding = REDIS_ENCODING_RAW;
    obj->ptr = sdsnew(s);
    return obj;    
}

/**
 * 字符串转long整数，支持10进制和16进制
 * @param s
 * @param succeed
 * @return
 */
long _string2l(const char* s, int *succeed) {
    if (!s || !*s) {  // 空指针或空字符串直接失败
        *succeed = 0;
        return 0;
    }

    int base = 10;
    char *endptr;
    errno = 0;  // 清除 errno
    if (*s == '0' && (*(s+1) =='x' || *(s+1) =='X'))
    {
        base = 16;
    }
    long long value = strtoll(s, &endptr, base);

    // 判断是否成功解析
    if (*endptr != '\0' || endptr == s) {
        // 1. 如果 `endptr == s`，表示没有解析出任何数字
        // 2. 如果 `*endptr != '\0'`，说明解析后仍有剩余字符
        *succeed = 0;
        return 0;
    }

    // 检查范围（防止 溢出）
    if (errno == ERANGE) {
        *succeed = 0;
        return 0;
    }

    *succeed = 1;
    return value;
}


/**
 * @brief 构建long整数的字符串
 * 
 * @param [long] value
 * @return robj* 
 * @note 限制为
 */
robj* _createLongString(long value)
{
    // 
    if (value <= REDIS_SHAREAD_MAX_INT && shared.integers[value]) {
        return shared.integers[value];
    }
    robj* obj = malloc(sizeof(robj) );
    obj->type = REDIS_STRING;
    obj->encoding = REDIS_ENCODING_INT;
    obj->ptr = (void*)value;
    return obj;
}


/**
 * @brief Create a Shared Objects object
 *
 */
void createSharedObjects()
{
    // 1. 0-999整数
    for (int i = 0; i < REDIS_SHAREAD_MAX_INT; i++) {
        char buf[5];
        snprintf(buf, 4, "%d", i);
        buf[4] = '\0';
        shared.integers[i] = robjCreateStringObject(buf);
    }
    // 2. RESP
    shared.ok = robjCreateStringObject("+OK\r\n");
    shared.pong = robjCreateStringObject("+PONG\r\n");
    shared.err = robjCreateStringObject("-ERR\r\n");
    shared.keyNotFound = robjCreateStringObject("-ERR key not found\r\n");
    shared.bye = robjCreateStringObject("-bye\r\n");
    shared.invalidCommand = robjCreateStringObject("-Invalid command\r\n");
    shared.sync = robjCreateStringObject("+FULLSYNC\r\n");

    // REQUEST
    shared.ping = robjCreateStringObject("*1\r\n$4\r\nPING\r\n");
    shared.info = robjCreateStringObject("*1\r\n$4\r\nINFO\r\n");
}

/**
 * 对象系统初始化
 */
void robjInit()
{
    createSharedObjects();
}

/* robj */
robj* robjCreate(int type, void *ptr)
{
    robj* obj = malloc(sizeof(robj));
    obj->type = type;
    obj->encoding = REDIS_ENCODING_RAW;
    obj->refcount = 1;
    obj->ptr = ptr;
    return obj;
}

/**
 * @brief 引用计数，调用对应类型释放
 * 
 * @param [in] obj 
 */
void robjDestroy(robj* obj)
{
    if (obj->refcount <= 1) {
        switch (obj->type) {
            case REDIS_STRING:
                _freeStringObject(obj);
                break;
            case REDIS_LIST:
                break;
            default:
                break;
        }
        free(obj);
    } else {
        obj->refcount--;
    }
}

robj* robjCreateStringObject(const char*s)
{
    int succeed = 0;
    long value = _string2l(s, &succeed);
    if (succeed) {
        return _createLongString(value);
    }
    // TODO 为什么是32字节？
    if (strlen(s) < 32) {
        return _createEmbeddedString(s);
    }
    return _createRawString(s);
}

char* robjGetValStr(robj* obj)
{
    char buf[1024] = {0};

    switch (obj->type)
    {
    case REDIS_STRING:
        if (obj->encoding == REDIS_ENCODING_INT) {
            snprintf(buf, sizeof(buf), "%d", (int)(obj->ptr));
        } else {
            sds* s = (sds*)(obj->ptr);
            strncpy(buf, s->buf, sizeof(buf) - 1);
        }
        break;
    
    default:
        break;
    }
    return strdup(buf);
}