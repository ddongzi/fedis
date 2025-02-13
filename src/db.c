#include  "db.h"
#include "sds.h"
#include "robj.h"

static unsigned long dbDictKeyHash(const void *key) {
    unsigned long hash = 5381;
    robj *obj = (robj*) key;
    sds* s = (sds*) (obj->ptr);
    const char *str = s->buf;
    while (*str) {
        hash = ((hash << 5) + hash) + *str; // hash * 33 + c
        str++;
    }
    return hash;
}
static void dbDictKeyfree(void* data, void* key)
{
    robjDestroy((robj*)key);
}

static void dbDictValfree(void* data, void* obj)
{
    robjDestroy((robj*)obj);
}
static int dbDictKeyCmp(void* data, const void* key1, const void* key2)
{
    robj* key1obj = (robj*)key1;
    robj* key2obj = (robj*)key2;
    sds* s1 = (sds*)(key1obj->ptr);
    sds* s2 = (sds*)(key2obj->ptr);
    return sdscmp(s1, s2);
}
// 有问题！！TODO 值是robj对象
dictType dbDictType = {
    .hashFunction = dbDictKeyHash,
    .keyDup = NULL,
   .valDup = NULL,
    .keyCompare = dbDictKeyCmp,
    .keyDestructor = dbDictKeyfree,
    .valDestructor = dbDictValfree,
};

/**
 * @brief 
 * 
 * @param [in] id 
 * @return redisDb* 
 * @note 正常流程中不会调用，db
 */
redisDb* dbCreate(int id)
{
    redisDb* db = malloc(sizeof(*db));
    db->id = id;
    db->dict = dictCreate(&dbDictType, NULL);

    return db;
}
void dbInit(redisDb* db, int id) 
{
    db->dict = dictCreate(&dbDictType, NULL);
    db->id = id;
}


void dbFree(redisDb* db)
{
    if (db->dict) dictRelease(db->dict);
    free(db);
}

void dbClear(redisDb* db)
{
    dictRelease(db->dict);
    db->dict = NULL;
}

int dbAdd(redisDb* db, robj* key, robj* value)

{
    if (db == NULL || key == NULL) return DB_DICT_ERR;
    return dictAdd(db->dict, (void*)key,(void*)value);
}

void* dbGet(redisDb* db, robj* key)
{
    if (db == NULL || key == NULL) return NULL;
    return dictFetchValue(db->dict, (void*)key);
}

int dbDelete(redisDb* db, robj* key)
{
    if (db == NULL || key == NULL) return DB_DICT_ERR;
    return dictDelete(db->dict, (void*)key);
}

void dbPrint(redisDb* db)
{
    dictIterator* di = dictGetIterator(db->dict);
    dictEntry* entry;
    while ((entry = dictIterNext(di))!= NULL) {
        printf("key: %s, val: %p\n", (char*)entry->key, entry->v.val);
    }
    dictReleaseIterator(di);
}