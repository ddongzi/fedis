#include <stdio.h>
#include "redis.h"
void rdbSave()
{
    printf("RDB save\n");
}

void bgsave()
{
    printf("================bgsave================\n");
    pid_t pid = fork();
    if (pid == 0) {
        sleep(1);   // 模拟落盘时间
        rdbSave();
        exit(0);
    } else if (pid < 0) {
        printf("Error: fork failed");
    }
    // 父亲进程continue
    server->rdbChildPid = pid;
    server->isBgSaving = 1;
}

void bgSaveIfNeeded()
{
    // 检查是否在BGSAVE
    if (server->isBgSaving) return;

    for(int i = 0; i < server->saveCondSize; i++) {
        time_t interval = time(NULL) - server->lastSave;
        if (interval >= server->saveParams[i].seconds && 
            server->dirty >= server->saveParams[i].changes
        ) {
            printf("CHECK BGSAVE OK, %d, %d\n", interval, server->dirty);
            bgsave();
            break;
        }
    }
}