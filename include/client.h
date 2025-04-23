#ifndef CLIENT_H
#define CLIENT_H

#define REDIS_CLIENT_NORMAL 0
#define REDIS_CLIENT_MASTER 1
#define REDIS_CLIENT_SLAVE 2
#define REDIS_CLIENT_SENTINEL 3

#include "sds.h"
#include "db.h"
#include "robj.h"

#define CLIENT_NAME_MAX 32

/**
 * @struct redisClient
 * @brief  client状态结构，不只是传统意义的client。 维持的master也是该结构，所以更像是对端peer。
 * 
 */
typedef struct redisClient {
    // 基本信息
    int fd;
    int flags;  ///< 表示对端角色 REDIS_CLIENT_
    char* ip;
    int port;

    // 读写缓冲
    sds* readBuf;
    sds* writeBuf;

    // 数据库
    int dbid;
    redisDb* db;

    // req命令处理
    int argc;   ///< 参数个数
    robj** argv;    ///< 参数数组

    // repli复制特性
    int replState; ///< 对端同步状态。

    // sentinel 客户实例特性
    char* name; // 对端名称
    
} redisClient;
redisClient *redisClientCreate(int fd, char* ip, int port);

void addWrite(redisClient* client, robj* obj) ;
void readToReadBuf(redisClient* client) ;
#endif
