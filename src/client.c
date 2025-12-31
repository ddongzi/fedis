#include <stdlib.h>
#include "client.h"
#include "errno.h"
#include "redis.h"
#include "log.h"
#include "net.h"
#include <string.h>
#include <unistd.h>

/**
 * 伪客户端，没有网络连接，但可以执行所有命令.
 * 但是不能注册ae，所以接受不到任何回复。
 * @return
 */
redisClient* redisFakeClientCreate()
{
    redisClient *c = malloc(sizeof(redisClient));
    c->fd = -1;
    c->flags = REDIS_CLIENT_FAKE;
    c->readBuf = sdsempty();
    c->writeBuf = sdsempty();
    c->dbid = 0;
    c->db = &server->db[c->dbid];
    c->argc = 0;
    c->argv = NULL;
    c->ip = NULL;
    c->port = -1;
    c->name = calloc(1, CLIENT_NAME_MAX);
    c->toclose = 0;
    return c;
}

redisClient *redisClientCreate(int fd, char* ip, int port)
{
    redisClient *c = malloc(sizeof(redisClient));
    c->fd = fd;
    c->flags = REDIS_CLIENT_NORMAL;
    c->readBuf = sdsempty();
    c->writeBuf = sdsempty();
    c->dbid = 0;
    c->db = &server->db[c->dbid];
    c->argc = 0;
    c->argv = NULL;
    c->ip = calloc(1, IP_ADDR_MAX);
    strcpy(c->ip, ip);
    c->port = port;
    c->name = calloc(1, CLIENT_NAME_MAX);
    c->toclose = 0;
    c->multiCmdCount = 0;
    c->multcmds = malloc(sizeof(sds* ) * 10);
    for (int i = 0; i < 10; ++i)
    {
        c->multcmds[i] = sdsempty();
    }
    log_debug("create client %s:%d, fd %d", c->ip, c->port, c->fd);
    return c;
}
/**
 * @param [in] client
 * @param [in] 为resp字符串
 */
void addWrite(redisClient* client, char* s)
{
    sdscat(client->writeBuf, s);
}
/**
 * @brief 设置client待关闭位。取消epoll
 * 
 * @param [in] c 
 */
void clientToclose(redisClient* c)
{
    if (c->flags & CLIENT_TO_CLOSE)
        return;
    c->flags |= CLIENT_TO_CLOSE;

    // 立即取消fd监听
    aeDeleteFileEvent(server->eventLoop, c->fd, AE_WRITABLE);
    aeDeleteFileEvent(server->eventLoop, c->fd, AE_READABLE);

    // 可以考虑不动链表，只是表示client将要关闭。因为外部可能正在遍历。 而是先标记，然后一起close定时
}
/**
 * @brief 释放client, 不能直接调用，   除非需要立马清除如重连。
 * 
 * @param [in] client 
 */
void freeClient(redisClient* client)
{
    if (!client)
        return;
    log_debug("free client %d", client->fd);
    // 确保epoll fd释放
    aeDeleteFileEvent(server->eventLoop, client->fd, AE_READABLE);
    aeDeleteFileEvent(server->eventLoop, client->fd, AE_WRITABLE);
    close(client->fd);

    sdsfree(client->readBuf);
    sdsfree(client->writeBuf);
    free(client);
}

/**
 * @brief read接口， 读取到client->readbuf, 两种情况：纯RESP, RESP+数据流。 只读取RESP部分。
 * @param [in] client 
 * @return 
 * @deprecated 使用sdscatlen就行！
 */
void readToReadBuf(redisClient* client) 
{
    // todo有问题 ！！
    char temp_buf[1]; // 逐字节读取，确保精确性
    ssize_t n;

    sdsclear(client->readBuf);
    log_debug(" 0000000011 %d", sdslen(server->master->writeBuf));

    printf("\n to read buf\n");
    while (1) {
        // 没必要RIO，
        n = read(client->fd, temp_buf, sizeof(temp_buf));
        log_debug(" 0000000021 %d", sdslen(server->master->writeBuf));

        if (n <= 0) {
            log_error("read failed or finished %s", strerror(errno));
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            return;
        }
        sdscatlen(client->readBuf, temp_buf, n);
        log_debug(" 0000000031 %d", sdslen(server->master->writeBuf));

        ssize_t resp_len = getRespLength(client->readBuf->buf, sdslen(client->readBuf));
        log_debug(" 0000000041 %d", sdslen(server->master->writeBuf));
        
        if (resp_len != -1) {
            // 读到一个RESP协议
            printf("get resp return");
            break;
        }
    }
    printf("\nread buf finished,  %s\n", client->readBuf->buf);
}
void clientMultiAdd(redisClient* c)
{
    sds* cmd = c->multcmds[c->multiCmdCount];
    sdsclear(cmd);
    sdscatsds(cmd, c->readBuf);
    c->multiCmdCount++;
}
