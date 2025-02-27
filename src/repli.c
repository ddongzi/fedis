#include "redis.h"


/**
 * @brief 向master写
 * 
 * @param [in] el 
 * @param [in] fd 
 * @param [in] privData 
 */
void repliWriteHandler(aeEventLoop *el, int fd, void* privData)
{
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
    sizet nwritten;
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
            sdsclear(server->master->readBuf);
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
            break;
        case REPL_STATE_SLAVE_SEND_REPLCONF:
            readToReadBuf(server->master);
            if (strstr(server->master->readBuf->buf, "+OK")!= NULL) {
                // 收到REPLCONF OK, 转到REPLCONF
                server->replState = REPL_STATE_SLAVE_SEND_SYNC;
                log_debug("receive REPLCONF OK");
            }
            sdsclear(server->master->readBuf);
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
            sdsclear(server->master->readBuf);
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
            receiveRDBfile(server->master->readBuf->buf, nread);
            log_debug("transfer finished.");
            rdbLoad();
            log_debug("rdbload finished.");
            sdsclear(server->master->readBuf);  // TODO 不应该clear
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