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
 * 为了重连：持久化server部分信息到conf，如role，master,offset信息
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
    char offset[16] = {0};
    snprintf(offset, sizeof(offset), "%ld", server->offset);
    char* argv[2];
    argv[0] = "SYNC";
    argv[1] = strdup(offset);
    char* buf = respEncodeArrayString(2, argv);
    //  SYNC <OFFSET>
    addWrite(server->master, buf);
    free(buf);
    log_debug("Slave -> sync %ld", server->offset);
}

void sendReplAckToMaster()
{
    char* argv[2];
    argv[0] = "REPLACK";
    char buf[1024];
    snprintf(buf, sizeof(buf), "%ld", server->offset);
    argv[1] = strdup(buf);
    addWrite(server->master, respEncodeArrayString(2, argv));
    log_debug("Send replack(heartbeat) %ld", server->offset);
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
 * 做增量同步
 * 参考使用fake加载aof
 */
void doAppendSync()
{
    log_debug("do append sync");
    sds* sbuf = server->master->readBuf;
    long oldlen = sdslen(sbuf);
    redisClient* fkc = redisFakeClientCreate();
    // 从buf中读取一些完整的resp
    char* endptr;
    while (( endptr = respParse(sbuf->buf, sbuf->len)) != NULL)
    {
        sdscatlen(fkc->readBuf, sbuf->buf, endptr - sbuf->buf + 1);
        // 找到了一个有效的resp
        processClientQueryBuf(fkc);
        sdsrange(sbuf, endptr - sbuf->buf + 1, sdslen(sbuf) - 1);
    }
    log_debug("do append done");
    slaveUpdateOffset(server->offset + oldlen - sdslen(sbuf));
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
        /* TODO 为了断线后增量同步（缺失的命令传播）， SYNC应该伴随自己的同步偏移（状态），
         * 主判断如何给他同步返回FULLSYNC,或者增量SYNC，
         * 从根据请求方法进行请求， 读取/处理数据。
         * 增量同步：
         */
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
    
    while (sdslen(c->readBuf) > 0)
    {
        switch (server->replState) {
            case REPL_STATE_SLAVE_CONNECTING:
                {
                    if (strncmp(c->readBuf->buf, "+PONG\r\n", 7) != 0) {
                        // 死等正确的响应
                        break;
                    }
                    // 收到PONG, 转到REPLCONF
                    server->replState = REPL_STATE_SLAVE_SEND_REPLCONF;
                    log_debug("<< 1. [REPL_STATE_SLAVE_CONNECTING] receive pong. => [REPL_STATE_SLAVE_SEND_REPLCONF]");
                    sdsrange(c->readBuf, 7, sdslen(c->readBuf) - 1);
                    if (aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, c) == AE_ERROR)
                    {
                        reconnectMaster();
                    }
                    break;
                }
            case REPL_STATE_SLAVE_SEND_REPLCONF:
                {
                    if (strncmp(c->readBuf->buf, "+OK\r\n", 5) != 0) {
                        break;
                    }
                    server->replState = REPL_STATE_SLAVE_SEND_SYNC;
                    log_debug("<< 2. [REPL_STATE_SLAVE_SEND_REPLCONF] receive REPLCONF OK. => [REPL_STATE_SLAVE_SEND_SYNC]");

                    // 确认主从关系后，应该持久化
                    update_config(server->configfile, "role", "slave");
                    char master[128] = {0};
                    snprintf(master, sizeof(master), "%s:%d", server->masterhost, server->masterport);
                    update_config(server->configfile, "master", master);
                    log_info("Save master relationship!.");
                    sdsrange(c->readBuf, 5, sdslen(c->readBuf) - 1);
                    if (aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, c) == AE_ERROR)
                    {
                        reconnectMaster();
                    }
                    break;
                }

            case REPL_STATE_SLAVE_SEND_SYNC:
                {
                    // +FULLSYNC\r\n$<length>\r\n<RDB binary data>
                    if (strncmp(c->readBuf->buf, resp.fullsync, strlen(resp.fullsync)) == 0) {
                        sdsrange(c->readBuf, strlen(resp.fullsync), sdslen(c->readBuf)-1);
                        // 收到FULLSYNC, 后面就跟着RDB文件, 切换传输状态
                        server->replState = REPL_STATE_SLAVE_RECEIVE_RDB;
                        log_debug("<< 3. [REPL_STATE_SLAVE_SEND_SYNC] receive FULLSYNC. => [REPL_STATE_SLAVE_RECEIVE_RDB]");
                    }
                    if (strncmp(c->readBuf->buf, resp.appendsync, strlen(resp.appendsync)) == 0) {
                        sdsrange(c->readBuf, strlen(resp.appendsync), sdslen(c->readBuf)-1);
                        // 收到APPENDSYNC, 后面就跟着resp命令, 切换传输状态
                        server->replState = REPL_STATE_SLAVE_RECEIVE_APPENDSYNC;
                        log_debug("<< 3. [REPL_STATE_SLAVE_SEND_SYNC] receive appendSYNC. => [REPL_STATE_SLAVE_RECEIVE_APPENDSYNC]");
                    }
                    if (strncmp(c->readBuf->buf, resp.nosync, strlen(resp.nosync)) == 0) {
                        sdsrange(c->readBuf, strlen(resp.nosync), sdslen(c->readBuf)-1);
                        // nosync, 不需要同步
                        server->replState = REPL_STATE_SLAVE_CONNECTED;
                        log_debug("<< 3. [REPL_STATE_SLAVE_SEND_SYNC] receive NOSYNC. => [REPL_STATE_SLAVE_CONNECTED]");
                    }
                    break;
                }
            case REPL_STATE_SLAVE_RECEIVE_APPENDSYNC:
                // 增量同步传来一些resp写命令, 转为connected状态，正常处理
                log_debug("appendsync receive %ld bytes", sdslen(c->readBuf));
                doAppendSync();
                server->replState = REPL_STATE_SLAVE_CONNECTED;
                log_debug("<< 4. [REPL_STATE_SLAVE_RECEIVE_APPENDSYNC] => [REPL_STATE_SLAVE_CONNECTED]");
                break;
            case REPL_STATE_SLAVE_RECEIVE_RDB:
                {
                    // printBuf("$<length>\r\n<RDB DATA> ", c->readBuf->buf, sdslen(c->readBuf));
                    //  $<length>\r\n<RDB DATA>
                    int len = 0;
                    int lenLength = 0;
                    if (sscanf(c->readBuf->buf + 1, "%d%n", &len, &lenLength) != 1)
                    {
                        // 没有解析到。 重新sync
                        log_error("<< 4.[REPL_STATE_SLAVE_RECEIVE_RDB] receive failed.Sync again.  =>[REPL_STATE_SLAVE_SEND_SYNC]");
                        server->replState = REPL_STATE_SLAVE_SEND_SYNC;
                        sdsclear(c->readBuf);
                    } else {
                        sdsrange(c->readBuf, 1 + lenLength + 2, sdslen(c->readBuf) - 1);
                        log_debug("start transfer ..., len %u", len);
                        receiveRDBfile(c->readBuf->buf, len);
                        log_debug("receive finished.");
                        rdbLoad();
                        log_debug("rdbload finished.");
                        server->replState = REPL_STATE_SLAVE_CONNECTED;
                        log_debug("<< 4. [REPL_STATE_SLAVE_RECEIVE_RDB] finished. => [REPL_STATE_SLAVE_CONNECTED]");
                        // 更新offset
                        slaveUpdateOffset(0);
                        sdsrange(c->readBuf, len + 2, sdslen(c->readBuf) - 1);
                        if (aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, c) == AE_ERROR)
                        {
                            reconnectMaster();
                        } 
                    }
                    break;
                }
            case REPL_STATE_SLAVE_CONNECTED:
                if (strncmp(c->readBuf->buf, resp.ok, strlen(resp.ok)) != 0) {
                    break;
                }
                log_debug("<< 5. [REPL_STATE_SLAVE_CONNECTED] receive ok.  Normally slave !! √");
                sdsrange(c->readBuf, strlen(resp.ok), sdslen(c->readBuf) - 1);
                if (aeCreateFileEvent(el, fd, AE_READABLE, readFromClient, c) == AE_ERROR)
                {
                    reconnectMaster();
                }
                break;
            default:
                log_error("Unknow state!");
                break;
        }
        // 如果buf残留不能继续读取了，即半包。break 继续读取
        if (respParse(c->readBuf->buf, c->readBuf->len) == NULL) {
            
            break;
        }
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
    assert(server->flags & REDIS_CLUSTER_SLAVE);
    if (server->replState == REPL_STATE_SLAVE_CONNECTED) {
        /*
         * 从主从复制角度来看，我们应该始终假定主是在线的。
         * 1. 主服务器要掉线了，那就需要sentinel 故障切换等机制。
         * 2. 由于从心跳断了，主服务器主动断开，从认为主服务器掉线。 需要重连。
         */
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
/**
 * @brief 中断当前master连接，重新连接
 * 
 */
void reconnectMaster()
{
    freeClient(server->master);
    server->master = NULL;
    connectMaster();
}

/**
 * @brief 主切从/ 从断线重连
 * 已经在调用时候设置了 host，port
 */
void connectMaster()
{
    int fd = anetTcpConnect(server->masterhost, server->masterport);
    if (fd < 0)
    {
        log_debug("connectMaster failed: %s", strerror(errno));
        return;
    }
    // 非阻塞
    anetNonBlock(fd);
    anetEnableTcpNoDelay(fd);

    if (server->master == NULL)
    {
        server->master = redisClientCreate(fd, server->masterhost, server->masterport);
    }

    int err = 0;
    server->master->flags &= ~REDIS_CLIENT_NORMAL;
    server->master->flags |= REDIS_CLIENT_MASTER;

    // 每次从服务器启动 都要尝试同步
    server->replState = REPL_STATE_SLAVE_CONNECTING;
    // 上次同步位置
    char *endptr;
    long offset = -1;
    if (!string2long(get_config(server->configfile, "offset"), &offset)) {
        log_error("string2long failed. offset!");
        update_config(server->configfile, "offset", "-1");
    }
    server->offset = offset;
    // 不能调换顺序。 epoll一个fd必须先read然后write， 否则epoll_wait监听不到就绪。
    if (aeCreateFileEvent(server->eventLoop, fd, AE_READABLE, repliReadHandler, server->master) == AE_ERROR)
    {
        //
        reconnectMaster();
    }
    if (aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, server->master) == AE_ERROR)
    {
        reconnectMaster();
    }

    log_debug("Connected Master fd %d", fd);
}
/**
 * 从更新同步的offset
 * @param len 更新len个字节数据
 */
void slaveUpdateOffset(long new_offset)
{
    server->offset = new_offset;
    // 写入config
    char val[1024] = {0};
    snprintf(val, sizeof(val) - 1, "%ld", server->offset);
    update_config(server->configfile, "offset", val);
    log_debug("slave update offset: %ld", server->offset);
}