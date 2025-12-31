/**
 * @file ae.c
 * @author ddongzi
 * @brief eventloop, epoll 
 * @version 0.1
 * @date 2025-02-17
 * 
 * 
 * @note 
 *  1. 为什么读事件不需要删除，写事件需要删除？
 *      读事件由对端触发，只有对端发来东西可触发。
 *      写事件如果不删除，那么在可写状态下，一直会调用写处理函数。 所以写处理，写完就会关闭。 想写就需要显示开启
 */



#include "ae.h"
#include <stdlib.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "log.h"
#include <poll.h>
#include <sys/socket.h>

#include "redis.h"
#include "net.h"
#include "aof.h"
/**
 * @brief 初始化apistate
 * 
 * @param [in] eventLoop 
 * @return int 
 */
static int aeApiCreate(aeEventLoop* eventLoop)
{
    aeApiState* apiState = malloc(sizeof(aeApiState));
    if (apiState == NULL) {
        return AE_ERROR;
    }
    apiState->epfd = epoll_create1(0);
    if (apiState->epfd == -1) {
        free(apiState);
        return AE_ERROR;
    }
    apiState->events =calloc(eventLoop->maxsize, sizeof(struct epoll_event));
    if (apiState->events == NULL) {
        free(apiState);
        return AE_ERROR;
    }
    // ET 模式

    eventLoop->apiState = apiState;
    // log_debug("Create epoll instance. epfd = %d", apiState->epfd);
    return AE_OK;
}
/**
 * @brief 
 * 
 * @param [out] seconds 
 * @param [out] milliseconds 
 * @return int 
 */
static int aeGetTime(long long* seconds, long long* milliseconds)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);    
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec / 1000;
    return AE_OK;
}

/**
 * @brief epoll_wait, 触发事件添加到fireEvents
 * 
 * @param [in] eventLoop 
 * @param [in] tvp null表示 一直等待，
 * @return int numevents
 * @note 
 */
int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp)
{
    int numevents;
    // log_debug("epoll wait ... time: %d ms", tvp ? (tvp->tv_sec * 1000 + tvp->tv_usec / 1000) : -1);
    
    // if (server->replState == REPL_STATE_SLAVE__NONE) {
    //     log_debug("checking master fd !!");
    // }
    // timeout=0 : 立即返回，返回当前可就绪的， timeout=-1:一直等待
    numevents = epoll_wait(eventLoop->apiState->epfd, 
        eventLoop->apiState->events, 
        eventLoop->maxsize, 
        tvp ? (tvp->tv_sec * 1000 + (tvp->tv_usec + 999)/1000): -1
        );

    if (numevents == -1) {
        if (errno == EINTR) {
            // 如果被中断了， 就返回把
            return numevents;
        } else
        {
            log_error("Unexpected epoll_wait error. %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    for (int i = 0; i < numevents; i++) {
        struct epoll_event* e = &eventLoop->apiState->events[i];
        int mask = 0;
        if (e->events & EPOLLIN) {
            mask |= AE_READABLE;
        }
        if (e->events & EPOLLOUT) {
            mask |= AE_WRITABLE;
        }
        if (e->events & EPOLLERR) {
            checkSockErr(e->data.fd);
        }

        eventLoop->fireEvents[i].fd = e->data.fd;
        eventLoop->fireEvents[i].mask = mask;
        // log_debug("epoll fd[%d] event[%s] come.", e->data.fd, mask == AE_WRITABLE ? "WRITABLE" : "READ");
    }
    return numevents;
}


/**
 * @brief 找到最早到期的时间事件
 * 
 * @param [in] loop 
 * @return aeTimeEvent* 
 */
static aeTimeEvent* aeSearchNearestTimer(aeEventLoop* loop)
{
    aeTimeEvent* te = loop->timeEventHead;
    aeTimeEvent* nearest = NULL;
    while (te) {
        if (nearest == NULL || te->when < nearest->when) {
            nearest = te;
        }
        te = te->next;
    }
    return nearest;
}


aeEventLoop *aeCreateEventLoop(int maxsize)
{
    aeEventLoop* eventLoop;
    eventLoop = malloc(sizeof(aeEventLoop));
    // 自维持事件
    eventLoop->maxsize = maxsize;
    eventLoop->events = calloc(maxsize, sizeof(aeFileEvent) );
    eventLoop->fireEvents = calloc(maxsize, sizeof(aeFileEvent));
    for (int i = 0; i < maxsize; i++) {
        eventLoop->events[i].data = NULL;
        eventLoop->events[i].mask = AE_NONE;    // 初始不监听i 
        eventLoop->events[i].rfileProc = NULL;
        eventLoop->events[i].wfileProc = NULL;
    }
    memcpy(eventLoop->fireEvents, eventLoop->events, maxsize * sizeof(aeFileEvent));

    // epoll事件维持
    if (aeApiCreate(eventLoop) == AE_ERROR) {
        return NULL;
    }
    eventLoop->stop = 0;
    return eventLoop;
}

/**
 * @brief 为fd注册读写事件。
 * 
 * @param [in] loop 
 * @param [in] fd 
 * @param [in] mask ：[AE_READABLE, AE_WRITABLE] 只能一种
 * @param [in] proc : aeFileProc
 * @param [in] data 
 * @return int : [AE_OK, AE_ERROR] 如果AE_ERROR 应该释放fd资源
 */
int aeCreateFileEvent(aeEventLoop* loop, int fd, int mask, aeFileProc *proc, void* data)
{
    // 很可能连接已经失效/不可用等，所以必须处理
    if (!checkSockErr(fd))
    {
        return AE_ERROR;
    }

    if (fd >= loop->maxsize || fd < 0) {
        return AE_ERROR;
    }
    aeFileEvent* fe = &loop->events[fd];
    
    // IO复用监听注册
    if (aeApiAddEvent(loop, fd, mask) == AE_ERROR) {

        return AE_ERROR;
    }

    fe->mask |= mask;
    if (mask & AE_READABLE) {
        fe->rfileProc = proc;
    }
    if (mask & AE_WRITABLE) {
        fe->wfileProc = proc;
    }
    fe->data = data;
    if (fd > loop->maxfd) {
        loop->maxfd = fd;
    }
    // log_debug("create %s event for fd %d.", mask == AE_WRITABLE ? "WRITE": "READ", fd);
    return AE_OK;
}
/**
 * @brief 修改/添加 epoll事件
 * 
 * @param [in] eventLoop 
 * @param [in] fd 
 * @param [in] mask :[AE_READABLE, AE_WRITABLE, AE_NONE]
 * @return int 
 */
int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    struct epoll_event ee;
    ee.data.fd = fd;
    ee.events = 0;

    if (mask == AE_NONE) {
        return AE_OK;
    }
    if (mask & AE_READABLE) {
        ee.events |= EPOLLIN;
    }
    if (mask & AE_WRITABLE) {
        ee.events |= EPOLLOUT;
    }
    int op = eventLoop->events[fd].mask == AE_NONE ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    if (epoll_ctl(eventLoop->apiState->epfd, op, fd, &ee) == -1) {
        checkSockErr(fd);
        return AE_ERROR;
    }
    // log_debug("EPOLL CTL fd:%d, OP :%s, mask:%s", fd, op == EPOLL_CTL_ADD ? "add" : "mod" 
    //     , mask == AE_WRITABLE ? "write" : "read");
    return AE_OK;
}


/**
 * @brief 对当前时间增加milliseconds毫秒，得到未来一个时间。
 * 
 * @param [in] milliseconds 要增加的毫秒数
 * @param [in] sec 返回
 * @param [in] ms 返回
 * @return int 
 */
int aeAddMillisecondsToNow(long long milliseconds, long long* sec, long long* ms)
{
    long long cur_sec, cur_ms, when_sec, when_ms;
    aeGetTime(&cur_sec, &cur_ms);
    when_sec = cur_sec + milliseconds / 1000;
    when_ms = cur_ms + milliseconds % 1000;
    if (when_ms >= 1000) {
        when_sec++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
    return AE_OK;
}

/**
 * @brief 从时间事件链表中删除id的时间事件
 * 
 * @param [in] loop 
 * @param [in] id 
 * @return int 
 */
int aeDeleteTimeEvent(aeEventLoop* loop, long long id)
{
    aeTimeEvent* te, *prev = NULL;
    te = loop->timeEventHead;
    while (te) {
        if (te->id == id) {
            if (prev == NULL) {
                loop->timeEventHead = te->next;
            } else {
                prev->next = te->next;
            }
            free(te);
            return AE_OK;
        }
        prev = te;
        te = te->next;
    }
    return AE_ERROR;
}

static int processTimeEvents(aeEventLoop* eventLoop)
{
    aeTimeEvent* te;
    long long now_sec, now_ms;

    te = eventLoop->timeEventHead;
    // TODO 
    while (te) {
        aeGetTime(&now_sec, &now_ms);
        if (now_sec > te->when || (now_sec == te->when && now_ms >= te->when_ms)) {
            int retval;
            retval = te->timeProc(eventLoop, te->id, te->data);
            if (retval != AE_NOMORE) {
                // 为周期事件重新设置when
                aeAddMillisecondsToNow(retval, &te->when, &te->when_ms);
            } else {
                aeDeleteTimeEvent(eventLoop, te->id);
            }
            // 可能删除后，需要重新遍历
            te = eventLoop->timeEventHead;
        } else {

            te = te->next;
        }
    }
}

/**
 * @brief 先处理定时任务，epoll_wait只等到最近定时任务时间。
 * 
 * @param [in] loop 
 * @param [in] flags : [AE_TIME_EVENTS, AE_FILE_EVENTS, AE_ALL_EVENTS]
 * @return int 
 */
static int aeProcessEvents(aeEventLoop* loop, int flags)
{
    int numevents;
    struct timeval tv, *tvp, now;

    aeTimeEvent* shortest = NULL;

    if (flags & AE_TIME_EVENTS) {
        shortest = aeSearchNearestTimer(loop);
    }
    if (shortest) {
        tvp = &tv;
        long long nowSec, nowMs;
        aeGetTime(&nowSec, &nowMs);
        long long when_ms = shortest->when * 1000;
        long delay_ms = when_ms - (nowSec*1000 + nowMs);
        if (delay_ms < 0) delay_ms = 0;
        // log_debug("delay_ms %u, when_ms,%u, now_ms %u", delay_ms, when_ms, nowSec*1000 + nowMs);
        tvp->tv_sec = delay_ms / 1000;
        tvp->tv_usec = (delay_ms % 1000) * 1000;
        
    } else {
        tvp = NULL; // 没有时间任务，文件事件可以一直等待。
    }

    // log_debug("API POLL timeout %u ms", tvp->tv_sec * 1000 + tvp->tv_usec/1000);

    // 文件事件: 至多等到下一个定时任务
    // TODO while运行很快，很可能在ms级别之下，运行了很多次timeOut 0 .(忙查询)
    numevents = aeApiPoll(loop, tvp);
    for (int i = 0; i < numevents; i++) {
        aeFileEvent* fe = &loop->events[loop->fireEvents[i].fd];
        int mask = loop->fireEvents[i].mask;
        int fd = loop->fireEvents[i].fd;
        // log_debug("event come: fd %d, ready %s", fd, mask == AE_WRITABLE ? "WRITABLE" : "READABLE");
        if ((fe->mask & AE_READABLE) && (mask & AE_READABLE)) {
            fe->rfileProc(loop, fd, fe->data);
        }
        if ((fe->mask & AE_WRITABLE) && (mask & AE_WRITABLE)) {
            fe->wfileProc(loop, fd, fe->data);
        }
        
    }

    // 时间事件
    if (flags & AE_TIME_EVENTS) {
        processTimeEvents(loop);
    }

    // 文件事件的写命令会追加到aof_buf缓冲， 需要考虑这里是否将缓冲刷到aof
    flushAppendOnlyFile();

    return AE_OK;
}
/**
 * @brief 删除epoll上读或者写监听
 * 
 * @param [in] eventLoop 
 * @param [in] fd 
 * @param [in] mask : 现在的[AE_READABLE, AE_WRITABLE, AE_NONE]
 * @return int 
 */
int aeApiDelEvent(aeEventLoop *eventLoop, int fd, int mask)
{
    aeApiState* apiState = eventLoop->apiState;
    struct epoll_event ee;
    if (mask == AE_NONE) {
        log_debug("Del epoll fd [%d]", fd);
        epoll_ctl(apiState->epfd, EPOLL_CTL_DEL, fd, NULL);
        return AE_OK;
    }
    // 描述符上还有监听，修改
    ee.events = 0;
    if (mask & AE_READABLE) {
        ee.events |= EPOLLIN;
    }
    if (mask & AE_WRITABLE) {
        ee.events |= EPOLLOUT;
    }
    ee.data.fd = fd;
    epoll_ctl(apiState->epfd, EPOLL_CTL_MOD, fd, &ee);
    return AE_OK;
}

/**
 * @brief 删除fd上的mask事件监听，epoll取消
 * 
 * @param [in] loop 
 * @param [in] fd 
 * @param [in] mask : [AE_READABLE, AE_WRITABLE, AE_NONE]
 * @return int 
 */
int aeDeleteFileEvent(aeEventLoop* loop, int fd, int mask)
{
    if (fd >= loop->maxsize) {
        return AE_ERROR;
    }
    aeFileEvent* fe = &loop->events[fd];
    // fd上无监听，无需del
    if (fe->mask == AE_NONE) {
        return AE_OK;
    }
    fe->mask &= ~mask;
    // 如果del后为AE_NONE，更新maxfd
    if (fd == loop->maxfd && fe->mask == AE_NONE) {
        int j;
        for (j = loop->maxfd - 1; j >= 0; j--) {
            if (loop->events[j].mask != AE_NONE) {
                break;
            }
        }
        loop->maxfd = j;
    }
    // 从events里面删除

    // 更新apisate
    return aeApiDelEvent(loop, fd, fe->mask);
}
/**
 * @brief 创建一个时间事件,添加到ae
 * 
 * @param [in] loop 
 * @param [in] ms : 在ms毫秒后
 * @param [in] proc 
 * @param [in] data 
 * @return int 
 */
int aeCreateTimeEvent(aeEventLoop* loop, long long ms, aeTimeProc* proc, void* data)
{
    long long now_sec, now_ms;
    aeTimeEvent* te;
    te = malloc(sizeof(aeTimeEvent));
    if (te == NULL) {
        return AE_ERROR;
    }
    aeGetTime(&now_sec, &now_ms);
    te->id = loop->timeEventNextId++;
    te->when = now_sec;
    te->when_ms = now_ms;
    aeAddMillisecondsToNow(ms, &te->when, &te->when_ms);
    
    te->timeProc = proc;
    te->data = data;
    te->next = loop->timeEventHead;
    loop->timeEventHead = te;
    return AE_OK;
}




/**
 * @brief 事件循环main
 * 
 * @param [in] loop 
 */
void aeMain(aeEventLoop* loop)
{
    loop->stop = 0;
    while (!loop->stop) {
        aeProcessEvents(loop, AE_FILE_EVENTS | AE_TIME_EVENTS);
    }
}