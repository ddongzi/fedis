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


void sendPingToMaster()
{
    addWrite(server->master, shared.ping);
    
}

void sendSyncToMaster()
{
    char* argv[] = {"SYNC"};
    char* buf = respFormat(1, argv);
    //  SYNC
    addWrite(server->master, robjCreateStringObject(buf));
    free(buf);

}
void sendReplAckToMaster()
{
    char* argv[] = {"REPLACK"};
    char* buf = respFormat(1, argv);
    //  REPLCONF ACK <从dbid>
    addWrite(server->master, robjCreateStringObject(buf));
    free(buf);
}

void sendReplconfToMaster()
{
    //  REPLCONF listening-port <从监听port>
    char* argv[] = {"REPLCONF", "listen-port", "6666"};
    char* buf = respFormat(3, argv);
    addWrite(server->master, robjCreateStringObject(buf));
    free(buf);
}
/**
 * @brief 命令传播：作为正常服务器处理命令
 *
 */
void handleCommandPropagate()
{
    log_debug("handle commandPropagate");
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
    log_debug("1.start, master.writeBuf len = %d", sdslen(server->master->writeBuf));
    switch (server->replState)
    {
    case REPL_STATE_SLAVE_CONNECTING:
        sendPingToMaster();
        log_debug("send ping to master");
        break;
    case REPL_STATE_SLAVE_SEND_REPLCONF:
        sendReplconfToMaster();
        log_debug("send replconf to master");
        break;
    case REPL_STATE_SLAVE_SEND_SYNC:
        //  发送SYNC
        sendSyncToMaster();
        log_debug("send sync to master");
        break;
    case REPL_STATE_SLAVE_CONNECTED:
        //  发送REPLCONF ACK
        sendReplAckToMaster();
        log_debug("send REPLACK to master");
        break;
    default:
        break;
    }

    char* msg = server->master->writeBuf->buf;
    if (sdslen(server->master->writeBuf) == 0) {
        // 如果没有数据，不可写
        log_debug("NO buffer available\n");
        aeDeleteFileEvent(el, fd, AE_WRITABLE);
        return;
    }
    size_t nwritten;
    rio sio;
    rioInitWithSocket(&sio, fd);
    nwritten = rioWrite(&sio, msg, strlen(msg));
    if (nwritten == 0 && sio.error) { 
        log_error("write failed\n");
        close(fd);
        return;
    }
    // TODO 默认全部写完  
    sdsclear(server->master->writeBuf);
    log_debug("2. after clear .master.writeBuf len = %d", sdslen(server->master->writeBuf));

    aeDeleteFileEvent(el, fd, AE_WRITABLE);
}


void repliReadHandler(aeEventLoop *el, int fd, void* privData)
{
    switch (server->replState) {
        case REPL_STATE_SLAVE_CONNECTING:
            readToReadBuf(server->master);
            if (strstr(server->master->readBuf->buf, "PONG") != NULL) {
                // 收到PONG, 转到REPLCONF
                server->replState = REPL_STATE_SLAVE_SEND_REPLCONF;
                log_debug("receive PONG");
            }
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
            break;
        case REPL_STATE_SLAVE_SEND_REPLCONF:
            readToReadBuf(server->master);
            if (strstr(server->master->readBuf->buf, "+OK")!= NULL) {
                // 收到REPLCONF OK, 转到REPLCONF
                server->replState = REPL_STATE_SLAVE_SEND_SYNC;
                log_debug("receive REPLCONF OK");
            }
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
            break;
        case REPL_STATE_SLAVE_SEND_SYNC:
            // +FULLSYNC\r\n$<length>\r\n<RDB binary data>
            readToReadBuf(server->master); // 应该只读取到fullsync部分
            printBuf("+FULLSYNC", server->master->readBuf->buf, sdslen(server->master->readBuf));
            if (strstr(server->master->readBuf->buf, "+FULLSYNC") != NULL) {
                // 收到FULLSYNC, 后面就跟着RDB文件, 切换传输状态读
                server->replState = REPL_STATE_SLAVE_TRANSFER;
                log_debug("receive FULLSYNC");
            }
            break;
        case REPL_STATE_SLAVE_TRANSFER:
            //  $<length>\r\n<RDB DATA> 
            // 1. 因为解析不到一个RESP协议，就一直读直到读完。 即使包括其他RDB外的数据。但我们已知length，就可以读取固定字节数
            readToReadBuf(server->master);

            printBuf("$<length>", server->master->readBuf->buf, sdslen(server->master->readBuf));
            int len = 0;
            int lenLength = 0;
            // 特殊用法， %n不消耗，记录消耗的字符数
            if (sscanf(server->master->readBuf->buf, "$%d\r\n%n", &len, &lenLength)!= 1) {
                log_error("sscanf failed");
                exit(1);
            }
            sdsrange(server->master->readBuf, lenLength + 3, sdslen(server->master->readBuf));

            log_debug("start transfer ..., len %u", len);
            receiveRDBfile(server->master->readBuf->buf, len);
            log_debug("transfer finished.");
            rdbLoad();
            log_debug("rdbload finished.");
            server->replState = REPL_STATE_SLAVE_CONNECTED;
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
            break;
        case REPL_STATE_SLAVE_CONNECTED:
            readToReadBuf(server->master);
            if (strstr(server->master->readBuf->buf, "+OK") != NULL) {
                log_debug("replication connection established.");
                handleCommandPropagate();
            }
            break;
        default:
            break;
    }

}



/**
 * @brief 从服务器定时
 * 
 * @return int 
 */
int replicationCron()
{
    // TODO:
}
