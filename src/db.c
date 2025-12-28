/**
 *db
 * 键: sds字符串
 * 值：robj对象， long整数
 */
#include "typedefs.h"
#include  "db.h"

#include "list.h"
#include "sds.h"
#include "robj.h"
#include "log.h"

static unsigned long dbDictKeyHash(const void *key) {
    unsigned long hash = 5381;
    sds* s = (sds*) key;
    const char *str = s->buf;
    while (*str) {
        hash = ((hash << 5) + hash) + *str; // hash * 33 + c
        str++;
    }
    return hash;
}
static void dbDictKeyfree(void* data, void* key)
{
    sdsfree((sds*)key);
}

static void dbDictValfreeRobj(void* data, void* obj)
{
    robjDestroy((robj*)obj);
}
static int dbDictKeyCmp(void* data, const void* key1, const void* key2)
{
    return sdscmp((sds*)key1, (sds*)key2);
}
static void dbDictValfreelist(void* data, void* obj)
{
    listRelease((list*)obj);
}
dictType kvtype = {
    .hashFunction =  dbDictKeyHash,
    .keyCompare = dbDictKeyCmp,
    .valDup = NULL,
    .keyDup = NULL,
    .keyDestructor = dbDictKeyfree,
    .valDestructor = dbDictValfreeRobj,
};
dictType expiretype = {
    .hashFunction =  dbDictKeyHash,
    .keyCompare = dbDictKeyCmp,
    .valDup = NULL,
    .keyDup = NULL,
    .keyDestructor = dbDictKeyfree,
    .valDestructor = NULL,
};
dictType watchtype = {
    .hashFunction =  dbDictKeyHash,
    .keyCompare = dbDictKeyCmp,
    .valDup = NULL,
    .keyDup = NULL,
    .keyDestructor = dbDictKeyfree,
    .valDestructor = dbDictValfreelist,
};
void dbInit(redisDb* db, int id)
{
    db->kv = dictCreate(&kvtype, NULL);

    db->id = id;
    db->expires = dictCreate(&expiretype, NULL);
    db->watched_keys = dictCreate(&watchtype, NULL);
}


void dbFree(redisDb* db)
{
    if (db->kv) dictRelease(db->kv);
    free(db);
}

void dbClear(redisDb* db)
{
    dictRelease(db->kv);
    db->kv = NULL;
}

/**
 * @param db
 * @param key sds对象
 * @param value robj对象
 * @return
 */
int dbAdd(redisDb* db, sds* key,void* value)
{
    if (db == NULL || key == NULL) return DB_DICT_ERR;
    if (!dictContains(db->kv, (void*)key))
    {
        return dictAdd(db->kv, (void*)key,(void*)value);
    } else
    {
        return dictReplace(db->kv, (void*)key, (void*)value);
    }


}

void* dbGet(redisDb* db, sds* key)
{
    if (db == NULL || key == NULL) return NULL;
    return dictFetchValue(db->kv, (void*)key);
}

int dbDelete(redisDb* db, sds* key)
{
    if (db == NULL || key == NULL) return DB_DICT_ERR;
    return dictDelete(db->kv, (void*)key);
}
int dbSetExpire(redisDb *db, sds* key, long time)
{
    return dictAdd(db->expires, (void*)key, (void*)time);
}
/**
 *  返回过期键剩余时间
 * @param db
 * @param key 过期键
 * @return
 */
long dbGetTTL(redisDb *db, sds* key)
{
    long expireate = (long)dictFetchValue(db->expires, key);
    return expireate - time(NULL);
}

/**
 * 惰性检查 key是否 国企删除
 * @param key key过期检查
 */
void expireIfNeed(redisDb* db, sds* key)
{
    if (dictContains(db->expires, key))
    {
        long expire_at = (long)dictFetchValue(db->expires, key);
        time_t now = time(NULL);
        if (now > expire_at)
        {
            // 过期删除键。
            if (dictDelete(db->kv, (void*)key) > 0 && dictDelete(db->expires, (void*)key) > 0)
                log_debug("OK.Delete expire key  %s", key->buf);
        }
    }
}

/**
 * 添加client到key上监视. 如果key还没有监视列表，就创建
 * @param db
 * @param key
 * @param client
 */
void dbAddWatch(redisDb* db, sds* key, redisClient* client)
{
    list* clients;
    if (!dictContains(db->watched_keys, key))
    {
        clients = listCreate();
        dictAdd(db->watched_keys, key, clients);
    }
    clients = dictFetchValue(db->watched_keys, key);
    listAddNodeTail(clients, listCreateNode(client));
}

int dbIsWatching(redisDb* db, sds* key)
{
    return dictContains(db->watched_keys, key);
}

void dbPrint(redisDb* db)
{
    dictIterator* di = dictGetIterator(db->kv);
    dictEntry* entry;
    while ((entry = dictIterNext(di))!= NULL) {
        log_debug("key: %s, val: %p", (char*)entry->key, entry->v.val);
    }
    dictReleaseIterator(di);
}
