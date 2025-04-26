
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
        log_debug("Address: %s, Port: %d\n", ip, port);
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
            log_debug("TRY bind() failed%s\n", strerror(errno));
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
    log_debug("listening on port: %d addr: %s ,listen-fd: %d\n", port, bindaddr, sockfd);
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

    log_debug("Accepted connection from %s:%d,  fd %d\n", ip, port, cfd);

    // 注册读事件
    aeCreateFileEvent(el, cfd, AE_READABLE, readQueryFromClient, client);
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

    log_debug("anetconnect %s:%d\n", host, port);

    snprintf(portStr, sizeof(portStr), "%d", port);
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((ret = getaddrinfo(host, portStr, &hints, &servinfo)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(ret));
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
        fprintf(stderr, "Failed to connect to %s:%d\n", host, port);
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
 * @brief 收到SLAVEOF命令，开始连接master，切换到CONNECTING
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
    server->replState = REPL_STATE_SLAVE_CONNECTING;
    log_debug("Connecting Master fd %d", fd);
    // 不能调换顺序。 epoll一个fd必须先read然后write， 否则epoll_wait监听不到就绪。
    aeCreateFileEvent(server->eventLoop, fd, AE_READABLE, repliReadHandler, server->master);
    aeCreateFileEvent(server->eventLoop, fd, AE_WRITABLE, repliWriteHandler, server->master);
}


/**
 * @brief resp格式内容转为字符串空格分割。 常用于打印RESP，RESP必须完整严格
 * 
 * @param [in] resp 
 * @return char* 
 */
char* respParse(const char* resp) {
    if (!resp) return NULL;
    
    char type = resp[0];
    const char* data = resp + 1;
    char* result = NULL;
    
    switch (type) {
        case '+':  // Simple Strings
        case '-':  // Errors
            result = strdup(data);
            result[strcspn(result, "\r\n")] = 0; // 去掉结尾的 \r\n
            break;
        case ':':  // Integers
        //  linux
            asprintf(&result, "%ld", strtol(data, NULL, 10));
            break;
        case '$': { // Bulk Strings
            int len = strtol(data, NULL, 10);
            if (len == -1) {
                result = strdup("(nil)");
            } else {
                const char* str = strchr(data, '\n');
                if (str) {
                    result = strndup(str + 1, len);
                }
            }
            break;
        }
        case '*': { // Arrays
            int count = strtol(data, NULL, 10);
            if (count == -1) {
                result = strdup("(empty array)");
            } else {
                result = malloc(1024);  // 假设最大长度不会超
                result[0] = '\0';
                const char* ptr = strchr(data, '\n') + 1;
                for (int i = 0; i < count; i++) {
                    char* elem = respParse(ptr);
                    strcat(result, elem);
                    strcat(result, " ");
                    free(elem);
                    
                    // 移动 ptr 指向下一个 RESP 片段
                    if (*ptr == '+' || *ptr == '-' || *ptr == ':') {
                        ptr = strchr(ptr, '\n') + 1;
                    } else if (*ptr == '$') {
                        int blen = strtol(ptr + 1, NULL, 10);
                        if (blen != -1) {
                            ptr = strchr(ptr, '\n') + 1 + blen + 2;
                        } else {
                            ptr = strchr(ptr, '\n') + 1;
                        }
                    }
                }
            }
            break;
        }
        default:
            result = strdup("(unknown)");
            break;
    }
    return result;
}


/**
 * @brief 构造resp格式
 * 
 * @param [in] argc 
 * @param [in] argv 
 * @return char* 
 */
char* respFormat(int argc, char** argv)
{
    // 计算 RESP 总长度
    size_t total_len = 0;
    for (int i = 0; i < argc; i++) {
        total_len += snprintf(NULL, 0, "$%zu\r\n%s\r\n", strlen(argv[i]), argv[i]);
    }
    total_len += snprintf(NULL, 0, "*%d\r\n", argc);

    // 分配 RESP 命令的字符串
    char *resp_cmd = (char *)malloc(total_len + 1);
    if (!resp_cmd) {
        return NULL;
    }

    // 构造 RESP 字符串
    char *ptr = resp_cmd;
    ptr += sprintf(ptr, "*%d\r\n", argc);
    for (int i = 0; i < argc; i++) {
        ptr += sprintf(ptr, "$%zu\r\n%s\r\n", strlen(argv[i]), argv[i]);
    }

    return resp_cmd;
}

/**
 * @brief 编码：argv[] -> RESP 格式字符串
 * 
 * @param [in] argc 
 * @param [in] argv 
 * @return char* 
 */
char* resp_encode(int argc, char* argv[])
 {
    size_t cap = 1024;
    char *buf = malloc(cap);
    size_t len = 0;

    len += snprintf(buf + len, cap - len, "*%d\r\n", argc);
    for (int i = 0; i < argc; ++i) {
        int arglen = strlen(argv[i]);
        len += snprintf(buf + len, cap - len, "$%d\r\n", arglen);
        if (len + arglen + 2 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        memcpy(buf + len, argv[i], arglen);
        len += arglen;
        memcpy(buf + len, "\r\n", 2);
        len += 2;
    }
    buf[len] = '\0';
    return buf;
}
/**
 * @brief 从resp字符串解析
 * 
 * @param [in] resp 
 * @param [out] argc_out 
 * @param [out] argv_out 
 * @return int 
 */
int resp_decode(const char *resp, int *argc_out, char** argv_out[]) {
    if (*resp != '*') return -1;
    int argc;
    sscanf(resp + 1, "%d", &argc);
    *argc_out = argc;
    *argv_out = malloc(sizeof(char*) * argc);

    const char *p = strchr(resp, '\n') + 1;
    for (int i = 0; i < argc; ++i) {
        if (*p != '$') return -1;
        int len;
        sscanf(p + 1, "%d", &len);
        p = strchr(p, '\n') + 1;

        (*argv_out)[i] = malloc(len + 1);
        memcpy((*argv_out)[i], p, len);
        (*argv_out)[i][len] = '\0';
        p += len + 2; // skip "\r\n"
    }
    return 0;
}