/**
 * @file main.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-02-25
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "redis.h"
#include "log.h"
#include "sentinel.h"
int main(int argc, char **argv)
{

    log_set_level(LOG_DEBUG);
    log_debug("hello log.");

    // 优先获取服务器角色，才能初始化
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "--sentinel") == 0) {
            server->role = REDIS_ROLE_SENTINEL;
            break;
        }
    }
    // TODO 通过服务器角色 特性初始化：选择sentinel 结构
    sentinel = calloc(1, sizeof(sentinel)); // 1. sentinel 服务器
    server = calloc(1,sizeof(struct redisServer)); // 2. 普通主从服务器
    initServerConfig();
    // 参数覆盖配置
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            server->port = atoi(argv[i + 1]);
            i++;
        } 
    }
    initServer();
    aeMain(server->eventLoop);


    return 0;
}
