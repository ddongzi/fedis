#ifndef REDIS_H
#define REDIS_H
#include <stdlib.h>
#include "ae.h"
#include "db.h"
#include "list.h"
#include "dict.h"
#include "robj.h"
#include "typedefs.h"
#include "client.h"
#include <stdbool.h>
#include "aof.h"
#include "ringbuffer.h"

#define REDIS_SERVERPORT 6666
#define REDIS_MAX_CLIENTS 10000

#define REDIS_CLUSTER_MASTER (1<<0)
#define REDIS_CLUSTER_SLAVE (1<<1)
#define REDIS_CLUSTER_SENTINEL (1<<2)


extern struct redisServer* server;

#define REDIS_SHAREAD_MAX_INT 999

#define REDIS_MAX_STRING 256

#define MASTER_REPLI_RINGBUFFER_SIZE 1024

// TODO 主从同步中， 主维持的buf. 应该作为环形缓冲区。


// 命令标志：不只有服务器角色，还应该有客户端角色，此后还可能会有更多。
// 应该修正lookup，给与更多的层次
#define CMD_MASTER (1<<0)   //      0001 主服务器可以执行
#define CMD_SLAVE (1<<2)    //      0100 从服务器可以执行
#define CMD_WRITE (1<<3)    //      1000 数据库写
#define CMD_READ (1<<4)     //     10000 数据库读
typedef void redisCommandProc(redisClient* client);
struct  redisCommand{
    int flags;  // CMD_
    char* name; //
    redisCommandProc* proc;
    int arity; // 参数个数. -x:表示至少X个变长参数（完整，包含操作字）
} ;

extern redisCommand commandsTable[];

struct saveparam {
    time_t seconds; // 保存条件：秒
    int changes; // 保存条件：修改数
};

struct redisServer {

    // 基础配置
    int id; // TODO 
    char *bindaddr;
    int port;
    int daemonize;  // 是否守护进程
    char *configfile; // 

    // 运行时状态
    time_t unixtime;    // 统一的粗粒度时间, 秒进度
    long long mstime;   // 当前时间，毫秒
    int shutdownAsap;   // 是否立即关闭

    // 客户端连接
    int maxclients; // 最大客户端连接数
    list * clients;  // 客户端链表    
    list * clientsToClose;   // 待关闭客户端链表

    // 数据库
    int dbnum;  // 数据库数量
    redisDb* db;    // 数据库数组
    dict* commands; // 命令表: 键是字符串, 命令名，值是cmd结构

    // 事件循环
    aeEventLoop* eventLoop; // 事件循环

    // TODO 考虑使用flags标
    int flags; // 角色 REDIS_CLUSTER_

    // master 特性
    unsigned char repli_buffer[MASTER_REPLI_RINGBUFFER_SIZE];
    long begin_offset; // 逻辑offset
    long last_offset; // 逻辑offset

    // Slave特性
    redisClient* master; // （从字段）主客户端
    char* masterhost; // （从字段）主host
    int masterport; // （从字段）主port
    int replState; ///< （从字段）状态: 从服务器维护自己主从复制状态。
    time_t repltimeout; // 心跳检测阈值. 从服务器检测主的阈值
    long offset; // 从服务器记录现在的同步offset。 -1表示还没同步过。0表示还没有增量同步，其他正常

    // 模块化

    // 持久化
    bool aofOn;
    bool rdbOn;

    //  RDB持久化 (Master)
    long long dirty; // 上次SAVE之后修改了多少次,set del 
    time_t lastSave;    // 上次SAVE时间
    int saveCondSize; // 
    struct saveparam* saveParams; // SAVE条件数组
    int rdbfd;     ///< 不关闭rdbfd
    char* rdbfile; //
    pid_t rdbChildPid; // 正在执行BGSAVE的子进程ID
    int isBgSaving; // 正在BGSAVE

    // aof持久化
    struct AOF aof;

    // sentinel 服务器特性
    dict* instances; // 监控的sentinel列表

};

void initServer();
void initServerConfig();
void readRespFromClient(aeEventLoop *el, int fd, void *privData);

void readFromClient(aeEventLoop *el, int fd, void *privData);
int serverCron(struct aeEventLoop* eventLoop, long long id, void* clientData);

void processClientQueryBuf(redisClient* client);

#endif
