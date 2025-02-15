#ifndef ROBJ_H
#define ROBJ_H

enum robj_encoding{
    REDIS_ENCODING_INT, // long类型整数
    REDIS_ENCODING_EMBSTR,  // embstr编码的sds
    REDIS_ENCODING_RAW, // sds
    REDIS_ENCODING_HT,  // 字典
    REDIS_ENCODING_LINKEDLIST,  // 双端链表
    REDIS_ENCODING_ZIPLIST, // 压缩列表
    REDIS_ENCODING_INTSET,  // 整数集合
    REDIS_ENCODING_SKIPLIST // 跳跃表和字典
};
enum robj_type{
    REDIS_STRING,
    REDIS_LIST, 
    REDIS_HASH, // 哈希
    REDIS_SET,  // 集合
    REDIS_ZSET, // 有序集合
};

typedef struct redisObject {
    unsigned type:4;        // 对象类型，例如字符串（REDIS_STRING）、列表（REDIS_LIST）
    unsigned encoding:4;    // 对象的编码方式，例如 RAW、INT 等
    unsigned lru:24;        // LRU 时间，用于记录对象的最近访问时间
    int refcount;           // 引用计数
    void* ptr;              // 指向对象具体数据的指针
} robj;

robj* robjCreate(int type, void* ptr);
void robjDestroy(robj* obj);


robj* robjCreateStringObject(const char*s);
void robjGetValStr(robj* obj, char* buf, int maxlen) ;
#endif