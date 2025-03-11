#ifndef COMMAND_H
#define COMMAND_H

extern redisCommand commandsMasterTable[];
extern redisCommand commandsSlaveTable[];
extern redisCommand commandsSentinelTable[];

typedef void redisCommandProc(client* client);
typedef struct redisCommand {
    char* name; // 
    redisCommandProc* proc;
    int arity; // 参数个数. -x:表示至少X个变长参数（完整，包含操作字）
} redisCommand;


#endif