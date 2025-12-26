#ifndef DB_H
#define DB_H
#include "dict.h"
#include <stdlib.h>
#include "sds.h"
#include "robj.h"

#define DB_DICT_ERR -1

/* 全局变量 */
extern dictType dbDictType;  // 数据库键值对


/* 过期时间相关 */
#define REDIS_NO_EXPIRE 0  /* 表示键不过期 */

/* Redis 数据库结构 */
typedef struct redisDb {
    dict *kv;          /* 存储键值对的主哈希表 */
    int id;              /* 数据库编号 */
    dict * expires; // 过期时间键值， {key:expiretime} ex
} redisDb;
// TODO 过期键值对需要持久化

/* --------------------- 数据库 API --------------------- */

/* 数据库管理 */
redisDb *dbCreate(int id);
void dbFree(redisDb *db);
void dbClear(redisDb *db);
void dbInit(redisDb* db, int id) ;
/* 键值操作 */
int dbAdd(redisDb *db, sds* key, void* value);
void *dbGet(redisDb *db, sds* key);
int dbDelete(redisDb *db, sds* key);

/* 过期管理 */
int dbSetExpire(redisDb *db, sds* key, long time);
int dbCheckExpire(redisDb *db, sds* key);
void expireIfNeed(redisDb* db, sds* key);


/* 数据库信息 */
void dbPrint(redisDb *db);
#endif

