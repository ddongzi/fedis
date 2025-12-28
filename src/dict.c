/**
 * 字典：
 * 键值对：void*, void*
 * 字典类型：hash函数等必要
 *
 *
 */

#include "dict.h"
#include "log.h"
// ------------static-------------------//

/**
 * @brief 设置entry的v
 *
 * @param [in] dict
 * @param [in] key
 * @param [in] val
 */
static void dictSetVal(dict *dict, dictEntry *entry, const void *val)
{
    if (dict->type->valDup)
    {
        entry->v.val = dict->type->valDup(dict->privdata, val);
    }
    else
    {
        entry->v.val = val;
    }
}
static void _dictReset(dictHT *ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizemask = 0;
    ht->used = 0;
}

static void _dictClear(dict *dict, dictHT *ht)
{
    for (int i = 0; i < ht->size && ht->used > 0; i++)
    {
        dictEntry *entry = ht->table[i];
        while (entry)
        {
            dictEntry *next = entry->next;
            if (dict->type->keyDestructor && entry->key) 
                dict->type->keyDestructor(dict->privdata, entry->key);
            if (dict->type->valDestructor && entry->v.val)
                dict->type->valDestructor(dict->privdata, entry->v.val);
            free(entry);
            entry = next;
            ht->used--;
        }
    }
    free(ht->table);
    _dictReset(ht);
}
/**
 * @brief
 *

 * @param [in] n
 * @return unsigned
 */
static unsigned long _nextpower(unsigned long n)
{
    unsigned long res = 1;
    while (res < n)
    {
        res = res << 1;
    }
    return res;
}

static double _dictLoadFactor(dict *dict)
{
    return (double)dict->ht[0].used / (double)dict->ht[0].size;
}

int dictIsRehashing(dict *dict)
{
    return dict->rehashidx != -1;
}

/**
 * @brief 设置entry的key
 *
 * @param [in] dict
 * @param [in] entry
 * @param [in] key
 */
static void dictSetKey(dict *dict, dictEntry *entry, const void *key)
{
    if (dict->type->keyDup)
    {
        entry->key = dict->type->keyDup(dict->privdata, key);
    }
    else
    {
        entry->key = key;
    }
}
/**
 * @brief 渐进式rehash
 *
 * @param [in] dict
 */
static void _dictRehashStep(dict *dict)
{

    // 跳过空桶，找到第一个非空桶
    while (dict->rehashidx < dict->ht[0].size && dict->ht[0].table[dict->rehashidx] == NULL)
    {
        dict->rehashidx++;
    }
    if (dict->rehashidx == dict->ht[0].size) {
        // 找不到非空桶， 说明rehash完了。  按理来说，不会走到这里！！！
        // log_debug("Unexpected !!!!! size %d, rehashidx %d\n", dict->ht[0].size, dict->rehashidx);
    }

    // 迁移rehashidx桶的第一个entry，
    dictEntry *entry = dict->ht[0].table[dict->rehashidx];
    if (dict->ht[0].table == NULL) {
        // log_debug("Unexpected table !!!!!!!!");
    }
    dict->ht[0].table[dict->rehashidx] = entry->next;

    // 添加到ht[1]
    unsigned int h = dict->type->hashFunction(entry->key);
    unsigned int idx = h & dict->ht[1].sizemask;
    entry->next = dict->ht[1].table[idx];
    dict->ht[1].table[idx] = entry;

    dict->ht[0].used--;
    dict->ht[1].used++;

    // log_debug("rehash : %s [%u]->[%u].  0used %d,   0size %d,     1used %d   1size %d", 
    //     (char*)entry->key ,dict->rehashidx, idx,
    //     dict->ht[0].used, dict->ht[0].size, dict->ht[1].used, dict->ht[1].size  // 注意，dict->ht[0]和dict->ht[1]是交换的，所以需要反转一下
    // );

    // 每一次rehash都去主动检查
    if (dict->ht[0].used == 0)
    {
        // ht[0]没有元素，rehash完成，将ht[1]赋值给ht[0]
        free(dict->ht[0].table);
        dict->ht[0] = dict->ht[1];
        _dictReset(&dict->ht[1]);
        dict->rehashidx = -1;
        // log_debug("rehash 完成");
        // log_debug("REHASH ok： 0used %d, 0size %d, 1used %d, 1size %d",
        //     dict->ht[0].used, dict->ht[0].size, dict->ht[1].used, dict->ht[1].size
        // );
        return;
    }
}

/**
 * @brief 扩缩容触发
 *
 * @param [in] dict
 * @param [in] newSize
 * @return int
 */
static int dictExpand(dict *dict, unsigned long newSize)
{
    dictHT dh;
    dh.size = newSize;
    dh.sizemask = newSize - 1;
    dh.used = 0;
    dh.table = calloc(newSize, sizeof(dictEntry *));

    if (dict->ht[0].table == NULL)
    {
        // dict还没初始化，
        dict->ht[0] = dh;
    }
    else
    {
        // 扩容，触发rehash
        dict->ht[1] = dh;
        dict->rehashidx = 0;
    }
}

/**
 * @brief 判断扩缩容，
 *
 * @param [in] dict
 * @param [in] newSize
 * @return int
 */
static int dictExpandIfNeed(dict *dict)
{
    unsigned long newSize;
    dictHT dh;

    if (dictIsRehashing(dict))
    {
        // 如果正在rehash，
        return DICT_OK;
    }
    if (dict->ht[0].size == 0)
    {
        // dict还没用过，size 0 不能计算负载因子，直接扩容
        return dictExpand(dict, DICT_INITIAL_SIZE);
    }
    // 就扩容
    if (dict->ht[0].used == dict->ht[0].size)
    {
        // log_debug("开始扩容");
        return dictExpand(dict, _nextpower(dict->ht[0].used * 2));
    }

    // 小于0.1缩容
    // log_debug("Ratio : used %d, size %d", dict->ht[0].used, dict->ht[0].size);
    if ((double)dict->ht[0].used / (double)dict->ht[0].size < DICT_LOAD_RATIO)
    {
        log_debug("开始缩容");
        // 如果缩容后小于最小size就不缩
        if (_nextpower(dict->ht[0].used) > DICT_INITIAL_SIZE)
        {
            return dictExpand(dict, _nextpower(dict->ht[0].used));
        }
        log_debug("小于 initial size 不缩容");
    }
    return DICT_OK;
}

/**
 * @brief 添加一个entry
 *
 * @param [in] dict
 * @param [in] entry
 */
static void dictAddEntry(dict *dict, dictEntry *entry)
{
    unsigned int h, idx;

    dictExpandIfNeed(dict);

    // 如果正在rehash
    if (dictIsRehashing(dict))
    {
        // 直接添加到ht[1]
        h = dict->type->hashFunction(entry->key);
        idx = h & dict->ht[1].sizemask;
        entry->next = dict->ht[1].table[idx];
        dict->ht[1].table[idx] = entry;
        dict->ht[1].used++;

        // 触发一次渐进式rehash
        _dictRehashStep(dict);
    }
    else
    {
        // 如果没有rehash，直接添加到ht[0]
        h = dict->type->hashFunction(entry->key);
        idx = h & dict->ht[0].sizemask;
        entry->next = dict->ht[0].table[idx];
        dict->ht[0].table[idx] = entry;
        dict->ht[0].used++;
    }
}

static int _dictInit(dict *d, dictType *type, void *privData)
{
    if (type  == NULL || type->hashFunction == NULL || type->keyCompare == NULL)
        return DICT_ERR;
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);
    d->type = type;
    d->privdata = privData;
    d->rehashidx = -1;
    return DICT_OK;
}

/**
 *
 * @param type type指针
 * @param privData 携带数据
 * @return
 */
dict *dictCreate(dictType *type, void *privData)
{
    dict *d = (dict *)malloc(sizeof(dict));
    int res = _dictInit(d, type, privData);
    return res == DICT_ERR ? NULL : d;
}

/**
 * @brief 根据key查找对应entry
 *
 * @param [in] dict
 * @param [in] key
 * @return dictEntry*
 */
dictEntry *dictFind(dict *dict, const void *key)
{
    if (dict == NULL || key == NULL) return NULL;
    if (dict->ht[0].size == 0)
    {
        return NULL;
    }
    unsigned int h = dict->type->hashFunction(key);
    for (int i = 0; i <= 1; i++)
    {
        unsigned int idx = h & dict->ht[i].sizemask;
        dictEntry *entry = dict->ht[i].table[idx];
        while (entry)
        {
            if (dict->type->keyCompare(dict->privdata, entry->key, key) == 0)
            {
                return entry;
            }
            entry = entry->next;
        }
        // 如果在ht[0]没找到，
        // 如果正在rehash，那就去ht[1]找
        // 如果没有rehash，那就是没找到
        if (!dictIsRehashing(dict))
        {
            return NULL;
        }
    }
    return NULL;
}
/**
 * @brief 只负责分配key，（如果key存在即返回. 不应该发生）。
 *
 * @param [in] dict
 * @param [in] key
 * @return dictEntry*
 */
dictEntry *dictAddRaw(dict *dict, const void *key)
{
    if (dict == NULL || key == NULL) return NULL;
    dictEntry *entry = dictFind(dict, key);
    if (entry == NULL)
    {
        entry = (dictEntry *)malloc(sizeof(dictEntry));
        entry->next = NULL;
        dictSetKey(dict, entry, key);
        dictSetVal(dict, entry, NULL);
        dictAddEntry(dict, entry);
    }
    return entry;
}

/**
 * @brief 如果key存在, 返回错误。
 *
 * @param [in] dict
 * @param [in] key
 * @param [in] val
 * @return int 失败返回-1， 成功返回0
 */
int dictAdd(dict *dict, const void *key, const void *val)
{
    if (dict == NULL || key == NULL) return DICT_ERR;

    dictEntry *entry = NULL;
    entry = dictFind(dict, key);
    if (entry) {
        // key冲突，
        return DICT_ERR;
    }
    
    entry = dictAddRaw(dict, key);
    dictSetVal(dict, entry, val);
    return DICT_OK;
}
/**
 * @brief 更新key处的值，key必须存在
 * 
 * @param [in] dict 
 * @param [in] key 
 * @param [in] val 
 * @return int 更新成功返回0， 否则返回-1
 */
int dictReplace(dict *dict, const void *key, const void *val)
{
    if (dict == NULL || key == NULL) return DICT_ERR;
    dictEntry *entry = dictFind(dict, key);
    if (entry) {
        dictSetVal(dict, entry, val);
        return DICT_OK;
    }
    return DICT_ERR;
}

void *dictFetchValue(dict *dict, const void *key)
{
    if (dict == NULL || key == NULL) return NULL;
    // 触发一次rehash
    if (dictIsRehashing(dict)) {
        _dictRehashStep(dict);
    }
    dictEntry *entry = dictFind(dict, key);
    if (entry) {
        return entry->v.val;
    }
    return NULL;
}
void *dictGetRandomKey(dict *d) {
    if (d == NULL) return NULL;
    dictEntry *entry;
    int htsize, index;
    int htidx = 0;  // 先从第一个哈希表开始

    if (d->ht[0].size == 0) return NULL;


    while (htidx < 2) {  // 遍历 0 号表，必要时再遍历 1 号表
        htsize = d->ht[htidx].size;

        // 随机选择一个 bucket（索引）
        index = rand() % htsize;

        // 如果 bucket 为空，则继续随机选择
        if ((entry = d->ht[htidx].table[index]) == NULL) {
            continue;
        }

        // 如果 bucket 内有多个 entry（链表），随机选择链表中的某个 entry
        int listLen = 0;
        dictEntry *iter = entry;
        while (iter) {
            listLen++;
            iter = iter->next;
        }

        int randEntryIndex = rand() % listLen;
        while (randEntryIndex--) {
            entry = entry->next;
        }

        return entry->key;  // 返回找到的随机 entry
    }

    return NULL;  // 不应该到这里
}







/**
 * @brief Deletes a key-value pair from the dictionary.
 *
 * @param dict The dictionary from which to delete the key-value pair.
 * @param key The key of the key-value pair to delete.
 *
 * @return DICT_OK if the key-value pair was successfully deleted, DICT_ERR otherwise.
 *
 * @note This function checks if the dictionary and key are not NULL, triggers an expansion if necessary,
 *       and performs a deletion if the key is found in the dictionary. If the dictionary is currently rehashing,
 *       the function triggers a rehash step before performing the deletion. If the key is not found,
 *       the function returns DICT_ERR.
 */
int dictDelete(dict *dict, const void *key)
{
    if (dict == NULL || key == NULL) return DICT_ERR;   

    // Check if expansion is needed
    dictExpandIfNeed(dict);

    if (dictIsRehashing(dict))
    {
        _dictRehashStep(dict);
    } 


    for (int i = 0; i <= 1; i++)
    {
        unsigned int h = dict->type->hashFunction(key);
        unsigned int idx = h & dict->ht[i].sizemask;
        dictEntry *entry = dict->ht[i].table[idx];
        dictEntry *prev = NULL;
        while (entry)
        {
            if (dict->type->keyCompare(dict->privdata, entry->key, key) == 0)
            {
                log_debug("delete ok");
                // Key-value pair found, perform deletion
                if (prev == NULL)
                {
                    dict->ht[i].table[idx] = entry->next;

                }
                else
                {
                    prev->next = entry->next;
                }
                if (dict->type->keyDestructor) {
                    dict->type->keyDestructor(dict->privdata, entry->key);
                }
                if (dict->type->valDestructor) {
                    dict->type->valDestructor(dict->privdata, entry->v.val);
                }
                free(entry);
                dict->ht[i].used--;
                return DICT_OK;
            }
            prev = entry;
            entry = entry->next;
        }
        // If key not found in ht[0],
        // If rehashing has already moved, check ht[1]
        // If not rehashing, key not found
        if (!dictIsRehashing(dict))
        {
            return DICT_ERR;
        }
    }
    return DICT_ERR;
}

/**
 *
 * @param dict
 * @param key
 * @return 包含返回 true
 */
int dictContains(dict* dict, const void* key)
{
    return dictFind(dict, key) != NULL;
}

/**
 * @brief 释放dict
 * 
 * @param [in] dict 
 */
void dictRelease(dict *dict)
{
    if (dict == NULL) return;
    _dictClear(dict, &dict->ht[0]);
    _dictClear(dict, &dict->ht[1]);
    free(dict);
}

/**
 * 获取一个迭代器。
 * @param dict
 * @return
 */
dictIterator *dictGetIterator(dict *dict)
{
    dictIterator *iter = (dictIterator *)malloc(sizeof(dictIterator));
    iter->dict = dict;
    iter->index = -1;
    iter->entry = NULL;
    iter->_htidx = 0;
    return iter;
}
dictEntry* dictIterNext(dictIterator *iter)
{
    dict* d = iter->dict;
    while(iter->_htidx < 2) {
        dictEntry* cur = iter->entry;
        if (d->ht[iter->_htidx].used == 0) {
            // 没有在使用
            iter->_htidx++;
            continue;
        }
        while (cur == NULL) {
            // 当前桶为空，移到下一个
            iter->index ++;
            if (iter->index == d->ht[iter->_htidx].size) {
                // 当前ht完了，
                iter->index = 0;
                iter->_htidx++;
                break;
            }
            // 当前ht没完
            cur = d->ht[iter->_htidx].table[iter->index];
        }
        if (cur) {
            // 找到了
            iter->entry = cur->next;
            return cur;
        }

    }
    return NULL;
}
void dictReleaseIterator(dictIterator* iter)
{
    free(iter);
}
int dictIsEmpty(dict* dict)
{
    return dict->ht[0].used == 0;
}

size_t dictSize(dict* dict)
{
    return dict->ht[0].used + dict->ht[1].used;
}