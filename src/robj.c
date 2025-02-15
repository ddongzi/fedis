#include "robj.h"
#include <stdlib.h>
#include "sds.h"
#include <errno.h>
#include "redis.h"
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
    // TODO : error malloc , there is extra pointer area.
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

int _string2i(const char* s, int *succeed)
{
    char* endptr;
    int value;
    value = strtoll(s, &endptr, 10);
    if (endptr == s) {
        // we can't get a availible integer from the string.
        *succeed = 0;
        return 0;
    }
    if (errno == ERANGE) {
        // the string was too large to fit into a long long integer.
        *succeed = 0;
        return 0;
    }
    *succeed = 1;
    return value;
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
        return _createIntString(value);
    }
    if (strlen(s) < 32) {
        return _createEmbeddedString(s);
    }
    return _createRawString(s);
}