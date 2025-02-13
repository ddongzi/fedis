#ifndef DICT_H
#define DICT_H
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define DICT_OK 0
#define DICT_ERR -1
#define DICT_INITIAL_SIZE 4 // 初始化tablesize大小
#define DICT_LOAD_RATIO 0.2  // 缩容比率
typedef struct dictEntry dictEntry;  // 向前声明
struct dictEntry {
    void* key;
    union {
        void* val;
        uint64_t u64;
        int64_t s64;
    }v;
    dictEntry *next;    // 解决冲突。
} ;


typedef struct dictHT {
    dictEntry** table;  
    unsigned long size; // table数组大小
    unsigned long sizemask; //  哈希表大小掩码，计算索引值，总是等于size-1
    unsigned long used; // 已用节点数：键值对数量
} dictHT;

typedef struct dictType {
    unsigned long (*hashFunction)(const void* key); // 必须
    void* (*keyDup)(void* privdata, const void* key);   // 默认行为：直接赋值
    void* (*valDup)(void* privdata, const void* obj);   // 默认行为：直接赋值
    int (*keyCompare)(void* privdata, const void* key1, const void* key2); // 必须
    void (*keyDestructor)(void* privdata, void* key);   // 默认行为：不释放
    void (*valDestructor)(void* privdata, void* val);   // 默认行为：不释放
    
} dictType;
typedef struct dict {
    dictType* type;
    void* privdata; // 外部调用者的携带信息：比如seed

    dictHT ht[2];
    int rehashidx;  
} dict;

dict* dictCreate(dictType* type, void* privData);   
int dictAdd(dict* dict, const void* key, const void* val);
int dictReplace(dict* dict, const void* key, const void* val);
void* dictFetchValue(dict* dict, const void* key);
void* dictGetRandomKey(dict* dict);    // 从字典中随机返回一个key
int dictDelete(dict* dict, const void* key);   // 删除指定键值对
void dictRelease(dict* dict);   // 释放字典及键值对

dictEntry* dictAddRaw(dict* dict, const void* key);
dictEntry* dictFind(dict* dict, const void* key);


/* dict iterator*/
typedef struct dictIterator {
    dict* dict; // 当前遍历字典
    long index; // 当前遍历索引
    dictEntry* entry;  // 当前遍历entry
    int _htidx;     // 在遍历哪个ht，ht[0]还是ht[1]
} dictIterator;

dictIterator* dictGetIterator(dict* dict);
dictEntry* dictIterNext(dictIterator* iter);
void dictReleaseIterator(dictIterator* iter);

#endif
