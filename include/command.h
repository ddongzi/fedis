#ifndef COMMAND_H
#define COMMAND_H
#include "dict.h"
#include "client.h"

typedef void (*commandProc)(Client* client);  
typedef struct  {
    char* name; // 
    commandProc proc;
    int arity; // 参数个数. -x:表示至少X个变长参数（完整，包含操作字）
} redisCommand;

extern dictType commandDictType;
extern redisCommand commandsMasterTable[];
extern redisCommand commandsSlaveTable[];
extern redisCommand commandsSentinelTable[];

#endif