#include "robj.h"
#include <stdlib.h>
#include "sds.h"


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

robj* robjCreateString(const char*s, unsigned long len)
{
    if (len <= 44) {
        // TODO 短字符串优化:embstr编码
    }
    robj* obj = robjCreate(REDIS_STRING, NULL);
    obj->ptr = sdsnew(s);
    return obj;
}