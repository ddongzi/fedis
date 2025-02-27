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

#define NET_OK 0
#define NET_ERR -1
int anetTcpServer(int port, char *bindaddr, int backlog);

// 封装accept，接受TCP连接。服务器初始化时候，会将该处理程序与AE_READABLE关联。创建client实例，注册事件。
void acceptTcpHandler(aeEventLoop *el, int fd, void* privData);
// 封装read。
void readQueryFromClient(aeEventLoop *el, int fd, void* privData);
// 封装write
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata);

int anetNonBlock(int fd);
int anetEnableTcpNoDelay(int fd);
int anetKeepAlive(int fd, int interval);
int anetFormatPeer(int fd, char *ip, size_t ip_len, int *port);
int anetTcpConnect( const char* host, int port);
void connectMaster();
ssize_t getRespLength(const char* buf, size_t len) ;
#endif

