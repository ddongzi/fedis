#ifndef CLIENT_H
#define CLIENT_H

#define REDIS_CLIENT_NORMAL (1<<0)
#define REDIS_CLIENT_MASTER (1<<1)
#define REDIS_CLIENT_SLAVE (1<<2)
#define REDIS_CLIENT_SENTINEL (1<<3)
#define REDIS_CLIENT_FAKE (1<<4)
#define REDIS_MULTI (1<<5) // 处于事务入队状态
#define REDIS_EXEC (1<<6) // 处于事务执行状态
#define REDIS_DIRTY_CAS (1<<7) // 客户端监视的键被修改过
#define CLIENT_TO_CLOSE (1<<8) // 客户端待关闭标识

#include "sds.h"
#include "db.h"
#include "robj.h"
#include "error.h"
#include "typedefs.h"
#include "redis.h"
#define CLIENT_NAME_MAX 32

// 存储事务中命令状态
struct MultiCmd
{
    int argc;
    char** argv;
    redisCommand* cmd;
};

/**
 * @struct redisClient
 * @brief  client状态结构，不只是传统意义的client。 维持的master也是该结构，所以更像是对端peer。
 * 
 */
struct redisClient {
    // 基本信息
    int fd;
    int flags;  ///< 表示对端角色 REDIS_CLIENT_
    char* ip;
    int port;
    time_t lastinteraction; // 最后一次收到请求(或者具体请求)的时间
    int toclose; // 将要关闭

    // 读写缓冲
    sds* readBuf;
    sds* writeBuf;

    // 数据库
    int dbid;
    redisDb* db;

    // req命令处理
    int argc;   ///< 参数个数
    char** argv;    ///< 参数数组

    // repli复制特性
    int replState; ///< 对端同步状态。

    // sentinel 客户实例特性
    char* name; // 对端名称

    // 错误
    char err_msg[128];
    ErrorCode last_errno;

    // 事务队列
    sds** multcmds; // 固定大小，最大支持10个。
    int multiCmdCount;

} ;
redisClient* redisFakeClientCreate();
redisClient *redisClientCreate(int fd, char* ip, int port);
void freeClient(redisClient* client);
void clientToclose(redisClient* c);

void addWrite(redisClient* client, char* s) ;
void addWriteBuf(redisClient* client, char* buf, size_t len);

void readToReadBuf(redisClient* client) ;
void clientMultiAdd(redisClient* c);
#endif
