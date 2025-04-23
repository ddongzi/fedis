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
// todo
#define BASE_DIR "/home/dong/fedis"


int main(int argc, char **argv)
{

    log_set_level(LOG_DEBUG);
    log_debug("hello log.");
    server = calloc(1,sizeof(struct redisServer));
    server->role = REDIS_CLUSTER_MASTER; // 默认角色
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "--sentinel") == 0) {
            server->role = REDIS_CLUSTER_SENTINEL;
        }
        if (strcmp(argv[i], "--slave") == 0) {
            server->role = REDIS_CLUSTER_SLAVE;
        }
        if (strcmp(argv[i],"--config") == 0 && i + 1 < argc) {
            server->configfile = argv[i + 1];
        }
    }
    initServerConfig();

    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            server->port = atoi(argv[i + 1]);
            break;
        }
    }
    
    initServer();
    aeMain(server->eventLoop);

    return 0;
}
