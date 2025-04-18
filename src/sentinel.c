/**
 * @file sentinel.c
 * @author your name (you@domain.com)
 * @brief sentinel 本身都是作为client 区connect  实例对象的
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
#include "conf.h"
#include "string.h"
#include "socket.h"
struct Sentinel* sentinel;

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
    SentinelClientInstanceFree((SentinelClientInstance*) val);
}

void SentinelClientInstanceFree(SentinelClientInstance* instance)
{
    // TODO
}

void sentinelStateInitConfig()
{
    char* monitor_master = get_config(server->configfile,"sentinel-monitor");
    char* name = strtok(monitor_master, ",");
    char* ip = strtok(strtok(NULL, ","));
    int port = atoi(strtok(NULL, ","));

    dictAdd(sentinel->instances, name, SentinelClientInstanceCreate(name, ip, port));

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
    };
    sentinel->instances = dictCreate(&dt, NULL);
}

static void sentinelConnectHandler(Connection* conn)
{
    log_debug("Sentinel callback for connect TODO");
}
/**
 * @brief 创建client，connect
 * 
 * @param [in] name 
 * @param [in] ip 
 * @param [in] port 
 * @return SentinelClientInstance* 
 */
SentinelClientInstance* SentinelClientInstanceCreate(const char* name, const char* ip, const int port)
{
    if (!ip || !port) return;
    SentinelClientInstance* instance = (SentinelClientInstance*)malloc(sizeof(SentinelClientInstance));
    if (!name) instance->name = calloc(1, SENTINEL_INSTANCE_MAXNAMELEN);
    else instance->name = strdup(name);
    sprintf(instance->name, SENTINEL_INSTANCE_MAXNAMELEN - 1,"%s:%d", ip, port);

    Connection* conn = connCreate(server->eventLoop, TYPE_SOCKET);
    int ret = connConnect(conn, ip, port, sentinelConnectHandler);
    instance->client = clientCreate(conn);

    return instance;
}
