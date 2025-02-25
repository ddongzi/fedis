#ifndef AE_H
#define AE_H

#include <time.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <errno.h>
#include <string.h>

// 自定义事件类型
#define AE_ALL_EVENTS 0
#define AE_FILE_EVENTS 1
#define AE_TIME_EVENTS 2

// 自定义事件监听类型
#define AE_NONE 0   // 套接字上无事件监听。
#define AE_READABLE 1
#define AE_WRITABLE 2

#define AE_ERROR    -1
#define AE_OK       0

struct aeEventLoop;

typedef void aeFileProc(struct aeEventLoop *eventLoop, int fd, void *clientData);
// 时间事件处理函数返回
#define AE_NOMORE -1
typedef int aeTimeProc(struct aeEventLoop* eventLoop, long long id, void* clientData);
// 文件事件
typedef struct aeFileEvent {
    int mask;   // AE_READABLE，AE_WRITABLE
    aeFileProc* rfileProc;   // 事件读处理程序
    aeFileProc* wfileProc;  // 事件写处理程序
    void* data; // 事件处理程序参数
} aeFileEvent;

// 触发事件
typedef struct aeFireEvent {
    int fd;
    int mask;   // 标记events[fd]触发上触发的事件类型。
} aeFireEvent;

// 维护epoll状态 
typedef struct aeApiState {
    int epfd;   // epoll fd
    struct epoll_event *events; // epoll_wait返回的事件数组
} aeApiState;

// 时间事件处理函数
typedef struct aeTimeEvent {
    long long id;   // 时间事件id
    long long when;  //  秒
    long long when_ms;   // 毫秒
    aeTimeProc* timeProc;   // 时间事件处理函数
    void* data;  // 时间事件处理函数参数
    struct aeTimeEvent* next;   // 下一个时间事件
} aeTimeEvent;



// 事件循环
typedef struct aeEventLoop {
    int maxfd;  // 目前最大注册fd
    int maxsize;    // 支持的最大fd-1。即events数组大小。
    aeFileEvent* events;    // 已注册事件数组。events[0]对应fd为0.
    aeFireEvent* fireEvents;    // 触发的事件队列。
    aeApiState* apiState;   //  对应的epoll事件。 上述为封装。
    int stop;   // 事件循环停止标志

    aeTimeEvent* timeEventHead; // 时间事件链表头
    long long timeEventNextId;  // 下一个时间事件id

} aeEventLoop;




// 事件循环初始化,
aeEventLoop *aeCreateEventLoop(int maxsize);
// 创建一个事件，注册事件到事件循环，添加到IO复用监听。
int aeCreateFileEvent(aeEventLoop* loop, int fd, int mask, aeFileProc *proc, void* procArg);
// 从注册事件中删除，取消IO复用监听。
int aeDeleteFileEvent(aeEventLoop* loop, int fd, int mask);
// 调用IO复用底层阻塞（比如select)，阻塞所有注册事件，有ready或者超时就返回。
int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp);
// 文件事件派发：首先调用apipoll等待事件产生，更新fireevent，调用相应回调函数处理
int aeProcessEvents(aeEventLoop* loop, int flags);
// 为fd注册事件
int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask);

int aeCreateTimeEvent(aeEventLoop* loop, long long when, aeTimeProc* proc, void* procArg);

void aeMain(aeEventLoop* eventLoop);
#endif
