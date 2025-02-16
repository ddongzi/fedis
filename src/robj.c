#include "robj.h"
#include <stdlib.h>
#include "sds.h"
#include <errno.h>
#include "redis.h"
#include <limits.h>
/* string object*/
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
 * @brief 创建embedded str 编码的字符串对象
 * 
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

int _string2i(const char* s, int *succeed) {
    if (!s || !*s) {  // 空指针或空字符串直接失败
        *succeed = 0;
        return 0;
    }

    char *endptr;
    errno = 0;  // 清除 errno
    long long value = strtoll(s, &endptr, 10);

    // 判断是否成功解析
    if (*endptr != '\0' || endptr == s) {
        // 1. 如果 `endptr == s`，表示没有解析出任何数字
        // 2. 如果 `*endptr != '\0'`，说明解析后仍有剩余字符
        *succeed = 0;
        return 0;
    }

    // 检查范围（防止 int 溢出）
    if (errno == ERANGE || value < INT_MIN || value > INT_MAX) {
        *succeed = 0;
        return 0;
    }

    *succeed = 1;
    return (int)value;
}


/**
 * @brief 
 * 
 * @param [in] value 
 * @return robj* 
 * @note 限制为
 */
robj* _createIntString(int value)
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
    int value = _string2i(s, &succeed);
    if (succeed) {
        // printf("CreateStringObj , INT, %d\n", value);
        return _createIntString(value);
    }
    if (strlen(s) < 32) {
        // printf("CreateStringObj , EMBSTR, %s\n", s);
        return _createEmbeddedString(s);
    }
    // printf("CreateStringObj , RAW, %s\n", s);
    return _createRawString(s);
}

void robjGetValStr(robj* obj, char* buf, int maxlen) 
{
    switch (obj->type)
    {
    case REDIS_STRING:
        if (obj->encoding == REDIS_ENCODING_INT) {
            snprintf(buf, maxlen, "%d", (int)(obj->ptr));
        } else {
            sds* s = (sds*)(obj->ptr);
            strncpy(buf, s->buf, maxlen - 1);
        }
        break;
    
    default:
        break;
    }
}