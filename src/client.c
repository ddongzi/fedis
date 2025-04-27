#include <stdlib.h>
#include "client.h"
#include "errno.h"
#include "redis.h"
#include "log.h"
#include "net.h"
#include <string.h>
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
    log_debug("create client %s:%d, fd %d", c->ip, c->port, c->fd);
    return c;

}
/**
 * @brief 准备buf，向client写, 后续write类handler处理
 * 
 * @param [in] client 
 * @param [in] obj 
 */
void addWrite(redisClient* client, robj* obj) 
{
    char buf[128] = {0};
    switch (obj->type)
    {
    case REDIS_STRING:
        robjGetValStr(obj, buf, sizeof(buf));
        break;
    
    default:
        break;
    }

    sdscat(client->writeBuf, buf);
}
/**
 * @brief 加入close链表
 * 
 * @param [in] c 
 */
void clientToclose(redisClient* c)
{
    listNode* node;
    node = listSearchKey(server->clientsToClose, c);
    if (node) {
        // 已经在close链表了
        node = listSearchKey(server->clients, c);
        return;
    }
    // 第一次添加
    aeDeleteFileEvent(server->eventLoop, c->fd, AE_WRITABLE);
    aeDeleteFileEvent(server->eventLoop, c->fd, AE_READABLE);

    node = listSearchKey(server->clients, c);    
    assert(c);
    assert(node);

    // 加到clientstoclose
    listAddNodeTail(server->clientsToClose, listCreateNode(node->value));
    listDelNode(server->clients, node);

}
/**
 * @brief 释放client, 不能直接调用，   除非需要立马清除如重连。
 * 
 * @param [in] client 
 */
void freeClient(redisClient* client)
{
    log_debug("free client %d\n", client->fd);
    // 确保epoll fd释放
    aeDeleteFileEvent(server->eventLoop, client->fd, AE_READABLE);
    aeDeleteFileEvent(server->eventLoop, client->fd, AE_WRITABLE);
    close(client->fd);

    sdsfree(client->readBuf);
    sdsfree(client->writeBuf);
    // 正常情况，每次执行完命令argv就destroy了，临时
    if (client->argv) {
        for(int i = 0; i < client->argc; i++) {
            robjDestroy(client->argv[i]);
        }
    }
    free(client);
    client = NULL;
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