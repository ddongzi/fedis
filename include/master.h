/**
 * @file master.h
 * @author your name (you@domain.com)
 * @brief  
 * @version 0.1
 * @date 2025-03-11
 * @warning TODO 主从切换？
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef MASTER_H
#define MASTER_H
#include "command.h"
extern struct master *master;
struct master {

    // 数据库
    int dbnum;  // 数据库数量
    redisDb* db;    // 数据库数组

    // 分布式集群
    int clusterEnabled; // 是否开启集群

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
};
// 连接到对端master
void connectMaster();

#endif