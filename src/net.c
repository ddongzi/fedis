
/**
 * @file net.c
 * @author your name (you@domain.com)
 * @brief TCP连接配置，RESP协议
 * @version 0.1
 * @date 2025-02-27
 *
 * @copyright Copyright (c) 2025
 *
 */
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <stdarg.h>

#include "redis.h"
#include "rdb.h"
#include "log.h"
#include "rio.h"
#include "repli.h"
#include "net.h"

static void printAddrinfo(struct addrinfo *servinfo)
{
    struct addrinfo *p;
    char ip[INET6_ADDRSTRLEN];
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        int port = 0;
        void *addr;
        if (p->ai_family == AF_INET)
        {
            // IPv4
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            port = ntohs(ipv4->sin_port);
        }
        else if (p->ai_family == AF_INET6)
        {
            // IPv6
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            port = ntohs(ipv6->sin6_port);
        }
        else
        {
            continue;
        }
        inet_ntop(p->ai_family, addr, ip, sizeof(ip));
        log_debug("Address: %s, Port: %d", ip, port);
    }
}

/**
 * @brief 设置SO_REUSEADDR
 *
 * @param [in] err 错误信息
 * @param [in] fd
 * @return int
 */
static int anetSetReuseAddr(int fd)
{
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
    {
        log_error("set reuse addr, %s", strerror(errno));
        return NET_ERR;
    }
    return NET_OK;
}

/**
 * @brief 设置非阻塞
 *
 * @param [in] fd
 * @return int
 */
static int anetNonBlock(int fd)
{
    int flags;
    if ((flags = fcntl(fd, F_GETFL)) == -1)
    {
        log_error("fcntl get (%d) failed", fd);
        return NET_ERR;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        log_error("fcntl set NONBLOCK (%d) failed", fd);
        return NET_ERR;
    }
    return NET_OK;
}

/**
 * @brief 启用nodelay ,保证实时性
 *
 * @param [in] fd
 * @return int
 */
static int anetEnableTcpNoDelay(int fd)
{
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) == -1)
    {
        log_error("set tcp nodelay fd(%d) failed", fd);
        return NET_ERR;
    }
    return NET_OK;
}
/**
 * @brief 设置TCP保活
 *
 * @param [in] err
 * @param [in] fd
 * @param [in] interval
 * @return int
 */
static int anetKeepAlive(int fd, int interval)
{
    int yes = 1;
    int keepidle = 3000;
    int keepcnt = 5;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) == -1)
    {
        log_error("set tcp keepalive fd(%d) failed", fd);
        return NET_ERR;
    }
    // 设置 TCP_KEEPIDLE（连接空闲时间）
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle)) == -1)
    {
        log_error("set tcp keepidle fd(%d) failed", fd);
        return NET_ERR;
    }

    // 设置 TCP_KEEPINTVL（探测包间隔时间）
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) == -1)
    {
        log_error("set tcp keepintvl fd(%d) failed", fd);
        return NET_ERR;
    }

    // 设置 TCP_KEEPCNT（最大探测次数）
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt)) == -1)
    {
        log_error("set tcp keepcnt fd(%d) failed", fd);
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
int anetTcpServer(int port, char *bindaddr, int backlog)
{
    int sockfd, rv;
    char _port[6]; // 端口号最大65535
    struct addrinfo hints, *servinfo, *p;

    snprintf(_port, sizeof(_port), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE;     // 用于bind

    rv = getaddrinfo(bindaddr, _port, &hints, &servinfo);
    if (rv != 0)
    {
        log_error("get addrinfo failed, %s", gai_strerror(rv));
        return NET_ERR;
    }

    // 根据返回的地址列表，尝试创建socket
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1)
        {
            continue;
        }
        anetSetReuseAddr(sockfd);
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {
            log_debug("TRY bind() failed%s", strerror(errno));
            close(sockfd);
            continue;
        }
        break;
    }
    if (p == NULL)
    {
        log_error("can't bind");
        return NET_ERR;
    }
    freeaddrinfo(servinfo);
    if (listen(sockfd, backlog) == -1)
    {
        log_error("listen: %s", strerror(errno));
        return NET_ERR;
    }
    log_debug("listening on port: %d addr: %s ,listen-fd: %d", port, bindaddr, sockfd);
    return sockfd;
}

/**
 * @brief 获取客户fd的 ip和host
 *
 * @param [in] fd
 * @param [out] ip
 * @param [in] ip_len
 * @param [out] port
 * @return int [NET_OK, NET_ERR]
 */
static int anetFormatPeer(int fd, char *ip, size_t ip_len, int *port)
{
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    if (getpeername(fd, (struct sockaddr *)&sa, &salen) == -1)
    {
        return NET_ERR;
    }
    if (sa.ss_family == AF_INET)
    {
        struct sockaddr_in *s = (struct sockaddr_in *)&sa;
        inet_ntop(AF_INET, &s->sin_addr, ip, ip_len);
        *port = ntohs(s->sin_port);
    }
    else
    {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)&sa;
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
void acceptTcpHandler(aeEventLoop *el, int fd, void *data)
{
    int cfd, port;
    char ip[128];
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    cfd = accept(fd, (struct sockaddr *)&sa, &salen);
    if (cfd == -1)
    {
        return;
    }

    anetNonBlock(cfd);
    anetEnableTcpNoDelay(cfd);
    anetKeepAlive(cfd, 300);
    anetFormatPeer(cfd, ip, sizeof(ip), &port);
    // 创建client实例
    redisClient *client = redisClientCreate(cfd, ip, port);
    listAddNodeTail(server->clients, listCreateNode(client));

    log_debug("Accepted connection from [%d]%s:%d。 Current all connections %d", cfd, ip, port, listLength(server->clients));
    // 注册读事件
    aeCreateFileEvent(el, cfd, AE_READABLE, readFromClient, client);
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
int anetTcpConnect(const char *host, int port)
{
    int sockfd = -1;
    char portStr[6];
    struct addrinfo hints, *servinfo, *p;
    int ret;

    log_debug("anetconnect %s:%d", host, port);

    snprintf(portStr, sizeof(portStr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((ret = getaddrinfo(host, portStr, &hints, &servinfo)) != 0)
    {
        log_error(stderr, "getaddrinfo error: %s", gai_strerror(ret));
        return NET_ERR;
    }
    printAddrinfo(servinfo);
    // 遍历返回的地址列表，尝试创建 socket 并连接
    for (p = servinfo; p != NULL; p = p->ai_next)
    {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1)
        {
            continue; // 创建 socket 失败，尝试下一个地址
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
        {

            perror("connect failed fd ");
            close(sockfd);
            continue; // 连接失败，尝试下一个地址
        }
        break; // 连接成功，退出循环
    }

    freeaddrinfo(servinfo); // 释放 `servinfo`

    if (p == NULL)
    { // 遍历所有地址仍然连接失败
        log_error(stderr, "Failed to connect to %s:%d", host, port);
        return NET_ERR;
    }

    return sockfd; // 返回 socket 描述符
}


/**
 * @brief 获取buf内RESP的长度， buf以RESP开头
 *
 * @param [in] buf
 * @param [in] len buf长度
 * @return ssize_t
 */
ssize_t getRespLength(const char *buf, size_t len)
{
    if (len < 2)
        return -1;

    char type = buf[0];
    size_t i;

    switch (type)
    {
    case '+': // 简单字符串
    case '-': // 错误
    case ':': // 整数
        for (i = 1; i < len - 1; i++)
        {
            if (buf[i] == '\r' && buf[i + 1] == '\n')
            {
                return i + 2;
            }
        }
        return -1;

    case '$': // 批量字符串， $10\r\nfoofoofoob\r\n
        if (len < 3)
            return -1;
        for (i = 1; i < len - 1; i++)
        {
            // 遍历找到第一个\r\n， 即length，然后根据i+length计算完整长度
            if (buf[i] == '\r' && buf[i + 1] == '\n')
            {
                char len_buf[32];
                size_t prefix_len = i + 2;
                strncpy(len_buf, buf + 1, i - 1);
                len_buf[i - 1] = '\0';
                int data_len = atoi(len_buf);
                if (data_len <= 0)
                    return prefix_len; // $-1\r\n 返回5
                if (len < prefix_len + data_len + 2)
                    return -1; // 数据部分不完整
                if (buf[prefix_len + data_len] == '\r' && buf[prefix_len + data_len + 1] == '\n')
                {
                    return prefix_len + data_len + 2; // 包括数据和末尾 \r\n
                }
                return -1; // 末尾无 \r\n
            }
        }
        return -1;

    case '*': // 数组 *2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
        if (len < 3)
            return -1;
        for (i = 1; i < len - 1; i++)
        {
            // 遍历找到第一个\r\n, 即数组大小，
            if (buf[i] == '\r' && buf[i + 1] == '\n')
            {
                char len_buf[32];
                size_t prefix_len = i + 2;
                strncpy(len_buf, buf + 1, i - 1);
                len_buf[i - 1] = '\0';
                int num_elements = atoi(len_buf);
                if (num_elements <= 0)
                    return prefix_len; // *-1\r\n 返回5，前缀长度
                size_t offset = prefix_len;
                for (int j = 0; j < num_elements; j++)
                {
                    if (offset >= len)
                        return -1; // 如果大于buf的len，不完整
                    ssize_t elem_len = getRespLength(buf + offset, len - offset);
                    if (elem_len == -1)
                        return -1; // 获取数组各元素
                    offset += elem_len;
                }
                return offset;
            }
        }
        return -1;

    default:
        return -1;
    }
}

/**
 * @brief 中断当前master连接，重新连接
 * 
 */
void reconnectMaster()
{
    freeClient(server->master);    
    connectMaster();
}

/**
 * @brief 连接主，切换到CONNECTING
 *
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
    int err = 0;
    server->master = redisClientCreate(fd, server->masterhost, server->masterport);
    server->master->flags = REDIS_CLIENT_MASTER;
    server->replState = REPL_STATE_SLAVE_CONNECTING;
    server->repltimeout = REPL_TIMEOUT;
    // 不能调换顺序。 epoll一个fd必须先read然后write， 否则epoll_wait监听不到就绪。
    aeCreateFileEvent(server->eventLoop, fd, AE_READABLE, repliReadHandler, server->master);
    aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, server->master);

    log_debug("Connecting Master fd %d", fd);

    server->master->lastinteraction =server->unixtime;
    // 开启心跳检测
    aeCreateTimeEvent(server->eventLoop, 1000, replicationCron, NULL);
}


/**
 * @brief 打印sock上的err
 * 
 * @param [in] sockfd 
 */
void checkSockErr(int sockfd)
{
    int err;
    socklen_t len = sizeof(err);
    // 查看错误状态
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &len) == 0) { 
        // getsockopt 成功了，再看 err
        if (err == 0) {
            log_debug("Socket [%d] is OK, no error.", sockfd);
        } else {
            log_error("Socket [%d] error:  %s", sockfd, strerror(err));  // 打印错误信息
        }
    } else {
        log_error("Socket getsockopt failed: [%d] %s", sockfd,strerror(errno));
    }
}

