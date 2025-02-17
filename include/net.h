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
int anetTcpServer(char *err, int port, char *bindaddr, int backlog);

// 封装accept，接受TCP连接。服务器初始化时候，会将该处理程序与AE_READABLE关联。创建client实例，注册事件。
void acceptTcpHandler(aeEventLoop *el, int fd, void* privData);
// 封装read。
void readQueryFromClient(aeEventLoop *el, int fd, void* privData);
// 封装write
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata);

int anetNonBlock(char *err, int fd);
int anetEnableTcpNoDelay(char *err, int fd);
int anetKeepAlive(char *err, int fd, int interval);
int anetFormatPeer(int fd, char *ip, size_t ip_len, int *port);
// 无buf缓存延迟，直接fd发送
int anetIOWrite(int fd, char *buf, int len);
int anetTcpConnect(char* err, const char* host, int port);
void connectMaster();

#endif

