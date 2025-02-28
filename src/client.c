#include "client.h"
#include <stdlib.h>
#include "errno.h"
#include "redis.h"
#include "log.h"
#include "net.h"
redisClient *redisClientCreate(int fd)
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
 * @brief read接口， 读取到client->readbuf, 两种情况：纯RESP, RESP+数据流。 只读取RESP部分。
 * @param [in] client 
 * @return 
 */
void readToReadBuf(redisClient* client) 
{
    char temp_buf[1]; // 逐字节读取，确保精确性
    ssize_t n;

    sdsclear(client->readBuf);

    printf("\n to read buf\n");
    while (1) {
        // 没必要RIO，
        n = read(client->fd, temp_buf, sizeof(temp_buf));
        if (n <= 0) {
            log_debug("read failed or finished");
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
            return;
        }
        sdscatlen(client->readBuf, temp_buf, n);

        ssize_t resp_len = getRespLength(client->readBuf->buf, sdslen(client->readBuf));
        if (resp_len != -1) {
            // 读到一个RESP协议
            printf("get resp return");
            break;
        }
    }
    printf("\nread buf finished,  %s\n", client->readBuf->buf);
}