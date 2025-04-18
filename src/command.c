/**
 * @file command.c
 * @author your name (you@domain.com)
 * @brief 不同类型服务器可能会共用一些命令处理
 * @version 0.1
 * @date 2025-03-10
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "command.h"
#include "client.h"
#include "log.h"
#include "server.h"
#include "slave.h"



// command dictType
static unsigned long commandDictHashFunction(const void *key) {
    unsigned long hash = 5381;
    const char *str = key;
    while (*str) {
        hash = ((hash << 5) + hash) + *str; // hash * 33 + c
        str++;
    }
    return hash;
}
static int commandDictKeyCompare(void* privdata, const void* key1, const void* key2)
{
    return strcmp((char*)key1, (char*)key2);
}
static void commandDictKeyDestructor(void* privdata, void* key)
{
}
static void commandDictValDestructor(void* privdata, void* val)
{
    free((redisCommand*)val);
}
static void* commandDictKeyDup(void* privdata, const void* key)
{
    if (key == NULL) {
        return NULL;
    }
    size_t size = strlen((char*)key);
    char* res = malloc(size + 1);
    strcpy(res, key);
    return (void*)res;
}
static void* commandDictValDup(void* privdata, const void* obj)
{
    if (obj == NULL) {
        return NULL;
    }
    redisCommand* res = malloc(sizeof(redisCommand));
    memcpy(res, obj, sizeof(redisCommand));
    return (void*)res;
}

// 加载命令表
dictType commandDictType = {
    .hashFunction = commandDictHashFunction,
    .keyCompare = commandDictKeyCompare,
    .keyDup =  commandDictKeyDup,
    .valDup =  commandDictValDup,
    .keyDestructor = commandDictKeyDestructor,
    .valDestructor = commandDictValDestructor
};

// ?TODO 只在初始化用， 给server.commands

redisCommand commandsMasterTable[] = {
    {"SET", commandSetProc, -3},
    {"GET", commandGetProc, 2},
    {"DEL", commandDelProc, 2},
    {"OBJECT", commandObjectProc, 3},
    {"BYE", commandByeProc, 1},
    {"SLAVEOF", commandSlaveofProc, 3},
    {"REPLCONF", commandReplconfProc, 3},
    {"SYNC", commandSyncProc, 1},
    {"REPLACK", commandReplACKProc, 1},
    {"PING", commandPingMasterProc, 1},
};
redisCommand commandsSlaveTable[] = {
    {"GET", commandGetProc, 2},
    {"DEL", commandDelProc, 2},
    {"OBJECT", commandObjectProc, 3},
    {"BYE", commandByeProc, 1},
    {"REPLCONF", commandReplconfProc, 3},
    {"REPLACK", commandReplACKProc, 1},
    {"PING", commandPingSlaveProc, 1},
};
redisCommand commandsSentinelTable[] = {
    {"PING", commandPingSentinelProc, 1},
    {"SENTINEL", commandSentinelProc, -2},  // Sentinel 相关命令
    {"BYE", commandByeProc, 1},
};



void commandSetProc(Client* client)
{
    int retcode = dbAdd(client->db, client->argv[1], client->argv[2]);
    if (retcode == DICT_OK) {
        addWrite(client, shared.ok);
        server->dirty++;
    } else {
        addWrite(client, shared.err);
    }
    connSetWriteHandler(client->conn, sendReplyToClient);
}
void commandGetProc(Client* client)
{
    robj* res = (robj*)dbGet(client->db, client->argv[1]);
    if (res == NULL) { 
        addWrite(client, robjCreateStringObject("-ERR key not found"));
    } else {
        addWrite(client, res);
    }
    connSetWriteHandler(client->conn, sendReplyToClient);
}
void commandDelProc(Client* client)
{
    int retcode = dbDelete(client->db, client->argv[1]);
    if (retcode == DICT_OK) {
        server->dirty++;

        addWrite(client, shared.ok);
    } else {
        addWrite(client, shared.keyNotFound);
    }
    connSetWriteHandler(client->conn, sendReplyToClient);
}
void commandObjectProc(Client* client)
{
    robj* key = client->argv[2];
    robj* op = client->argv[1];
    if (strcasecmp(((sds*)(op->ptr))->buf, "ENCODING") == 0) {
        robj* val = dbGet(client->db, key);
        if (val == NULL) {
            addWrite(client, shared.keyNotFound);
        } else {
            // TODO maybe we should use the valEncode() function
            char buf[1024];
            _encodingStr(val->encoding, buf, sizeof(buf));

            addWrite(client, robjCreateStringObject(buf));
        }
    }
    connSetWriteHandler(client->conn, sendReplyToClient);
}

void commandByeProc(Client* client)
{

    listAddNodeTail(server->clientsToClose, listCreateNode(client));
    addWrite(client, shared.bye);
    connSetWriteHandler(client->conn, sendReplyToClient);
}

/**
 * @brief [主] 主切从
 * 
 * @param [in] client 
 */
void commandSlaveofProc(Client* client)
{
    // TODO 主切从要切换state
    sds* s = (sds*)(client->argv[1]->ptr);
    char* host = s->buf;

    slaveStateInitFromMaster();
    slave->masterhost = strdup(host);
    slave->masterport = (int)(client->argv[2]->ptr);

    addWrite(client, shared.ok);
    
    // 创建新连接连接master
    Connection* conn = connCreate(server->eventLoop, TYPE_SOCKET);
    connConnect(conn, slave->masterhost, slave->masterport, slaveConnectMasterHandler);

    connSetWriteHandler(client->conn, sendReplyToClient);

}

/**
 * @brief 🚩[主]
 * 
 * @param [in] client 
 */
void commandPingMasterProc(Client* c) {
    switch (c->flags) {
        case CLIENT_ROLE_NORMAL:
        case CLIENT_ROLE_MASTER:
            addReply(c, shared.pong);
            connSetWriteHandler(client->conn, sendReplyToClient);
            break;
        case CLIENT_ROLE_SLAVE:
            SlaveClientInstance* instance = c->privdata;
            if (instance->replState == REPL_STATE_SLAVE_CONNECTING)
                addReply(c, shared.pong);
            else
                addReply(c, shared.syncing); // todo
            break;
        case CLIENT_ROLE_SENTINEL:
            // TODO
            addReply(c, shared.pong);
            break;
        default:
            addReply(c, shared.unknown);
            break;
    }
}

/**
 * @brief [主] 处理sync命令
 * 
 * @param [in] client 
 * @return * void 
 */
void commandSyncProc(Client* client)
{
    assert(CLIENT_IS_SLAVE(client));
    SlaveClientInstance* instance = client->privdata;
    instance->replState = REPL_STATE_MASTER_WAIT_SEND_FULLSYNC;  // 状态等待clientbuf 发送出FULLSYNC
    addWrite(client, shared.sync);
    connSetWriteHandler(client->conn, sendReplyToClient);
}

void commandReplconfProc(Client* client)
{
    //  暂不处理，不影响
    addWrite(client, shared.ok);
    connSetWriteHandler(client->conn, sendReplyToClient);
}
void commandReplACKProc(Client* client)
{
    //  暂不处理，不影响
    addWrite(client, shared.ok);
    connSetWriteHandler(client->conn, sendReplyToClient);
}


void commandSentinelProc(Client* client)
{
    log_debug("commandSentinel proc TODO!");

}

