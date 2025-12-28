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
#include <string.h>
#include <unistd.h>
// todo
#define BASE_DIR "/home/dong/fedis"


int main(int argc, char **argv)
{
    log_set_level(LOG_DEBUG);
    log_info("Process id: [%d]", getpid());
    log_debug("hello log.");
    server = calloc(1,sizeof(struct redisServer));

    server->configfile = "server.conf";
    if (argc == 2)
    {
        char* conf_file = argv[1];
        server->configfile = strdup(conf_file);
    }
    initServerConfig();
    initServer();
    aeMain(server->eventLoop);

    return 0;
}
