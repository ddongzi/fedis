#ifndef SENTINEL_H
#define SENTINEL_H
#include <time.h>
#include "dict.h"
#include "ae.h"
#include "net.h"
#include "client.h"
#define SENTINEL_LISTENPORT 7777
#define SENTINEL_MAXCONNECTIONS 30
#define SENTINEL_INSTANCE_MAXNAMELEN 32


extern struct Sentinel* sentinel;

// sentinel 本身是作为客户端连接 实例的， 所以这里实际是 对端（服务端）
typedef struct SentinelClientInstance {
    Client* client; ///< 通用读写客户端
    char* name;
    int role;   // 
    int isdown; //
    time_t lastPingTime; // 上次发送PING的时间，sentinel 记录
    time_t lastPongTime; // 上次收到PONG的时间，
    int downAfterPeriod; ///< down的阈值认定。 PONG超过阈值，标记DOWN
} SentinelClientInstance;

// sentinel 特性状态
struct Sentinel {
    char* id; // sentinel ID 唯一
    dict* instances; ///< 维护的实例字典。键：实例名。值：实例SentinelClientInstance。
};

// 监控实例状态。定期执行，发送PING，检查保活。
void sentinelMonitor(struct Sentinel *sentinel);
// 发送PING
void sentinelPing(SentinelClientInstance *ri);
// 检查是否保活。
void sentinelCheckHealth(SentinelClientInstance *ri);
// 主节点故障。开始故障转移
void sentinelFailover(SentinelClientInstance *ri);
// 更新实例信息
void sentinelUpdateMasterInfo(struct Sentinel *sentinel, SentinelClientInstance *ri);
void SentinelClientInstanceFree(SentinelClientInstance* instance);

void sentinelStateInit();
SentinelClientInstance* sentinelClientInstanceCreate(const char* name, const char* ip, const int port);
void sentinelInitExtra();

#endif