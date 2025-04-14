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
#include "rio.h"


#define IP_MAX_STRLEN 16



#define NET_OK 0
#define NET_ERR -1
int anetTcpServer(int port, char *bindaddr, int backlog);

int anetAcceptTcp(int fd, char* cip, size_t iplen ,int* port);

// 封装read。
void readQueryFromClient(aeEventLoop *el, int fd, void* privData);
// 封装write
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata);

int anetTcpConnect( const char* host, int port);
void connectMaster();
ssize_t getRespLength(const char* buf, size_t len) ;
char * respFormat(int argc, char** argv);

int anetFormatPeer(int fd, char *ip, size_t ip_len, int *port);

connection* netCreateConnection(int cfd , const char* ip, const int port);
void netCloseConnection(connection *conn);

#endif

