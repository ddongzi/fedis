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



void commandSetProc(client* client)
{
    int retcode = dbAdd(client->db, client->argv[1], client->argv[2]);
    if (retcode == DICT_OK) {
        addWrite(client, shared.ok);
        server->dirty++;
    } else {
        addWrite(client, shared.err);
    }
    // TODO 通过set writeHandler设置， 并且command表应该是回调
    aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);

}
void commandGetProc(client* client)
{
    robj* res = (robj*)dbGet(client->db, client->argv[1]);
    if (res == NULL) { 
        addWrite(client, robjCreateStringObject("-ERR key not found"));
    } else {
        addWrite(client, res);
    }
    aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);
}
void commandDelProc(client* client)
{
    int retcode = dbDelete(client->db, client->argv[1]);
    if (retcode == DICT_OK) {
        server->dirty++;

        addWrite(client, shared.ok);
    } else {
        addWrite(client, shared.keyNotFound);
    }
    aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);
}
void commandObjectProc(client* client)
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
    aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);

}

void commandByeProc(client* client)
{

    listAddNodeTail(server->clientsToClose, listCreateNode(client));
    addWrite(client, shared.bye);
    aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);

}

/**
 * @brief [主] 主切从
 * 
 * @param [in] client 
 */
void commandSlaveofProc(client* client)
{
    // TODO 主切从要切换state
    sds* s = (sds*)(client->argv[1]->ptr);
    char* host = s->buf;

    slaveStateInitFromMaster();
    slave->masterhost = strdup(host);
    slave->masterport = (int)(client->argv[2]->ptr);

    addWrite(client, shared.ok);
    connectMaster();
    aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);
}

/**
 * @brief 🚩[主]
 * 
 * @param [in] client 
 */
void commandPingMasterProc(client *c) {
    switch (c->flags) {
        case CLIENT_ROLE_NORMAL:
            addReply(c, shared.pong);
            aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);
            break;
        case CLIENT_ROLE_MASTER:
            addReply(c, shared.pong);
            aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);
            break;
        case CLIENT_ROLE_SLAVE:
            if (c->replState == REPL_STATE_CONNECTED)
                addReply(c, shared.pong);
            else
                addReply(c, shared.syncing);
            break;
        case CLIENT_ROLE_SENTINEL:
            if (c->sentinelState)
                updateSentinelState(c);
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
void commandSyncProc(client* client)
{
    assert(CLIENT_IS_SLAVE(client));
    slaveInstance* instance = (slaveInstance*)client->instance;
    instance->replState = REPL_STATE_MASTER_WAIT_SEND_FULLSYNC;  // 状态等待clientbuf 发送出FULLSYNC
    addWrite(client, shared.sync);
    aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);
}

void commandReplconfProc(client* client)
{
    //  暂不处理，不影响
    addWrite(client, shared.ok);
    aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);
}
void commandReplACKProc(client* client)
{
    //  暂不处理，不影响
    addWrite(client, shared.ok);
    aeCreateFileEvent(server->eventLoop, client->connection->cfd, AE_WRITABLE, sendReplyToClient, client);
}


void commandSentinelProc(client* client)
{
    // TODO
    log_debug("commandSentinel proc!");
}