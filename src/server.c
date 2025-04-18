#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#include "server.h"
#include "rdb.h"
#include "log.h"
#include "rio.h"
#include "slave.h"
#include "net.h"
#include "sentinel.h"
#include "command.h"
#include "conf.h"
#include "socket.h"
#include "client.h"

struct redisServer *server;

struct sharedObjects shared;

void _encodingStr(int encoding, char *buf, int maxlen)
{
    switch (encoding)
    {
    case REDIS_ENCODING_EMBSTR:
        strncpy(buf, "embstr", maxlen - 1);
        break;
    case REDIS_ENCODING_INT:
        strncpy(buf, "int", maxlen - 1);
        break;
    case REDIS_ENCODING_RAW:
        strncpy(buf, "raw", maxlen - 1);
        break;
    default:
        strncpy(buf, "unknown", maxlen - 1);
        break;
    }
}

static void loadCommands(int role)
{
    // TODO

    int tablelen = 0;
    redisCommand *table = NULL;
    switch (role)
    {
    case SERVER_ROLE_MASTER:
        table = commandsMasterTable;
        tablelen = sizeof(commandsMasterTable);
        break;
    case SERVER_ROLE_SLAVE:
        table = commandsSlaveTable;
        tablelen = sizeof(commandsSlaveTable);
        break;
    case SERVER_ROLE_SENTINEL:
        table = commandsSentinelTable;
        tablelen = sizeof(commandsSentinelTable);
        break;

    default:
        break;
    }
    for (int i = 0; i < tablelen; i++)
    {
        dictAdd(server->commands, table[i].name, table[i]);
    }
}

/**
 * @brief 初始化服务器配置, 这里只能是主、sentinel。  从角色由运行动态切换而来
 *
 * @param [in] role [SERVER_ROLE_MASTER, SERVER_ROLE_SENTINEL]
 */
void serverInitConfig()
{
    server->configfile = "../conf/server.conf";
    int port = get_config(server->configfile, "port");
    char *role = get_config(server->configfile, "role");
    if (strcasecmp("slave", role) == 0)
    {
        server->role = SERVER_ROLE_SLAVE;
    }
    else if (strcasecmp("sentinel", role) == 0)
    {
        server->role = SERVER_ROLE_SENTINEL;
    }
    else
    {
        server->role = SERVER_ROLE_MASTER;
    }
    server->maxclients = REDIS_MAX_CLIENTS; // TCP类属性

    if (server->role == SERVER_ROLE_MASTER)
    {
        masterInitConfig();
    }
    if (server->role == SERVER_ROLE_SLAVE)
    {
        log_info("TODO Slave server init");
    }
    if (server->role == SERVER_ROLE_SENTINEL)
    {
        sentinelStateInitConfig();
    }

    server->commands = dictCreate(&commandDictType, NULL);
    loadCommands(server->role);
}

/**
 * @brief 返回当前ms级时间戳
 *
 * @return long long
 */
long long mstime(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec) * 1000 + (tv.tv_usec / 1000);
}

void updateServerTime()
{
    server->unixtime = time(NULL);
    server->mstime = mstime();
}
void freeClient(Client *client)
{
    log_debug("free client %d", client->conn->fd);
    // 关闭连接, privdata清除
    connClose(client->conn);
    if (client->privdata)
        free(client->privdata);

    close(conn->fd);
    sdsfree(client->readBuf);
    sdsfree(client->writeBuf);
    // 正常情况，每次执行完命令argv就destroy了，临时
    if (client->argv)
    {
        for (int i = 0; i < client->argc; i++)
        {
            robjDestroy(client->argv[i]);
        }
    }
    free(client);
}
void closeClients()
{
    listNode *node = listHead(server->clientsToClose);
    while (node)
    {
        Client *client = node->value;
        listDelNode(server->clientsToClose, node);
        freeClient(client);
        node = listHead(server->clientsToClose);
    }
}
void prepareShutdown()
{
    if (server->role == SERVER_ROLE_MASTER)
    {
        bgSaveIfNeeded();
    }

    // TODO :

    // 4. 自动释放部分文件、网络资源
    exit(0);
}

/**
 * @brief 定时任务
 *
 * @param [in] eventLoop
 * @param [in] id
 * @param [in] clientData
 * @return int 周期时间
 */
int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData)
{
    log_debug("server cron.");

    if (server->role == SERVER_ROLE_MASTER)
    {
        // 检查SAVE条件，执行BGSAVE
        bgSaveIfNeeded();
    }
    // 更新server时间
    updateServerTime();

    // 关闭clients
    closeClients();

    if (server->shutdownAsap)
    {
        // 检测到需要关闭。在shutdown中exit。
        prepareShutdown();
    }

    return 3000;
}

/**
 * @brief Create a Shared Objects object
 *
 */
void createSharedObjects()
{
    // 1. 0-999整数
    for (int i = 0; i < REDIS_SHAREAD_MAX_INT; i++)
    {
        char buf[5];
        snprintf(buf, 4, "%d", i);
        buf[4] = '\0';
        shared.integers[i] = robjCreateStringObject(buf);
    }
    // 2. RESP
    shared.ok = robjCreateStringObject("+OK\r\n");
    shared.pong = robjCreateStringObject("+PONG\r\n");
    shared.err = robjCreateStringObject("-err\r\n");
    shared.keyNotFound = robjCreateStringObject("-ERR key not found\r\n");
    shared.bye = robjCreateStringObject("+bye\r\n");
    shared.invalidCommand = robjCreateStringObject("-Invalid command\r\n");
    shared.sync = robjCreateStringObject("+FULLSYNC\r\n");

    shared.ping = robjCreateStringObject("*1\r\n$4\r\nPING\r\n");
}

/**
 * @brief server收到accept
 *
 * @param [in] conn
 */
void srvAcceptHandler(Connection *conn)
{
    Client *client = clientCreate(conn);
    listAddNodeTail(server->clients, listCreateNode(client));
    // 注册读事件处理器
    conn->privData = client;
    connSetReadHandler(conn, readQueryFromClient);
}

/**
 * @brief
 *
 */
void serverInit()
{
    createSharedObjects();

    server->unixtime = time(NULL);
    server->mstime = mstime();

    server->clients = listCreate();
    server->clientsToClose = listCreate();

    server->eventLoop = aeCreateEventLoop(server->maxclients);
    server->bindaddr = NULL;

    switch (server->role)
    {
    case SERVER_ROLE_MASTER:
        master = calloc(1, sizeof(struct Master));
        masterInit();
        break;
    case SERVER_ROLE_SENTINEL:
        sentinel = calloc(1, sizeof(struct Sentinel));
        sentinelStateInit();
        break;
    case SERVER_ROLE_SLAVE:
        break;
    default:
        break;
    }
}

/**
 * 目前只用2个， net socket 和 unix socket
 */
void initListeners()
{
    // 0：TCP
    ConnectionListener tcpListener = server->listeners[0];
    tcpListener.port = REDIS_SERVERPORT;
    tcpListener.bindaddr = NULL; // 之后会分配， 考虑copy?
    tcpListener.type = connGetConnType(TYPE_SOCKET);

    // 1：unix socket
    // 2. TLS socket

    for (size_t i = 0; i < MAX_TYPE_LISTENERS; i++)
    {
        if (server->listeners[i].type == NULL)
        {
            continue;
        }
        if (connListen(&server->listeners[i]) == RET_ERR)
        {
            log_error("Listen failed");
        }
    }
}
void initTimers()
{
    // 注册定时任务
    aeCreateTimeEvent(server->eventLoop, 1000, serverCron, NULL);
    log_debug("● create time event for serverCron\n");
}

redisCommand *lookupCommand(dict *commands, const char *cmd)
{
    return dictFetchValue(commands, cmd);
}
/**
 * @brief 调用执行命令。已有argc,argv[]
 *
 * @param [in] c
 */
void processCommand(Client *c)
{
    redisCommand *cmd;
    // argv[0] 一定是字符串，sds
    cmd = lookupCommand(server->commands, ((sds *)(c->argv[0]->ptr))->buf);
    if (cmd == NULL)
    {
        addWrite(c, shared.invalidCommand);
        return;
    }
    cmd->proc(c);
    c->argc = 0;
    for (int i = 0; i < c->argc; i++)
    {
        robjDestroy(c->argv[i]);
    }
    c->argv = NULL;
}
/**
 * @brief 解析协议到 argc, argv[]
 *  *3\r\n
    $3\r\nSET\r\n
    $2\r\nk1\r\n
    $2\r\nv1\r\n
 * @param [in] client
 */
void processClientQueryBuf(Client *client)
{
    if (client->readBuf == NULL)
        return;

    sds *s = (sds *)(client->readBuf);
    while (sdslen(s))
    {
        // 预期：只有一行，一轮
        if (client->argc == 0)
        {
            // 解析 argc
            if (*(s->buf) != '*')
            {
                return; // 非预期
            }
            char *p = strchr(s->buf, '\n');
            client->argc = atoi(s->buf + 1);
            sdsrange(s, p - s->buf + 1, s->len - 1); // 移除argc行，处理完
        }
        int remain = client->argc;
        client->argv = calloc(client->argc, sizeof(robj *));
        while (remain > 0)
        {
            char *p = strchr(s->buf, '\n');
            int len = atoi(s->buf + 1);
            sdsrange(s, p - s->buf + 1, s->len - 1); // 移除len字段
            p = strchr(s->buf, '\r');
            *p = '\0';

            log_debug("%s, argv: %s\n", __func__, s->buf);
            client->argv[client->argc - remain] = robjCreateStringObject(s->buf);
            // 处理完一行
            sdsrange(s, p - s->buf + 2, s->len - 1);
            remain--;
        }
    }
    // sdsclear(client->readBuf); // 不需要，过程中已经读取完了
    processCommand(client);
}

void readQueryFromClient(Connection *conn)
{
    Client *client = conn->privData;
    char buf[1024] = {0};

    size_t nread = conn->type->read(conn, buf, sizeof(buf));
    log_debug("deal query from client, %s", respParse(buf));

    sdscat(client->readBuf, buf);
    log_debug(" processing query from client, %s", buf);
    processClientQueryBuf(client);
    log_debug(" processed query from client");
    // 写事件转移到各命令 自注册
}

void sendReplyToClient(Connection *conn)
{
    Client *client = conn->privData;
    char *msg = client->writeBuf->buf;
    size_t msg_len = sdslen(client->writeBuf);
    int nwritten;

    nwritten = connWrite(conn, msg, msg_len);
    if (nwritten <= 0)
    {
        log_error("write error!");
        return;
    }

    log_debug("reply to client %d, %.*s (%zd bytes)", conn->fd, (int)nwritten, msg, nwritten);

    // 更新缓冲区
    if (nwritten == msg_len)
    {
        sdsclear(client->writeBuf); // 全部发送完毕
    }
    else
    {
        // sdsrange(client->writeBuf, nwritten, -1); // 删除已发送部分
        log_error("unexpected. 没发送完. need todo");
        return; // 等待下次写事件发送剩余数据
    }

    // TODO 写到master
    if (CLIENT_IS_SLAVE(client))
    {
        SlaveClientInstance *instance = client->privdata;
        // 写完FULLSYNC之后触发状态转移
        if (instance->replState == REPL_STATE_MASTER_WAIT_SEND_FULLSYNC)
        {
            instance->replState = REPL_STATE_MASTER_SEND_RDB;
            masterRDBToSlave(conn); // 发送RDB
        }
    }

    connSetWriteHandler(conn, NULL);
}

void initExtra()
{
}

int main(int argc, char **argv)
{

    log_set_level(LOG_DEBUG);
    log_debug("hello log.");

    server = calloc(1, sizeof(struct redisServer));
    serverInit();

    serverInitConfig();
    for (size_t i = 0; i < argc; i++)
    {
        if (strcasecmp(argv[i], "-p") == 0)
        {
            server->port = atoi(argv[++i]);
        }
    }
    // TODO init config 和init顺序？

    initListeners();
    initTimers();

    initExtra(); // 特性部分
    log_debug("√ init server .\n");
    aeMain(server->eventLoop);

    return 0;
}
