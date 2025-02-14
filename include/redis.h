#ifndef REDIS_H
#define REDIS_H

#include "ae.h"
#include "net.h"
#include "db.h"
#include "list.h"
#include "dict.h"
#include <stdlib.h>
#include "robj.h"

#define REDIS_SERVERPORT 6666
#define REDIS_DEFAULT_DBNUM 16
#define REDIS_MAX_CLIENTS 10000

extern struct redisServer* server;

struct redisCommand;

// 服务器为客户端状态结构
typedef struct redisClient {
    int fd;
    int flags;
    sds* queryBuf;
    sds* replyBuf;
    int dbid;
    redisDb* db;
    int argc;   // 参数个数
    robj** argv;    // 参数数组
} redisClient;
redisClient *redisClientCreate(int fd);
void processClientQueryBuf(redisClient* client);


typedef void redisCommandProc(redisClient* client);
typedef struct redisCommand {
    char* name; // 
    redisCommandProc* proc;
    int arity; // 参数个数. -x:表示至少X个变长参数（完整，包含操作字）
} redisCommand;

extern redisCommand commandsTable[];

struct saveparam {
    time_t seconds; // 保存条件：秒
    int changes; // 保存条件：修改数
};

struct redisServer {

    // 基础配置

    char *bindaddr;
    int port;
    int daemonize;  // 是否守护进程
    char *configfile; // 

    // 运行时状态
    time_t unixtime;    // 当前时间, 秒进度
    long long mstime;   // 当前时间，毫秒
    int shutdownAsap;   // 是否立即关闭

    // 客户端连接
    int maxclients; // 最大客户端连接数
    list* clients;  // 客户端链表    
    list* clientsToClose;   // 待关闭客户端链表

    // 数据库
    int dbnum;  // 数据库数量
    redisDb* db;    // 数据库
    dict* commands; // 命令表: 键是字符串，值是cmd结构



    // 事件循环
    aeEventLoop* eventLoop; // 事件循环

    // 分布式集群
    int clusterEnabled; // 是否开启集群

    // 模块化

    // 持久化
    long long dirty; // 上次SAVE之后修改了多少次,set del 
    time_t lastSave;    // 上次SAVE时间
    int saveCondSize; // 
    struct saveparam* saveParams; // SAVE条件数组
    
    // RDB持久化
    char* rdbFileName; //
    pid_t rdbChildPid; // 正在执行BGSAVE的子进程ID
    int isBgSaving; // 正在BGSAVE

    // 其他
    char* neterr;

};



void initServer();


#endif
