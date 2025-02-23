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
    log_debug(" processing query from client, %s", buf);
    processClientQueryBuf(client);
    log_debug(" processed query from client");
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
 * @brief 向fd write写入len长度的buf， 于addwrite不同。
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
// TODO drawio 图
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
 * @brief 获取buf内RESP的长度， buf以RESP开头
 * 
 * @param [in] buf 
 * @param [in] len buf长度
 * @return ssize_t 
 */
static ssize_t getRespLength(const char* buf, size_t len) {
    if (len < 2) return -1;

    char type = buf[0];
    size_t i;

    switch (type) {
        case '+': // 简单字符串
        case '-': // 错误
        case ':': // 整数
            for (i = 1; i < len - 1; i++) {
                if (buf[i] == '\r' && buf[i + 1] == '\n') {
                    return i + 2;
                }
            }
            return -1;

        case '$': // 批量字符串， $10\r\nfoofoofoob\r\n
            if (len < 3) return -1;
            for (i = 1; i < len - 1; i++) {
                // 遍历找到第一个\r\n， 即length，然后根据i+length计算完整长度 
                if (buf[i] == '\r' && buf[i + 1] == '\n') {
                    char len_buf[32];
                    size_t prefix_len = i + 2;
                    strncpy(len_buf, buf + 1, i - 1);
                    len_buf[i - 1] = '\0';
                    int data_len = atoi(len_buf);
                    if (data_len <= 0) return prefix_len;    // $-1\r\n 返回5
                    if (len < prefix_len + data_len + 2) return -1; // 参数len太小了，不完整，返回-1
                    if (buf[prefix_len + data_len] == '\r' && buf[prefix_len + data_len + 1] == '\n') {
                        return prefix_len + data_len + 2;
                    }
                    return -1;
                }
            }
            printf("找不到\r\n ");
            return -1;

        case '*': // 数组 *2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
            if (len < 3) return -1;
            for (i = 1; i < len - 1; i++) {
                // 遍历找到第一个\r\n, 即数组大小，
                if (buf[i] == '\r' && buf[i + 1] == '\n') {
                    char len_buf[32];
                    size_t prefix_len = i + 2;
                    strncpy(len_buf, buf + 1, i - 1);
                    len_buf[i - 1] = '\0';
                    int num_elements = atoi(len_buf);
                    if (num_elements <= 0) return prefix_len; // *-1\r\n 返回5，前缀长度
                    size_t offset = prefix_len;
                    for (int j = 0; j < num_elements; j++) {
                        if (offset >= len) return -1;   // 如果大于buf的len，不完整
                        ssize_t elem_len = getRespLength(buf + offset, len - offset);
                        printf(" elem_len %u\n", elem_len);
                        if (elem_len == -1) return -1;  // 获取数组各元素
                        offset += elem_len;
                    }
                    return offset;
                }
            }
            return -1;

        default:
            printf("unexpected\n");
            return -1;
    }
}



/**
 * @brief read接口， 读取到client->readbuf, 两种情况：纯RESP, RESP+数据流。 只读取RESP部分。
 * @param [in] client 
 * @return resp长度。 通过这个标记 后面怎么处理readbuf
 */
int readToReadBuf(redisClient* client) {
    char temp_buf[1024]; // 临时缓冲区
    ssize_t n;

    // 清空 readBuf，准备接收新的 RESP
    sdsclear(client->readBuf);

    n = read(client->fd, temp_buf, sizeof(temp_buf));
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return -1; // 非阻塞模式暂无数据
        }
        return -1; // 读取结束或错误
    }

    // 将读取的数据追加到 readBuf
    client->readBuf = sdscatlen(client->readBuf, temp_buf, n);

    // 检查是否包含一个完整的 RESP
    // TODO 需要测试！
    ssize_t resp_len = getRespLength(client->readBuf->buf, sdslen(client->readBuf));

    // TODO 为了解决RDB读取问题，需要逐字读取，cat到readbuf,检查buf是否满足RESP， $<length>\r\n<RDB binary data>

    // TODO 返回值
    return resp_len;
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

// TODO 要详细化交互协议步骤， 来看readtoreadbuf咋实现
void repliReadHandler(aeEventLoop *el, int fd, void* privData)
{
    switch (server->replState) {
        case REPL_STATE_SLAVE_CONNECTING:
            readToReadBuf(server->master, 4);
            if (strstr(server->master->readBuf->buf, "PONG", 5) != NULL) {
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
            // FIXME: 比较有问题 没有读到OK, 相关readtoreadbuf都要重新设置
            if (strncmp(server->master->readBuf->buf, "+OK", 3) == 0) {
                // 收到REPLCONF OK, 转到REPLCONF
                server->replState = REPL_STATE_SLAVE_SEND_SYNC;
                log_debug("receive REPLCONF OK");
            }
            sdsclear(server->master->readBuf);
            aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, NULL);
            break;
        case REPL_STATE_SLAVE_SEND_SYNC:
        // +FULLRESYNC <repl-id> <offset>\r\n$<length>\r\n<RDB binary data>
            readToReadBuf(server->master, 9);
            log_debug("debug,  In send sync, received 9 bytes. %s", server->master->readBuf->buf);
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