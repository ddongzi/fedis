/**
 * @file connection.h head-only
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

typedef void (*ConnectionCallbackFunc)(Connection *conn);
typedef struct ConnectionType
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

} ConnectionType;


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


// 通用listen
int connListen(ConnectionListener* listener);
// 能通用吗？？？
int connAccept(Connection* conn, ConnectionCallbackFunc callback);
