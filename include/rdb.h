#ifndef RDB_H
#define RDB_H

#define RDB_MAGIC "REDIS"
#define RDB_VERSION "0001"

#define RDB_TYPE_STRING REDIS_STRING
#define RDB_TYPE_LIST   REDIS_LIST
#define RDB_TYPE_SET    REDIS_SET
#define RDB_TYPE_ZSET   REDIS_ZSET
#define RDB_TYPE_HASH   REDIS_HASH

#define RDB_ENC_INT8 0xFC
#define RDB_ENC_INT16 0xFD
#define RDB_ENC_INT32 0xFE
#define RDB_EOF 0XFF
#define RDB_SELECTDB 0xFE   // 与INT32不冲突



void bgSaveIfNeeded();
void rdbLoad();

#endif