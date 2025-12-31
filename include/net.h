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

#define IP_ADDR_MAX 64

#define NET_BUF_MAX_SIZE 1024

#define NET_OK 0
#define NET_ERR -1
int anetTcpServer(int port, char *bindaddr, int backlog);

// 封装accept，接受TCP连接。服务器初始化时候，会将该处理程序与AE_READABLE关联。创建client实例，注册事件。
void acceptTcpHandler(aeEventLoop *el, int fd, void* privData);
// 封装read。
void readFromClient(aeEventLoop *el, int fd, void* privData);
// 封装write
void sendToClient(aeEventLoop *el, int fd, void *privdata);

int anetTcpConnect( const char* host, int port);
void connectMaster();
ssize_t getRespLength(const char* buf, size_t len) ;
char * respFormat(int argc, char** argv);


char* respEncodeArrayString(int argc, char* argv[]);
int resp_decode(const char *resp, int *argc_out, char** argv_out[]);
bool checkSockErr(int sockfd);
bool checkSockRead(redisClient* c, int nread);
void reconnectMaster();

#endif