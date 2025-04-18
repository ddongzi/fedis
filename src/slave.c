/**
 * @file slave.c
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
#include "server.h"
#include "slave.h"
#include "rio.h"
#include "log.h"
#include "rdb.h"
#include "net.h"
#include "util.h"
#include "master.h"
struct Slave *slave;

void slaveStateInitFromMaster()
{
    if (slave == NULL)
    {
        slave = calloc(1, sizeof(struct Slave));
    }
    slave->dbnum = master->dbnum;
    slave->db = master->db;

    slave->clusterEnabled = master->clusterEnabled;
}

void sendPingToMaster()
{
    addWrite(slave->master, shared.ping);
}

void sendSyncToMaster()
{
    char *argv[] = {"SYNC"};
    char *buf = respFormat(1, argv);
    //  SYNC
    addWrite(slave->master, robjCreateStringObject(buf));
    free(buf);
}
void sendReplAckToMaster()
{
    char *argv[] = {"REPLACK"};
    char *buf = respFormat(1, argv);
    //  REPLCONF ACK <从dbid>
    addWrite(slave->master, robjCreateStringObject(buf));
    free(buf);
}

void sendReplconfToMaster()
{
    //  REPLCONF listening-port <从监听port>
    char *argv[] = {"REPLCONF", "listen-port", "6666"};
    char *buf = respFormat(3, argv);
    addWrite(slave->master, robjCreateStringObject(buf));
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
void repliWriteHandler(Connection *conn)
{
    log_debug("1.start, master.writeBuf len = %d", sdslen(slave->master->writeBuf));
    switch (slave->replState)
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

    char *msg = slave->master->writeBuf->buf;
    if (sdslen(slave->master->writeBuf) == 0)
    {
        // 如果没有数据，不可写
        log_debug("NO buffer available\n");
        connSetWriteHandler(conn, NULL);
        return;
    }
    int nwritten = connWrite(conn, msg, strlen(msg));
    if (nwritten <= 0)
        log_error("write error");

    sdsclear(slave->master->writeBuf);
    log_debug("2. after clear .master.writeBuf len = %d", sdslen(slave->master->writeBuf));
    connSetWriteHandler(conn, NULL);
}

void repliReadHandler(Connection *conn)
{
    switch (slave->replState)
    {
    case REPL_STATE_SLAVE_CONNECTING:
        readToReadBuf(slave->master);
        if (strstr(slave->master->readBuf->buf, "PONG") != NULL)
        {
            // 收到PONG, 转到REPLCONF
            slave->replState = REPL_STATE_SLAVE_SEND_REPLCONF;
            log_debug("receive PONG");
        }
        connSetWriteHandler(conn, repliWriteHandler);
        break;
    case REPL_STATE_SLAVE_SEND_REPLCONF:
        readToReadBuf(slave->master);
        if (strstr(slave->master->readBuf->buf, "+OK") != NULL)
        {
            // 收到REPLCONF OK, 转到REPLCONF
            slave->replState = REPL_STATE_SLAVE_SEND_SYNC;
            log_debug("receive REPLCONF OK");
        }
        connSetWriteHandler(conn, repliWriteHandler);
        break;
    case REPL_STATE_SLAVE_SEND_SYNC:
        // +FULLSYNC\r\n$<length>\r\n<RDB binary data>
        readToReadBuf(slave->master); // 应该只读取到fullsync部分
        printBuf("+FULLSYNC", slave->master->readBuf->buf, sdslen(slave->master->readBuf));
        if (strstr(slave->master->readBuf->buf, "+FULLSYNC") != NULL)
        {
            // 收到FULLSYNC, 后面就跟着RDB文件, 切换传输状态读
            slave->replState = REPL_STATE_SLAVE_TRANSFER;
            log_debug("receive FULLSYNC");
        }
        break;
    case REPL_STATE_SLAVE_TRANSFER:
        //  $<length>\r\n<RDB DATA>
        // 1. 因为解析不到一个RESP协议，就一直读直到读完。 即使包括其他RDB外的数据。但我们已知length，就可以读取固定字节数
        readToReadBuf(slave->master);

        printBuf("$<length>", slave->master->readBuf->buf, sdslen(slave->master->readBuf));
        int len = 0;
        int lenLength = 0;
        // 特殊用法， %n不消耗，记录消耗的字符数
        if (sscanf(slave->master->readBuf->buf, "$%d\r\n%n", &len, &lenLength) != 1)
        {
            log_error("sscanf failed");
            exit(1);
        }
        sdsrange(slave->master->readBuf, lenLength + 3, sdslen(slave->master->readBuf));

        log_debug("start transfer ..., len %u", len);
        receiveRDBfile(slave->rdbFileName, slave->master->readBuf->buf, len);
        log_debug("transfer finished.");
        rdbLoad(slave->db, slave->dbnum, slave->rdbFileName);
        log_debug("rdbload finished.");
        slave->replState = REPL_STATE_SLAVE_CONNECTED;
        connSetWriteHandler(conn, repliWriteHandler);
        break;
    case REPL_STATE_SLAVE_CONNECTED:
        readToReadBuf(slave->master);
        if (strstr(slave->master->readBuf->buf, "+OK") != NULL)
        {
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

/**
 * @brief 收到SLAVEOF命令,准备变为从，开始连接master，切换到CONNECTING
 *
 */
void slaveConnectMasterHandler(Connection *conn)
{
    slave->master = clientCreate(conn);
    slave->replState = REPL_STATE_SLAVE_CONNECTING;

    connSetReadHandler(conn, repliReadHandler);
    connSetWriteHandler(conn, repliWriteHandler);
}