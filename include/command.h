#ifndef COMMAND_H
#define COMMAND_H

#include "client.h"

// 命令标志：命令-服务器角色
#define CMD_MASTER (1<<0)   //      0001 主服务器可以执行
#define CMD_SLAVE (1<<2)    //      0100 从服务器可以执行
#define CMD_WRITE (1<<3)    //      1000 数据库写
#define CMD_READ (1<<4)     //     10000 数据库读

typedef void redisCommandProc(redisClient* client);
typedef  struct  {
    int flags;  // CMD_
    char* name; //
    redisCommandProc* proc;
    int arity; // 参数个数. -x:表示至少X个变长参数（完整，包含操作字）
} redisCommand;

#endif
