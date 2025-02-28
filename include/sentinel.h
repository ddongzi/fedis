#ifndef SENTINEL_H
#define SENTINEL_H
#include <time.h>
#include "dict.h"
#include "ae.h"

#define SENTINEL_LISTENPORT 7777
#define SENTINEL_MAXCONNECTIONS 30
sentinel* sentinel;

// sentinel 维护的redis实例状态：主、从、sentinel
typedef struct sentinelRedisInstance {
    char* name;
    char* ip;
    int port;
    int role;   // 
    int isdown; //
    time_t lastPingTime; // 上次发送PING的时间，sentinel 记录
    time_t lastPongTime; // 上次收到PONG的时间，
    int downAfterPeriod; ///< down的阈值认定。 PONG超过阈值，标记DOWN
} sentinelRedisInstance;

// sentinel 特性状态
typedef struct sentinel {
    char* id; // sentinel ID 唯一
    dict* instances; ///< 维护的实例字典。键：实例名。值：实例。
    
} sentinel;

// 监控实例状态。定期执行，发送PING，检查保活。
void sentinelMonitor(sentinel *sentinel);
// 发送PING
void sentinelPing(sentinelRedisInstance *ri);
// 检查是否保活。
void sentinelCheckHealth(sentinelRedisInstance *ri);
// 主节点故障。开始故障转移
void sentinelFailover(sentinelRedisInstance *ri);
// 更新实例信息
void sentinelUpdateMasterInfo(sentinel *sentinel, sentinelRedisInstance *ri);
void sentinelRedisInstanceFree(sentinelRedisInstance* instance);


#endif