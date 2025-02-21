#include "net.h"
#include <stdarg.h>
#include "redis.h"
#include "rdb.h"
#include "log.h"

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
        log_debug("Address: %s, Port: %d\n", ip, port);
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
        log_debug("set reuse addr %s\n", strerror(errno));
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
    
    rv = getaddrinfo(bindaddr, _port, &hints, &servinfo);
    if (rv != 0) {
        log_error("get addrinfo failed, %s", gai_strerror(rv));
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
            log_debug( "TRY bind() failed%s\n", strerror(errno));
            close(sockfd);
            continue;
        }
        break;
    }
    if (p == NULL) {
        log_error("can't bind");
        return NET_ERR;
    }
    freeaddrinfo(servinfo);
    if (listen(sockfd, backlog) == -1) {
        log_error("listen: %s", strerror(errno));
        return NET_ERR;
    }
    log_debug("listening on port: %d addr: %s ,listen-fd: %d\n", port, bindaddr, sockfd);
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

    log_debug("Accepted connection from %s:%d,  fd %d\n", ip, port, cfd);

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
    log_debug("deal query from client, %s", respParse(buf));
    
    sdscat(client->readBuf, buf);
    processClientQueryBuf(client);
    // 注册写事件
    aeCreateFileEvent(el, fd, AE_WRITABLE, sendReplyToClient, client);
}
/**
 * @brief REPL_STATE_MASTER_SEND_RDB下的写事件处理
 * 
 * @param [in] el 
 * @param [in] fd 
 * @param [in] privdata 
 */
void sendRDBChunk(aeEventLoop *el, int fd, void *privdata )
{
    redisClient *client = (redisClient*) privdata;
    char* buf = calloc(2048, 1);
    ssize_t nread = read(server->rdbfd, buf, 2048);
    if (nread > 0) {
        anetIOWrite(fd, buf, nread);
        aeDeleteFileEvent(el, fd, AE_WRITABLE);
    }
}
/**
 * @brief 主读取RDB文件发送到slave, 切换fd写事件处理为 sendRDB
 * 
 * @param [in] client 
 */
void saveRDBToSlave(redisClient* client)
{
    int rdbfd = open(server->rdbFileName , O_RDONLY);
    if (rdbfd < 0) return;
    server->rdbfd = rdbfd;
    // 覆盖sendReplytoclient
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendRDBChunk, client);
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
    log_debug("reply to client %d, %s", client->fd, msg);
    sdsclear(client->writeBuf);

    // 状态转换
    if (client->replState == REPL_STATE_MASTER_WAIT_SEND_FULLSYNC) {
        client->replState = REPL_STATE_MASTER_SEND_RDB;
        saveRDBToSlave(client);
    } else {
        aeDeleteFileEvent(el, fd, AE_WRITABLE);
    }

}


// TODO redis进程主动退出清理
void netCleanup()
{
}

/**
 * @brief stream fd直接发送，与 addwrite加入buf延迟发送不同。！
 *  在发送RDB文件使用。
 * @param [in] fd 
 * @param [in] buf 
 * @param [in] len 
 * @return int 
 */
int anetIOWrite(int fd, char *buf, int len)
{
    int nwritten = send(fd, buf, len, 0);
    if (nwritten == -1) {
        log_debug("anet write error: %s\n", strerror(errno));
        return NET_ERR;
    }
    return nwritten;
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

    log_debug("anetconnect %s:%d\n", host, port);

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

    int nwritten = write(fd, msg, strlen(msg));
    if (nwritten == -1) {
        log_debug("write failed\n");
        close(fd);
        return;
    }
    sdsclear(server->master->writeBuf);
    aeDeleteFileEvent(el, fd, AE_WRITABLE);
}


/**
 * @brief 命令传播：作为正常服务器处理命令
 * 
 */
void handleCommandPropagate()
{
    log_debug("handle commandPropagate\n");
}

/**
 * @brief read 到client.readBuf。 用于文本字符串格式
 * @param [in] client 
 * @param [in] len 读取长度
 */
void readToReadBuf(redisClient* client, int len)
{
    sds* readBuf = client->readBuf;
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int nread;
    // 是RESP格式，+，-，*..
    nread = read(client->fd, buf, len);
    log_debug("read %s", buf);
    if (nread == -1) {
        return;
    }
    if (nread == 0) {
        // FIXME 应该标记正确清理，fd关闭,client状态也更新
        // close(fd);
        return;
    }
    sdscat(client->readBuf, buf);
}

/**
 * @brief read 到分配的buf，用于二进制文件等
 *  比如接受RDB文件
 * @param [in] client 
 * @param [in] buf 
 * @param [in] size
 */
int readToBuf(redisClient* client, char* buf, int size)
{
    int nread;
    nread = read(client->fd, buf, size);
    if (nread == -1) {
        return -1;
    }
    if (nread == 0) {
        return 0;
    }
    return nread;
}


void repliReadHandler(aeEventLoop *el, int fd, void* privData)
{
    switch (server->replState) {
        case REPL_STATE_SLAVE_CONNECTING:
            readToReadBuf(server->master, 5);
            if (strncmp(server->master->readBuf->buf, "+PONG", 5) == 0) {
                // 收到PONG, 转到REPLCONF
                server->replState = REPL_STATE_SLAVE_SEND_REPLCONF;
                log_debug("receive PONG");
            }
            sdsclear(server->master->readBuf);
            // aeDeleteFileEvent(el, fd, AE_READABLE); 此时没东西往里写，无需关注读
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
            break;
        case REPL_STATE_SLAVE_SEND_REPLCONF:
            readToReadBuf(server->master, 3);
            if (strncmp(server->master->readBuf->buf, "+OK", 3) == 0) {
                // 收到REPLCONF OK, 转到REPLCONF
                server->replState = REPL_STATE_SLAVE_SEND_SYNC;
                log_debug("receive REPLCONF OK");
            }
            sdsclear(server->master->readBuf);
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
        case REPL_STATE_SLAVE_SEND_SYNC:
            readToReadBuf(server->master, 9);
            // FIXME: 比较有问题
            if (strncmp(server->master->readBuf->buf, "+FULLSYNC", 9) == 0) {
                // 收到FULLSYNC, 后面就跟着RDB文件, 切换传输状态读
                server->replState = REPL_STATE_SLAVE_TRANSFER;
                log_debug("receive FULLSYNC");
            }
            break;
        case REPL_STATE_SLAVE_TRANSFER:
            log_debug("start transfer ...");
            char* buf = calloc(2048, 1);
            int nread = readToBuf(server->master, buf, 2048);
            receiveRDBfile(buf, nread);
            log_debug("transfer finished.");
            rdbLoad();
            log_debug("rdbload finished.");
            sdsclear(server->master->readBuf);
            server->replState = REPL_STATE_SLAVE_CONNECTED;
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
            break;
        case REPL_STATE_SLAVE_CONNECTED:
            readToReadBuf(server->master, 3);
            if (strncmp(server->master->readBuf->buf, "+OK", 3) == 0) {
                log_debug("replication connection established.");
                handleCommandPropagate();
            }
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
        log_debug("connectMaster failed: %s\n", strerror(errno));
        return;
    }
    // 非阻塞
    anetNonBlock(NULL, fd);
    anetEnableTcpNoDelay(NULL, fd);

    int err = 0;

    server->master = redisClientCreate(fd);
    server->replState = REPL_STATE_SLAVE_CONNECTING;
    log_debug("Connecting Master fd %d\n", fd);
    // 不能调换顺序。 epoll一个fd必须先read然后write， 否则epoll_wait监听不到就绪。
    aeCreateFileEvent(server->eventLoop, fd, AE_READABLE, repliReadHandler, NULL);
    aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
}