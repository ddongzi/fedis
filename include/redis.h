#ifndef REDIS_H
#define REDIS_H
#include <stdlib.h>
#include "ae.h"
#include "db.h"
#include "list.h"
#include "dict.h"
#include "robj.h"
#include "client.h"

#define REDIS_SERVERPORT 6666
#define REDIS_DEFAULT_DBNUM 16
#define REDIS_MAX_CLIENTS 10000

#define REDIS_CLUSTER_MASTER 0x01
#define REDIS_CLUSTER_SLAVE 0x10

extern struct redisServer* server;
extern struct sharedObjects shared;

struct redisCommand;

#define REDIS_SHAREAD_MAX_INT 999
struct sharedObjects {
    // RESP res
    robj *ok;
    robj *pong;
    robj* err;
    robj* keyNotFound;
    robj* bye;
    robj* invalidCommand;
    robj* sync;

    // RESP request
    robj* ping;
    
    robj* integers[1000];
};


typedef void redisCommandProc(redisClient* client);
typedef struct redisCommand {
    char* name; // 
    redisCommandProc* proc;
    int arity; // 参数个数. -x:表示至少X个变长参数（完整，包含操作字）
} redisCommand;

extern redisCommand commandsTable[];
char* respParse(const char* resp);

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
    redisDb* db;    // 数据库数组
    dict* commands; // 命令表: 键是字符串，值是cmd结构

    // 事件循环
    aeEventLoop* eventLoop; // 事件循环

    // 分布式集群
    int clusterEnabled; // 是否开启集群
    int role; // 角色
    redisClient* master; // （从字段）主客户端
    char* masterhost; // （从字段）主host
    int masterport; // （从字段）主port
    int replState; ///< （从字段）状态: 从服务器维护自己主从复制状态。

    // 模块化

    // 持久化
    long long dirty; // 上次SAVE之后修改了多少次,set del 
    time_t lastSave;    // 上次SAVE时间
    int saveCondSize; // 
    struct saveparam* saveParams; // SAVE条件数组
    
    // RDB持久化
    int rdbfd;     ///< 不关闭rdbfd
    char* rdbFileName; //
    pid_t rdbChildPid; // 正在执行BGSAVE的子进程ID
    int isBgSaving; // 正在BGSAVE

    // 其他

};



void initServer();
void initServerConfig();


#endif
