#ifndef SLAVE_H
#define SLAVE_H

#include "client.h"
#include "master.h"
extren struct slave* slave;

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
// slave作为客户端实例状态
typedef struct {
    int replState; ///< 用于主服务器维护某个从服务器同步状态。 // TODO 可以考虑另外摘到slaveInstance
    client* client; ///< 实验性：与client联系
} slaveInstance;

// slave服务器特性状态
struct slave{
    // 数据库
    int dbnum;  // 数据库数量
    redisDb* db;    // 数据库数组

    // 集群
    client* master; // （从字段）主客户端
    char* masterhost; // （从字段）主host
    int masterport; // （从字段）主port
    int replState; ///< （从字段）状态: 从服务器维护自己主从复制状态。

    /***和主共有字段***/
    // 分布式集群
    int clusterEnabled; // 是否开启集群

    // 模块化
} ;



void sendPingToMaster();
void sendReplconfToMaster();
void sendSyncToMaster();


void repliWriteHandler(aeEventLoop *el, int fd, void* privData);
void repliReadHandler(aeEventLoop *el, int fd, void* privData);

// TODO 从主初始化slave
void slaveStateInitFromMaster();

#endif