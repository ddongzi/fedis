
#ifndef CLIENT_H
#define CLIENT_H

// 类似枚举，client只能有一个角色
#define CLIENT_ROLE_NORMAL 0
#define CLIENT_ROLE_MASTER 1
#define CLIENT_ROLE_SLAVE 2
#define CLIENT_ROLE_SENTINEL 3 // role后面表示对方是什么

#define CLIENT_IS_NORMAL(c)     ((c)->flags == CLIENT_ROLE_NORMAL)
#define CLIENT_IS_MASTER(c)     ((c)->flags == CLIENT_ROLE_MASTER)
#define CLIENT_IS_SLAVE(c)      ((c)->flags == CLIENT_ROLE_SLAVE)
#define CLIENT_IS_SENTINEL(c)   ((c)->flags == CLIENT_ROLE_SENTINEL)


#include "sds.h"
#include "db.h"
#include "robj.h"
#include "net.h"
#include "connection.h"
typedef struct Client Client;
/**
 * @brief // TODO 改名
 * 
 * @struct client
 * @brief  client 对端状态结构，通用，1. 偏向于io, 2. resp解析响应
 * 
 */
struct Client {
    Connection* conn;
    int flags;  ///< 通过flags找到具体对应的实例状态。 可能不需要
    sds* readBuf;
    sds* writeBuf;
    int dbid;   // TODO 这里sentinel不涉及的。
    redisDb* db;
    int argc;   ///< 参数个数
    robj** argv;    ///< 参数数组
    void* instance; ///< 具体各类实例
} ;
Client *clientCreate(Connection* conn);

void addWrite(Client* client, robj* obj) ;
void readToReadBuf(Client* client) ;
#endif
