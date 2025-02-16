#include "net.h"
#include <stdarg.h>
#include "redis.h"
#include "rdb.h"

static void repliReadHandler(aeEventLoop *el, int fd, void* privData);
static void repliWriteHandler(aeEventLoop *el, int fd, void* privData);

void printAddrinfo(struct addrinfo *servinfo) {
    struct addrinfo *p;
    char ip[INET6_ADDRSTRLEN];
    for (p = servinfo; p != NULL; p = p->ai_next) {
        int port = 0;
        void *addr;
        if (p->ai_family == AF_INET) {
            // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            port = ntohs(ipv4->sin_port);
        } else if (p->ai_family == AF_INET6) {
            // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            port = ntohs(ipv6->sin6_port);
        } else {
            continue;
        }
        inet_ntop(p->ai_family, addr, ip, sizeof(ip));
        printf("Address: %s, Port: %d\n", ip, port);
    }
}
int anetSetError(char *err, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, 128, fmt, ap);
    va_end(ap);
    return NET_ERR;
}

/**
 * @brief 设置SO_REUSEADDR
 * 
 * @param [in] err 错误信息
 * @param [in] fd 
 * @return int 
 */
int anetSetReuseAddr(char *err, int fd)
{
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt SO_REUSEADDR: %s\n", strerror(errno));
        printf("set reuse addr %s\n", strerror(errno));
        return NET_ERR;
    }
    return NET_OK;
}

/**
 * @brief 创建TCP服务器
 * 封装了socket，bind，listen
 * @param [in] err 错误信息
 * @param [in] port 
 * @param [in] bindaddr 
 * @return int 监听fd
 */
int anetTcpServer(char *err, int port, char *bindaddr, int backlog)
{
    int sockfd, rv;
    char _port[6];  // 端口号最大65535
    struct addrinfo hints, *servinfo, *p;

    snprintf(_port, sizeof(_port), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;    // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;    // TCP
    hints.ai_flags = AI_PASSIVE;    // 用于bind
    
    printf("bindaddr = %s, port = %s\n", bindaddr ? bindaddr : "NULL", _port);
    rv = getaddrinfo(bindaddr, _port, &hints, &servinfo);
    printf("getaddrinfo returned %d, servinfo = %p\n", rv, (void *)servinfo);
    if (rv != 0) {
        anetSetError(err, "getaddrinfo: %s", gai_strerror(rv));
        return NET_ERR;
    }

    // 根据返回的地址列表，尝试创建socket
    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            continue;
        }
        anetSetReuseAddr(err, sockfd);
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            printf( "TRY bind() failed%s\n", strerror(errno));
            close(sockfd);
            continue;
        }
        break;
    }
    if (p == NULL) {
        anetSetError(err, "can't bind");
        return NET_ERR;
    }
    freeaddrinfo(servinfo);
    if (listen(sockfd, backlog) == -1) {
        anetSetError(err, "listen: %s", strerror(errno));
        return NET_ERR;
    }
    return sockfd;
}

int anetNonBlock(char *err, int fd)
{
    int flags;
    if ((flags = fcntl(fd, F_GETFL)) == -1) {
        anetSetError(err, "fcntl(F_GETFL): %s", strerror(errno));
        return NET_ERR;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        anetSetError(err, "fcntl(F_SETFL): %s", strerror(errno));
        return NET_ERR;
    }
    return NET_OK;

}
int anetEnableTcpNoDelay(char *err, int fd)
{
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt(TCP_NODELAY): %s", strerror(errno));
        return NET_ERR;
    }
    return NET_OK;

}
/**
 * @brief 保活
 * 
 * @param [in] err 
 * @param [in] fd 
 * @param [in] interval 
 * @return int 
 */
int anetKeepAlive(char *err, int fd, int interval)
{
    int yes = 1;
    int keepidle = 3000;
    int keepcnt = 5;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) == -1) {
        anetSetError(err, "setsockopt(SO_KEEPALIVE): %s", strerror(errno));
        return NET_ERR;
    }
    // 设置 TCP_KEEPIDLE（连接空闲时间）
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) == -1) {
        anetSetError(err, "setsockopt(TCP_KEEPIDLE): %s", strerror(errno));
        return NET_ERR;
    }

    // 设置 TCP_KEEPINTVL（探测包间隔时间）
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) == -1) {
        anetSetError(err, "setsockopt(TCP_KEEPINTVL): %s", strerror(errno));
        return NET_ERR;
    }

    // 设置 TCP_KEEPCNT（最大探测次数）
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) == -1) {
        anetSetError(err, "setsockopt(TCP_KEEPCNT): %s", strerror(errno));
        return NET_ERR;
    }
    return NET_OK;

}
/**
 * @brief 获取对端IP和端口
 * 
 * @param [in] fd 
 * @param [in] ip 值参数
 * @param [in] ip_len 
 * @param [in] port 值参数
 * @return int 
 */
int anetFormatPeer(int fd, char *ip, size_t ip_len, int *port)
{
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    if (getpeername(fd, (struct sockaddr*)&sa, &salen) == -1) {
        return NET_ERR;
    }
    if (sa.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in*)&sa;
        inet_ntop(AF_INET, &s->sin_addr, ip, ip_len);
        *port = ntohs(s->sin_port);
    } else {
        struct sockaddr_in6 *s = (struct sockaddr_in6*)&sa;
        inet_ntop(AF_INET6, &s->sin6_addr, ip, ip_len);
        *port = ntohs(s->sin6_port);
    }
    return NET_OK;

}

/**
 * @brief 封装accept，接受TCP连接。
 * @param [in] el 
 * @param [in] fd 
 * @param [in] privData 
 */
void acceptTcpHandler(aeEventLoop *el, int fd, void* data )
{
    int cfd, port;
    char ip[128];
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    cfd = accept(fd, (struct sockaddr*)&sa, &salen);
    if (cfd == -1) {
        return;
    }

    anetNonBlock(NULL, cfd);
    anetEnableTcpNoDelay(NULL, cfd);
    anetKeepAlive(NULL, cfd, 300);
    anetFormatPeer(cfd, ip, sizeof(ip), &port);
    // 创建client实例
    redisClient* client = redisClientCreate(cfd);
    listAddNodeTail(server->clients, listCreateNode(client));
    // 注册读事件
    aeCreateFileEvent(el, cfd, AE_READABLE, readQueryFromClient, client);
}

void readQueryFromClient(aeEventLoop *el, int fd, void* privData )
{
    redisClient* client = (redisClient*) privData;
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int nread;
    nread = read(fd, buf, sizeof(buf));
    if (nread == -1) {
        return;
    }
    if (nread == 0) {
        close(fd);
        return;
    }
    
    sdscat(client->readBuf, buf);
    processClientQueryBuf(client);
    // 注册写事件
    aeCreateFileEvent(el, fd, AE_WRITABLE, sendReplyToClient, client);
}
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata )
{
    redisClient *client = (redisClient*) privdata;
    int nwritten;
    char* msg = client->writeBuf->buf;
    nwritten = write(fd, msg, strlen(msg));
    if (nwritten == -1) {
        return;
    }
    printf("Send %d bytes: %s\n", nwritten, msg);
    sdsclear(client->writeBuf);
    aeDeleteFileEvent(el, fd, AE_WRITABLE);
}


// TODO redis进程主动退出清理
void netCleanup()
{
}

/**
 * @brief 作为客户端，连接到Host:port
 * 
 * @param [in] host 
 * @param [in] port 
 */
int anetTcpConnect(char* err, const char* host, int port)
{
    int sockfd = -1;
    char portStr[6];
    struct addrinfo hints, *servinfo, *p;
    int ret;

    printf("anetconnect %s:%d\n", host, port);

    snprintf(portStr, sizeof(portStr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((ret = getaddrinfo(host, portStr, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(ret));
        return NET_ERR;
    }
    printAddrinfo(servinfo);
    // 遍历返回的地址列表，尝试创建 socket 并连接
    for (p = servinfo; p != NULL; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            continue;  // 创建 socket 失败，尝试下一个地址
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            // FIXME: telnet可以，这不可以
            perror("connect failed fd ");
            close(sockfd);
            continue;  // 连接失败，尝试下一个地址
        }
        break;  // 连接成功，退出循环
    }

    freeaddrinfo(servinfo);  // 释放 `servinfo`

    if (p == NULL) {  // 遍历所有地址仍然连接失败
        fprintf(stderr, "Failed to connect to %s:%d\n", host, port);
        return NET_ERR;
    }

    return sockfd;  // 返回 socket 描述符
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
    switch (server->replState)
    {
    case REPL_STATE_CONNECTING:
        //  发送PING
        sendPingToMaster();
        break;
    case REPL_STATE_SEND_SYNC:
        //  发送SYNC
        sendSyncToMaster();
        break;
    case REPL_STATE_CONNECTED:
        // TODO 发送REPLCONF ACK
        sendReplconfAckToMaster();
        break;
    default:
        break;
    }

    char* msg = server->master->writeBuf->buf;
    int nwritten = write(fd, msg, strlen(msg));
    if (nwritten == -1) {
        printf("write failed\n");
        close(fd);
        return;
    }

    sdsclear(server->master->writeBuf);
    aeDeleteFileEvent(el, fd, AE_WRITABLE);
    aeCreateFileEvent(server->eventLoop, fd, AE_READABLE, repliReadHandler, NULL);
}

void repliReadHandler(aeEventLoop *el, int fd, void* privData)
{
    redisClient* client = server->master;
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int nread;
    nread = read(fd, buf, sizeof(buf));
    if (nread == -1) {
        printf("read failed\n");
        close(fd);
        return;
    }
    if (nread == 0) {
        printf("master disconnected\n");
        close(fd);
        return;
    }
    switch (server->replState) {
        case REPL_STATE_CONNECTING:
            if (strcmp(buf, "+PONG") == 0) {
                // 收到PONG, 发起REPLCONF
                sendReplconfToMaster();
                server->replState = REPL_STATE_SEND_SYNC;
            }
            aeDeleteFileEvent(el, fd, AE_READABLE);
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
            break;
        case REPL_STATE_SEND_SYNC:
            if (strncmp(buf, "+FULLSYNC", 9) == 0) {
                // 收到FULLSYNC, 后面就跟着RDB文件
                server->replState = REPL_STATE_TRANSFER;
            }

            break;
        case REPL_STATE_TRANSFER:
            receiveRDBfile(buf, nread);
            server->replState = REPL_STATE_CONNECTED;
            break;
        case REPL_STATE_CONNECTED:
            // TODO  后续命令传播
            handleCommandPropagate(buf, nread);
            break;
        default:
            break;
    }

}


/**
 * @brief 收到SLAVEOF命令，开始连接master，切换到CONNECTING
 * 
 */
void connectMaster()
{
    int fd = anetTcpConnect(server->neterr, server->masterhost, server->masterport);
    if (fd < 0) {
        printf("connectMaster failed: \n");
        return;
    }
    // 非阻塞
    anetNonBlock(NULL, fd);
    anetEnableTcpNoDelay(NULL, fd);

    server->master = redisClientCreate(fd);
    server->replState = REPL_STATE_CONNECTING;
    aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
    
}