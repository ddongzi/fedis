/**
 * @file repli.c
 * @author your name (you@domain.com)
 * @brief 从特性函数
 * @version 0.1
 * @date 2025-02-28
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

void sendPingToMaster()
{
    addWrite(server->master, shared.ping);
}

void sendSyncToMaster()
{
    char* argv[] = {"SYNC"};
    char* buf = resp_encode(1, argv);
    //  SYNC
    addWrite(server->master, robjCreateStringObject(buf));
    free(buf);
}
void sendReplHeartBeatToMaster()
{
    char* argv[] = {"HEARTBEAT"};
    char* buf = resp_encode(1, argv);
    addWrite(server->master, robjCreateStringObject(buf));
    free(buf);
}
void sendReplAckToMaster()
{
    char* argv[] = {"REPLACK"};
    char* buf = resp_encode(1, argv);
    addWrite(server->master, robjCreateStringObject(buf));
    free(buf);
}

void sendReplconfToMaster()
{
    //  REPLCONF listening-port <从监听port>
    log_debug("sendReplconf port: %d", server->port);
    char* portstr = calloc(1, REDIS_MAX_STRING);
    sprintf(portstr, "%d", server->port);
    char* argv[] = {"REPLCONF", "listen-port", portstr};
    char* buf = resp_encode(3, argv);
    log_debug("sendReplconf content : %s", buf);
    addWrite(server->master, robjCreateStringObject(buf));
    log_debug("addwrite ok");
    // TODO Buf free
    free(portstr);
    free(buf);
}

/**
 * @brief 向master写
 * 
 * @param [in] el 
 * @param [in] fd 
 * @param [in] privData 
 */
void repliWriteHandler(aeEventLoop *el, int fd, void* privData)
{
    redisClient* c = privData; // 就是server.master
    switch (server->replState)
    {
    case REPL_STATE_SLAVE_CONNECTING:
        sendPingToMaster();
        log_debug("<<== 1. [REPL_STATE_SLAVE_CONNECTING] send ping to master");
        break;
    case REPL_STATE_SLAVE_SEND_REPLCONF:
        sendReplconfToMaster();
        log_debug("<<== 2. [REPL_STATE_SLAVE_SEND_REPLCONF] send replconf to master");
        break;
    case REPL_STATE_SLAVE_SEND_SYNC:
        //  发送SYNC
        sendSyncToMaster();
        log_debug("<<== 3. [REPL_STATE_SLAVE_SEND_SYNC] send sync to master");
        break;
    case REPL_STATE_SLAVE_CONNECTED:
        //  发送REPLCONF ACK
        log_debug("ready send repl ack");
        sendReplAckToMaster();
        log_debug("<<== 4. [REPL_STATE_SLAVE_CONNECTED] send REPLACK to master");
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
    c->lastinteraction = server->unixtime;

}



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
            if (strstr(c->readBuf->buf, "PONG") != NULL) {
                // 收到PONG, 转到REPLCONF
                server->replState = REPL_STATE_SLAVE_SEND_REPLCONF;
                log_debug("==>> 1. [REPL_STATE_SLAVE_CONNECTING] receive pong. => [REPL_STATE_SLAVE_SEND_REPLCONF]");
            }
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, c);
            sdsclear(c->readBuf);
            break;
        case REPL_STATE_SLAVE_SEND_REPLCONF:
            if (strstr(server->master->readBuf->buf, "+OK")!= NULL) {
                // 收到REPLCONF OK, 转到REPLCONF
                server->replState = REPL_STATE_SLAVE_SEND_SYNC;
                log_debug("==>> 2. [REPL_STATE_SLAVE_SEND_REPLCONF] receive REPLCONF OK. => [REPL_STATE_SLAVE_SEND_SYNC]");

            }
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, c);
            sdsclear(c->readBuf);
            break;
        case REPL_STATE_SLAVE_SEND_SYNC:
            // 读取+FULLSYNC
            // +FULLSYNC\r\n$<length>\r\n<RDB binary data> 

            char* token = strtok(c->readBuf->buf, "\r\n");
            if (strcmp(token, "+FULLSYNC") == 0) {
                sdsrange(c->readBuf, strlen(token) + 2, sdslen(c->readBuf)-1);
                // 收到FULLSYNC, 后面就跟着RDB文件, 切换传输状态读
                server->replState = REPL_STATE_SLAVE_TRANSFER;
                log_debug("==>> 3. [REPL_STATE_SLAVE_SEND_SYNC] receive FULLSYNC. => [REPL_STATE_SLAVE_TRANSFER]");
            }
        case REPL_STATE_SLAVE_TRANSFER:
            printBuf("$<length>\r\n<RDB DATA> ", c->readBuf->buf, sdslen(c->readBuf));
            //  $<length>\r\n<RDB DATA> 
            // 1. 因为解析不到一个RESP协议，就一直读直到读完。 即使包括其他RDB外的数据。但我们已知length，就可以读取固定字节数
            int len = 0;
            int lenLength = 0;
            // 特殊用法， %n不消耗，记录消耗的字符数
            if (sscanf(c->readBuf->buf, "$%d\r\n%n", &len, &lenLength)!= 1) {
                log_error("==>> 4.[REPL_STATE_SLAVE_TRANSFER] sscanf failed. Will repl again.  =>[REPL_STATE_SLAVE_CONNECTING]");
                // 返回初始状态。重新repl
                server->replState = REPL_STATE_SLAVE_CONNECTING;
            } else {
                sdsrange(c->readBuf, lenLength, sdslen(c->readBuf) - 1);
                log_debug("start transfer ..., len %u", len);
                receiveRDBfile(c->readBuf->buf, len);
                log_debug("transfer finished.");
                rdbLoad();
                log_debug("rdbload finished.");
                server->replState = REPL_STATE_SLAVE_CONNECTED;
                log_debug("==>> 4. [REPL_STATE_SLAVE_TRANSFER] finished. => [REPL_STATE_SLAVE_CONNECTED]");
                aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, c);
            }
            sdsclear(c->readBuf);
            break;
        case REPL_STATE_SLAVE_CONNECTED:
            if (strstr(c->readBuf->buf, "+OK") != NULL) {
                log_debug("==>> 5. [REPL_STATE_SLAVE_CONNECTED] receive ok.  Normally slave !! √");
                aeCreateFileEvent(el, fd, AE_READABLE, readFromClient, c);
            }
            sdsclear(c->readBuf);
            break;
        default:
            break;
    }
    c->lastinteraction = server->unixtime;

}

/**
 * @brief 从服务器定时. 心跳检测
 * 
 * @return int 
 */
int replicationCron(aeEventLoop* eventLoop, long long id, void* clientData)
{
    log_debug("Repli Cron.");
    assert(server->role == REDIS_CLUSTER_SLAVE);
    if (server->master) {
        log_debug("TIME [%ld] %ld", server->repltimeout, server->unixtime - server->master->lastinteraction);
    }
    if (server->replState == REPL_STATE_SLAVE_CONNECTED) {
        log_debug("Heartbeat..");
        sendPingToMaster();
        aeCreateFileEvent(eventLoop, server->master->fd, AE_READABLE, readFromClient, server->master);
        aeCreateFileEvent(eventLoop, server->master->fd, AE_WRITABLE, sendToClient, server->master);
    }
    if (server->master && 
        (server->unixtime - server->master->lastinteraction) > server->repltimeout
    ) {
        // 心跳断开，重新连接
        log_warn("Master timeout, disconnecting...");
        reconnectMaster();
    }
    return 10000;
}
