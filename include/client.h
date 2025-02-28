#ifndef CLIENT_H
#define CLIENT_H

#define REDIS_CLIENT_NORMAL 0
#define REDIS_CLIENT_MASTER 1
#define REDIS_CLIENT_SLAVE 2
#include "sds.h"
#include "db.h"
#include "robj.h"

/**
 * @struct redisClient
 * @brief  client状态结构，不只是传统意义的client。 维持的master也是该结构，所以更像是对端peer。
 * 
 */
typedef struct redisClient {
    int fd;
    int flags;  ///<
    sds* readBuf;
    sds* writeBuf;
    int dbid;
    redisDb* db;
    int argc;   ///< 参数个数
    robj** argv;    ///< 参数数组
    int replState; ///< 用于主服务器维护某个从服务器同步状态。
    
} redisClient;
redisClient *redisClientCreate(int fd);

void addWrite(redisClient* client, robj* obj) ;
void readToReadBuf(redisClient* client) ;
#endif
