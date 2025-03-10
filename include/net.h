/**
 * @file net.h
 * @author your name (you@domain.com)
 * @brief 网络连接
 * @version 0.1
 * @date 2025-02-26
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef __NET_H__
#define __NET_H__

#include "ae.h"

#define INET_ADDRSTRLEN 16

// 临时结构， 不明确 client还是sentinelInstance， accept创建
typedef struct connection {
    int cfd;
    char ip[INET_ADDRSTRLEN];
    int port;
    rio* io;
} connection;


#define NET_OK 0
#define NET_ERR -1
int anetTcpServer(int port, char *bindaddr, int backlog);

// 封装accept，接受TCP连接。服务器初始化时候，会将该处理程序与AE_READABLE关联。创建client实例，注册事件。
void acceptTcpHandler(aeEventLoop *el, int fd, void* privData);
// 封装read。
void readQueryFromClient(aeEventLoop *el, int fd, void* privData);
// 封装write
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata);

int anetTcpConnect( const char* host, int port);
void connectMaster();
ssize_t getRespLength(const char* buf, size_t len) ;
char * respFormat(int argc, char** argv);


connection* netCreateConnection(int cfd , const char* ip, const int port);
void netCloseConnection(connection *conn);

#endif

