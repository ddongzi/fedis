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
#define REDIS_SENTINELPORT 7777
#define REDIS_DEFAULT_DBNUM 16
#define REDIS_MAX_CLIENTS 10000

#define MAX_LISTENERS 8

// 服务端三种角色：主 从 sentinel
#define SERVER_ROLE_MASTER 0
#define SERVER_ROLE_SLAVE 1
#define SERVER_ROLE_SENTINEL 2

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

char* respParse(const char* resp);

struct saveparam {
    time_t seconds; // 保存条件：秒
    int changes; // 保存条件：修改数
};

struct redisServer {

    // 基础配置
    int flags;  ///< 服务器类型

    ConnectionListener listeners[MAX_LISTENERS]; // 固定个数，

    int daemonize;  // 是否守护进程
    char *configfile; // 
    int role;

    // 运行时状态
    time_t unixtime;    // 当前时间, 秒进度
    long long mstime;   // 当前时间，毫秒
    int shutdownAsap;   // 是否立即关闭

    // 客户端连接
    int maxclients; // 最大客户端连接数
    list* clients;  // 客户端链表    
    list* clientsToClose;   // 待关闭客户端链表

    // 事件循环
    aeEventLoop* eventLoop; // 事件循环

    void* state; ///< 具体server实例
    
};



void initServer();
void initServerConfig();


#endif
