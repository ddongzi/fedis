#ifndef CLIENT_H
#define CLIENT_H

#define CLIENT_ROLE_NORMAL 0
#define CLIENT_ROLE_MASTER 1
#define CLIENT_ROLE_SLAVE 2
#define CLIENT_ROLE_SENTINEL 3 // sentinel 客户实例

#include "sds.h"
#include "db.h"
#include "robj.h"

/**
 * @struct redisClient
 * @brief  client 对端状态结构，通用，1. 偏向于io, 2. resp解析响应
 * 
 */
typedef struct redisClient {
    connection* connection;
    int flags;  ///<
    sds* readBuf;
    sds* writeBuf;
    int dbid;
    redisDb* db;
    int argc;   ///< 参数个数
    robj** argv;    ///< 参数数组
    int replState; ///< 用于主服务器维护某个从服务器同步状态。 // TODO 可以考虑另外摘到slaveInstance
    
} redisClient;
redisClient *redisClientCreate(connection* conn);

void addWrite(redisClient* client, robj* obj) ;
void readToReadBuf(redisClient* client) ;
#endif
