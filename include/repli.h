#ifndef REPLI_H
#define REPLI_H

#define MASTER_SLAVE_TIMEOUT 20 // 从<=>主都采用这个

// 主从复制状态
enum REPL_STATE {
    // 从服务器server.replState 字段
    REPL_STATE_SLAVE_NONE,          // (从）未启用复制
    REPL_STATE_SLAVE_CONNECTING,    // 正在连接主服务器
    REPL_STATE_SLAVE_SEND_REPLCONF,   // 发送port号
    REPL_STATE_SLAVE_SEND_SYNC,    // 发送SYNC请求
    REPL_STATE_SLAVE_TRANSFER,      // 接收RDB文件
    REPL_STATE_SLAVE_CONNECTED,      // 等待REPL ACK返回
    // 主服务器维护主向从的状态。
    REPL_STATE_MASTER_NONE,     //
    REPL_STATE_MASTER_WAIT_PING,    // 正在等待PING
    REPL_STATE_MASTER_WAIT_SEND_FULLSYNC,  // 等待FULLSYNC发送
    REPL_STATE_MASTER_SEND_RDB, // 正在发送RDB
    REPL_STATE_MASTER_CONNECTED, // 主认为此次同步完成

};

void sendPingToMaster();
void sendReplconfToMaster();
void sendSyncToMaster();


void repliWriteHandler(aeEventLoop *el, int fd, void* privData);
void repliReadHandler(aeEventLoop *el, int fd, void* privData);
int slaveCron(aeEventLoop* eventLoop, long long id, void* clientData);


#endif