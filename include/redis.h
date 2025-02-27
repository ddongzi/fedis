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





// 主从复制状态
enum REPL_STATE {
    // 从服务器server.replState 字段
    REPL_STATE_SLAVE_NONE,          // (从）未启用复制
    REPL_STATE_SLAVE_CONNECTING,    // 正在连接主服务器
    REPL_STATE_SLAVE_SEND_REPLCONF,   // 发送port号
    REPL_STATE_SLAVE_SEND_SYNC,    // 发送SYNC请求
    REPL_STATE_SLAVE_TRANSFER,      // 接收RDB文件
    REPL_STATE_SLAVE_CONNECTED,      // 正常复制中
    // 主服务器维护主向从的状态。
    REPL_STATE_MASTER_NONE,     //
    REPL_STATE_MASTER_WAIT_PING,    // 正在等待PING
    REPL_STATE_MASTER_WAIT_SEND_FULLSYNC,  // 等待FULLSYNC发送
    REPL_STATE_MASTER_SEND_RDB, // 正在发送RDB
    REPL_STATE_MASTER_CONNECTED, // 主认为此次同步完成

};

#define REDIS_CLIENT_NORMAL 0
#define REDIS_CLIENT_MASTER 1
#define REDIS_CLIENT_SLAVE 2

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
void processClientQueryBuf(redisClient* client);


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

void selectDB(redisClient* client, int dbid);

void sendPingToMaster();
void sendReplconfToMaster();
void sendSyncToMaster();
void sendReplconfAckToMaster();

void addWrite(redisClient* client, robj* obj) ;
void readToReadBuf(redisClient* client) ;
#endif
