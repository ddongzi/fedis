#ifndef AOF_H
#define AOF_H

#include <stdbool.h>
#include "sds.h"
#include <pthread.h>
struct AOF
{
    int fd; // aof fd 长期打开
    sds* active_buf; // 主线程写的.  
    sds* io_buf; // io刷盘
    long long lastAofTime; // everysec 使用
    bool isAofing;
    pthread_mutex_t aof_mutex; // aof线程
    pthread_cond_t aof_cond; // 
};
void aof_load();
void aof_init();
void  flushAppendOnlyFile();
void* erverySecAOF(void* arg);

#endif