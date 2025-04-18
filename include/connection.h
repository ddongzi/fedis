#ifndef CONNECTION_H
#define CONNECTION_H
/**
 * @file Connection.h head-only
 * @author your name (you@domain.com)
 * @brief For different connection type, such as socket,unix socket,tls. They have different dealing methods.
 * @version 0.1
 * @date 2025-03-29
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "ae.h"

// 通用返回码
#define RET_OK 0
#define RET_ERR -1

#define TYPE_SOCKET 0
#define TYPE_UNIXSOCKET 1
#define TYPE_TLS 2

typedef enum {
    CONN_STATE_NONE = 0,
    CONN_STATE_CONNECTING , // 主动connect触发
    CONN_STATE_ACCEPTING , // 服务端accept
    CONN_STATE_CONNECTED, // 服务端accept返回，简历连接
    CONN_STATE_CLOSED,
    CONN_STATE_ERROR,
} ConnectionState;

typedef struct Connection Connection;
typedef struct ConnectionListener ConnectionListener; 
typedef struct ConnectionType  ConnectionType;

typedef void (*ConnectionCallbackFunc)(Connection *conn);
struct ConnectionType // TODO 连接分客户服务吗？
{
    /* connection type initialize & finalize & configure */

    /* ae  */
    void (*aeHandler)(aeEventLoop *el, int fd, void *clientData, int mask); // ae handler:
    aeFileProc* acceptHandler;

    /* create & close connection */
    Connection *(*connCreate)(aeEventLoop *el);
    void (*connClose)(struct Connection *conn);

    /* connect & accept*/
    int (*connect)(Connection *conn, const char *host, int port, ConnectionCallbackFunc connectHandler);
    int (*accept)(Connection *conn, ConnectionCallbackFunc acceptHandler);
    int (*listen)(ConnectionListener* listener);

    /* IO */
    int (*write)(Connection *conn, const void *data, size_t datalen);
    int (*read)(Connection *conn, void *buf, size_t buflen);
    int (*setWriteHandler)(Connection *conn, ConnectionCallbackFunc writeHandler);
    int (*setReadHandler)(Connection *conn, ConnectionCallbackFunc readHandler);

} ;


/* 为多协议多监听扩展。 server.port只能一个*/
// TODO 去掉server.port, 支持listener链表，多端口监听。
struct ConnectionListener {
    int port;
    char* bindaddr;
    ConnectionType *type;
} ;

struct Connection {
    ConnectionType *type;
    ConnectionState state;

    int fd;
    aeEventLoop *el;

    ConnectionCallbackFunc writeHandler;
    ConnectionCallbackFunc readHandler;

    void* privData; // client
};

// 包含connection.h 的几个源文件会实现
extern ConnectionType CT_SOCKET;
extern ConnectionType CT_UNIXSOCKET;
extern ConnectionType CT_TLS;

ConnectionType* connGetConnType(int type);

// 通用listen
int connListen(ConnectionListener* listener);
Connection* connCreate(aeEventLoop* el, int type);
int connConnect(Connection* conn, char* ip, char* port, ConnectionCallbackFunc callback);
int connSetReadHandler(Connection* conn, ConnectionCallbackFunc callback);

#endif