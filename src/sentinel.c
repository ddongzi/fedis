/**
 * @file sentinel.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-02-28
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include "sentinel.h"
#include "server.h"
#include "net.h"
#include "log.h"
#include <errno.h>
#include <string.h>
sentinel* sentinel;

/**
 * @brief host-1678392323-1024
 * 
 * @param [in] id_buf 
 * @param [in] len 
 */
static void generateSentinelID(char *id_buf, size_t len) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    char hostname[256];
    gethostname(hostname, sizeof(hostname)); // 主机名字
    snprintf(id_buf, len, "%s-%ld-%d", hostname, tv.tv_usec, getpid());
}

// key是 char*
static unsigned long dictKeyHash(const void* key)
{
    unsigned long hash = 5381;
    char* k = (char*)key;
    while (*k) {
        hash = ((hash << 5) + hash) + *k; // hash * 33 + c
        k++;
    }
    return hash;
}
static int dictKeyCmp(void* data, const void* key1, const void* key2)
{
    char* name1 = (char*)key1;
    char* name2 = (char*)key2;
    return strcmp(name1, name2);
}
static void dictKeyfree(void* data, void* key)
{
    free((char*)key);
}
static void dictValfree(void* data, void* val)
{
    sentinelRedisInstanceFree((sentinelRedisInstance*) val);
}

void sentinelRedisInstanceFree(sentinelRedisInstance* instance)
{
    // TODO
}

void sentinelStateInit()
{
    int maxIdlen = 64;  // 
    sentinel->id = calloc(1, sizeof(char) * maxIdlen);
    generateSentinelID(sentinel->id, maxIdlen);
    
    dictType dt = {
        .hashFunction = dictKeyHash,
        .keyDup = NULL,
        .valDup = NULL,
        .keyCompare = dictKeyCmp,
        .keyDestructor = dictKeyfree,
        .valDestructor = dictValfree,
    }
    sentinel->instances = dictCreate(&dt, NULL);
}

// 禁用一些command
void sentinelCommandsForbid()
{
}

sentinelRedisInstance* sentinelRedisInstanceCreate(connection* conn)
{
    sentinelRedisInstance* instance = (sentinelRedisInstance*)malloc(sizeof(sentinelRedisInstance));
    instance->conn= conn;
    instance->name = calloc(1, SENTINEL_INSTANCE_MAXNAMELEN);
    sprintf(instance->name, SENTINEL_INSTANCE_MAXNAMELEN - 1,"%s:%d", conn->ip, conn->port);
}


void parseConfIntoSentinel()
{
    FILE* fp = fopen("/home/dong/fedis/conf/sentinel.conf", "r");
    if (fp == NULL) {
        log_error("Couldn't open sentinel.conf, %s", strerror(errno));
        exit(1);
    }

    char line[1024];
    
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        // TODO
    }

}