/**
 * @file repli.c
 * @author your name (you@domain.com)
 * @brief 从服务器（主的客户端）
 * @version 0.1
 * @date 2025-02-28
 * @details
 *  salveof 命令服务器切换到从角色，连接到master。
 * 主从复制过程：
 * << 发送ping到 主。
 * >> 接受pong。确定连接已经建立。
 * << 发送从自己的信息，replconf: port-6666
 * >> 主返回replconf ok。
 * << 从发送sync，请求开始同步
 * >> 主返回FULLSYNC，开始同步数据
 * >> 主开始做rdb。向从传输rdb数据.
 * << 从接受rdb完成，返回replack。表示复制成功
 * << 持续heartbeat
 * TODO（为了重连：持久化server部分信息到conf，如role，master信息）
 * 在这个过程中，主服务和从服务器都没有新开线程/进程。可能会耗时。
 *
 * @copyright Copyright (c) 2025
 * 
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
 #include "redis.h"
#include "repli.h"
#include "rio.h"
#include "log.h"
#include "rdb.h"
#include "net.h"
#include "util.h"
#include "client.h"
#include "conf.h"
#include "resp.h"


void sendPingToMaster()
{
    addWrite(server->master, resp.ping);
}

void sendSyncToMaster()
{
    char* argv[] = {"SYNC"};
    char* buf = respEncodeArrayString(1, argv);
    //  SYNC
    addWrite(server->master, buf);
    free(buf);
}

void sendReplAckToMaster()
{
    char* argv[] = {"REPLACK"};
    char* buf = respEncodeArrayString(1, argv);
    addWrite(server->master, buf);
    free(buf);
}

void sendReplconfToMaster()
{
    //  REPLCONF listening-port <从监听port>
    log_debug("Send Replconf, slave-port: %d", server->port);
    char* portstr = calloc(1, REDIS_MAX_STRING);
    sprintf(portstr, "%d", server->port);
    char* argv[] = {"REPLCONF", "listen-port", portstr};
    char* buf = respEncodeArrayString(3, argv);
    addWrite(server->master, buf);
    free(portstr);
    free(buf);
}

/**
 * @brief 从服务器的 主fd写处理
 * 
 * @param [in] el 
 * @param [in] fd 主fd
 * @param [in] privData 
 */
void repliWriteHandler(aeEventLoop *el, int fd, void* privData)
{
    redisClient* c = privData; // 就是server.master
    switch (server->replState)
    {
    case REPL_STATE_SLAVE_CONNECTING:
        sendPingToMaster();
        log_debug(">> 1. [REPL_STATE_SLAVE_CONNECTING] send ping to master");
        break;
    case REPL_STATE_SLAVE_SEND_REPLCONF:
        sendReplconfToMaster();
        log_debug(">> 2. [REPL_STATE_SLAVE_SEND_REPLCONF] send replconf to master");
        break;
    case REPL_STATE_SLAVE_SEND_SYNC:
        //  发送SYNC
        sendSyncToMaster();
        log_debug(">> 3. [REPL_STATE_SLAVE_SEND_SYNC] send sync to master");
        break;
    case REPL_STATE_SLAVE_CONNECTED:
        //  发送REPLCONF ACK (心跳)
        sendReplAckToMaster();
        // log_debug(">> 4. [REPL_STATE_SLAVE_CONNECTED] send REPLACK to master");
        break;
    default:
        break;
    }
    // 其他的状态，如心跳

    char* msg = c->writeBuf->buf;
    if (sdslen(c->writeBuf) == 0) {
        // 如果没有数据，不可写
        log_debug("NO buffer available");
        aeDeleteFileEvent(el, fd, AE_WRITABLE);
        return;
    }
    size_t nwritten;
    rio sio;
    rioInitWithSocket(&sio, fd);
    nwritten = rioWrite(&sio, msg, strlen(msg));
    if (nwritten == 0 && sio.error) { 
        log_error("write failed");
        close(fd);
        return;
    }   
    sdsclear(c->writeBuf);

    aeDeleteFileEvent(el, fd, AE_WRITABLE);
}

/**
 * 从服务器的 主fd 读处理
 *
 * @param el
 * @param fd 主fd
 * @param privData
 */
void repliReadHandler(aeEventLoop *el, int fd, void* privData)
{
    // 也就是server.master
    redisClient* c = privData;
    rio sio;
    rioInitWithSocket(&sio, fd);
    char buf[NET_BUF_MAX_SIZE];
    ssize_t nread = rioRead(&sio, buf, NET_BUF_MAX_SIZE);
    
    assert(nread > 0);
    sdscatlen(c->readBuf, buf, nread);
    
    // buf内容一定与下面的case开头匹配
    switch (server->replState) {
        case REPL_STATE_SLAVE_CONNECTING:
            {
                if (strstr(c->readBuf->buf, "PONG") != NULL) {
                    // 收到PONG, 转到REPLCONF
                    server->replState = REPL_STATE_SLAVE_SEND_REPLCONF;
                    log_debug("<< 1. [REPL_STATE_SLAVE_CONNECTING] receive pong. => [REPL_STATE_SLAVE_SEND_REPLCONF]");
                }
                if (aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, c) == AE_ERROR)
                {
                    // TODO
                    reconnectMaster();
                }
                sdsclear(c->readBuf);
                break;
            }
        case REPL_STATE_SLAVE_SEND_REPLCONF:
            {
                if (strstr(server->master->readBuf->buf, "+OK")!= NULL) {
                    // 收到REPLCONF OK, 转到REPLCONF
                    server->replState = REPL_STATE_SLAVE_SEND_SYNC;
                    log_debug("<< 2. [REPL_STATE_SLAVE_SEND_REPLCONF] receive REPLCONF OK. => [REPL_STATE_SLAVE_SEND_SYNC]");

                }
                // 确认主从关系后，应该持久化
                update_config(server->configfile, "role", "slave");
                char master[128] = {0};
                snprintf(master, sizeof(master), "%s:%d", server->masterhost, server->masterport);
                update_config(server->configfile, "master", master);
                log_info("Save master relationship!.");

                if (aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, c) == AE_ERROR)
                {
                    reconnectMaster();
                } else
                {
                    sdsclear(c->readBuf);
                }
                break;
            }

        case REPL_STATE_SLAVE_SEND_SYNC:
            {
                // 读取+FULLSYNC
                // +FULLSYNC\r\n$<length>\r\n<RDB binary data>

                char* token = strtok(c->readBuf->buf, "\r\n");
                if (strcmp(token, "+FULLSYNC") == 0) {
                    sdsrange(c->readBuf, strlen(token) + 2, sdslen(c->readBuf)-1);
                    // 收到FULLSYNC, 后面就跟着RDB文件, 切换传输状态读
                    server->replState = REPL_STATE_SLAVE_TRANSFER;
                    log_debug("<< 3. [REPL_STATE_SLAVE_SEND_SYNC] receive FULLSYNC. => [REPL_STATE_SLAVE_TRANSFER]");
                }
            }
        case REPL_STATE_SLAVE_TRANSFER:
            {
                // printBuf("$<length>\r\n<RDB DATA> ", c->readBuf->buf, sdslen(c->readBuf));
                //  $<length>\r\n<RDB DATA>
                // 1. 因为解析不到一个RESP协议，就一直读直到读完。 即使包括其他RDB外的数据。但我们已知length，就可以读取固定字节数
                int len = 0;
                int lenLength = 0;
                // 特殊用法， %n不消耗，记录消耗的字符数
                if (sscanf(c->readBuf->buf, "$%d\r\n%n", &len, &lenLength)!= 1) {
                    log_error("<< 4.[REPL_STATE_SLAVE_TRANSFER] sscanf failed. Will repl again.  =>[REPL_STATE_SLAVE_CONNECTING]");
                    // 返回初始状态。重新repl
                    server->replState = REPL_STATE_SLAVE_CONNECTING;
                    sdsclear(c->readBuf);
                } else {
                    sdsrange(c->readBuf, lenLength, sdslen(c->readBuf) - 1);
                    log_debug("start transfer ..., len %u", len);
                    receiveRDBfile(c->readBuf->buf, len);
                    log_debug("receive finished.");
                    rdbLoad();
                    log_debug("rdbload finished.");
                    server->replState = REPL_STATE_SLAVE_CONNECTED;
                    log_debug("<< 4. [REPL_STATE_SLAVE_TRANSFER] finished. => [REPL_STATE_SLAVE_CONNECTED]");
                    if (aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, c) == AE_ERROR)
                    {
                        reconnectMaster();
                    } else
                    {
                        sdsclear(c->readBuf);
                    }
                }
                break;
            }
        case REPL_STATE_SLAVE_CONNECTED:
            if (strstr(c->readBuf->buf, "+OK") != NULL) {
                log_debug("<< 5. [REPL_STATE_SLAVE_CONNECTED] receive ok.  Normally slave !! √");
                if (aeCreateFileEvent(el, fd, AE_READABLE, readFromClient, c) == AE_ERROR)
                {
                    reconnectMaster();
                } else
                {
                    sdsclear(c->readBuf);
                }
            } else
            {
                sdsclear(c->readBuf);
            }
            break;
        default:
            break;
    }

    server->master->lastinteraction = server->unixtime;
}

/**
 * @brief 从服务器定时. 心跳检测
 * 
 * @return int 
 */
int slaveCron(aeEventLoop* eventLoop, long long id, void* clientData)
{
    // log_debug("Repli Cron.");
    assert(server->role == REDIS_CLUSTER_SLAVE);
    if (server->replState == REPL_STATE_SLAVE_CONNECTED) {
        /*
         * 从主从复制角度来看，我们应该始终假定主是在线的。
         * 1. 主服务器要掉线了，那就需要sentinel 故障切换等机制。
         * 2. 由于从心跳断了，主服务器主动断开，从认为主服务器掉线。 需要重连。
         */
        log_debug("Heartbeat. %d", server->unixtime - server->master->lastinteraction);
        if (aeCreateFileEvent(eventLoop, server->master->fd, AE_WRITABLE, repliWriteHandler, server->master) == AE_ERROR)
        {
            reconnectMaster();
        }
    }
    if (server->master &&
        server->unixtime - server->master->lastinteraction> MASTER_SLAVE_TIMEOUT
    ) {
        //
        log_warn("Master heartbeat timeout, will reconnect...");
        reconnectMaster();
    }
    return 5000;
}
