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
    dict *dict;          /* 存储键值对的主哈希表 */
    int id;              /* 数据库编号 */
} redisDb;

/* --------------------- 数据库 API --------------------- */

/* 数据库管理 */
redisDb *dbCreate(int id);
void dbFree(redisDb *db);
void dbClear(redisDb *db);
void dbInit(redisDb* db, int id) ;
/* 键值操作 */
int dbAdd(redisDb *db, robj* key, robj *value);
void *dbGet(redisDb *db, robj* key);
int dbDelete(redisDb *db, robj* key);

/* 过期管理 */
int dbSetExpire(redisDb *db, robj* key, uint64_t expire_time);
int dbCheckExpire(redisDb *db, robj* key);

/* 数据库信息 */
void dbPrint(redisDb *db);

#endif