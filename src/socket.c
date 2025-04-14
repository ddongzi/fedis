/**
 * @file socket.c 通过connection包裹socket
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-03-29
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "connection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include "net.h"
#include "log.h"
/**
 * @brief connection 的 aeFileproc
 * 
 * @param [in] el 
 * @param [in] fd 
 * @param [in] clientData ：connection
 * @param [in] mask 
 */
void connSocketEventHandler(aeEventLoop* el, int fd, void* clientData, int mask)
{
    Connection* conn = (Connection*)clientData;
    if (conn->state == CONN_STATE_CONNECTING) {

    }

    int call_write = (mask & AE_WRITABLE) && conn->writeHandler;
    int call_read = (mask & AE_READABLE) && conn->readHandler;

    // 读写事件：处理逻辑
    if (call_write)
        conn->writeHandler(conn);
    if (call_read)
        conn->readHandler(conn);

}

/**
 * @brief 创建socket类型的connection
 * 
 * @param [in] el 
 * @return Connection* 
 */
Connection* connSocketCreate(aeEventLoop* el)
{
    Connection* connection = calloc(1, sizeof(Connection));
    connection->type = &CT_SOCKET;
    connection->state = CONN_STATE_NONE;
    connection->fd = -1;
    connection->el = el;
    return connection;
}

void connSocketClose(Connection* conn)
{
    if (conn->fd != -1) {
        // FD 资源
        aeDeleteFileEvent(conn->el, conn->fd, AE_WRITABLE | AE_READABLE);
        close(conn->fd);
        conn->fd = -1;
    }
    free(conn);
}
/**
 * @brief 发起连接
 * 
 * @param [in] conn 
 * @param [in] host 
 * @param [in] port 
 * @param [in] connectHandler 
 * @return int 
 */
int connSocketConnect(Connection* conn, const char* host, int port, ConnectionCallbackFunc connectHandler)
{
    int fd = anetTcpConnect(host, port);
    if (fd == -1) {
        conn->state = CONN_STATE_ERROR;
        return RET_ERR;
    }
    conn->fd = fd;
    conn->state = CONN_STATE_CONNECTING; 
    connectHandler(conn);
    aeCreateFileEvent(conn->el, conn->fd, AE_WRITABLE, conn->type->aeHandler, conn);
    return RET_OK;
}

int connSocketAccept(Connection* conn, ConnectionCallbackFunc acceptHandler)
{
    if (conn->state != CONN_STATE_ACCEPTING) {
        return RET_ERR;
    }
    conn->state = CONN_STATE_CONNECTED;
    acceptHandler(conn);
    return RET_OK;
}
int connSocketWrite(Connection* conn, const void* data, size_t datalen)
{
   
    int ret = send(conn->fd, data, datalen, 0);
    if (ret == -1)
        log_error("send error %s", strerror(errno));
    return ret;
}
int connSocketRead(Connection* conn, void* buf, size_t buflen)
{
    int ret = recv(conn->fd, buf, buflen, 0);
    if (ret == -1)
        log_error("send error %s", strerror(errno));
    return ret;
}
/**
 * @brief 注册连接写处理函数
 * 
 * @param [in] conn 
 * @param [in] writeHandler 
 * @return int 
 */
int connSocketSetWriteHandler(Connection* conn, ConnectionCallbackFunc writeHandler)
{
    if (conn->writeHandler == writeHandler) 
        return RET_OK;
    conn->writeHandler = writeHandler;

    if (!conn->writeHandler) 
        aeDeleteFileEvent(conn->el, conn->fd, AE_WRITABLE);
    else 
        aeCreateFileEvent(conn->el, conn->fd, AE_WRITABLE, conn->type->aeHandler, conn);

}
int connSocketSetReadHandler(Connection* conn, ConnectionCallbackFunc readHandler)
{
    if (conn->readHandler == readHandler)
        return RET_OK;
    conn->readHandler = readHandler;
    if (!conn->readHandler)
        aeDeleteFileEvent(conn->el, conn->fd, AE_READABLE);
    else 
        aeCreateFileEvent(conn->el, conn->fd, AE_READABLE, conn->type->aeHandler, conn);
}

static ConnectionType CT_SOCKET = {
   /* connection type initialize & finalize & configure */

    /* ae  */
    .aeHandler = connSocketEventHandler,
    /* create & close connection */
    .connCreate = connSocketCreate,
    .connClose = connSocketClose,

    /* connect & accept*/
    .connect = connSocketConnect,
    .accept = connSocketAccept,

    /* IO */
    .write = connSocketWrite,
    .read = connSocketRead,
    .setWriteHandler = connSocketSetWriteHandler,
    .setReadHandler = connSocketSetReadHandler,
};