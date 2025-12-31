/**
 *
 * Command
 *  只负责进行逻辑处理，向client更新buf
 */

#include <stdio.h>
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
#include <stdbool.h>

#include "redis.h"
#include "rdb.h"
#include "log.h"
#include "rio.h"
#include "repli.h"
#include "net.h"
#include "conf.h"
#include "util.h"
#include "aof.h"
#include "resp.h"
struct redisServer* server;

extern struct RespShared resp;

char* getRoleStr(int role);

static void commandSetProc(redisClient* client);
static void commandGetProc(redisClient* client);
static void commandDelProc(redisClient* client);
static void commandObjectProc(redisClient* client);
static void commandByeProc(redisClient* client);
static void commandSlaveofProc(redisClient* client);
static void commandPingProc(redisClient* client);
static void commandReplconfProc(redisClient* client);
static void commandSyncProc(redisClient* client);
static void commandReplACKProc(redisClient* client);
static void commandInfoProc(redisClient* client);
static void commandHeartBeatProc(redisClient* client);
static void commandSelectProc(redisClient* client);
static void commandExpireProc(redisClient* client);
static void commandTtlProc(redisClient* client);
static void commandMultiProc(redisClient* client);
static void commandExecProc(redisClient* client);
static void commandWatchProc(redisClient* client);

// 全局命令表，包含sentinel等所有命令
redisCommand commandsTable[] = {
    {CMD_WRITE | CMD_MASTER, "SET", commandSetProc, 3},
    {CMD_READ | CMD_MASTER | CMD_SLAVE, "GET", commandGetProc, 2},
    {CMD_WRITE | CMD_MASTER, "DEL", commandDelProc, 2},
    {CMD_READ | CMD_MASTER | CMD_SLAVE, "OBJECT", commandObjectProc, 3},
    {CMD_MASTER | CMD_SLAVE, "BYE", commandByeProc, 1},
    {CMD_MASTER , "SLAVEOF", commandSlaveofProc, 3},
    {CMD_MASTER | CMD_SLAVE, "PING", commandPingProc, 1},
    {CMD_MASTER | CMD_SLAVE, "REPLCONF", commandReplconfProc, 3},
    {CMD_MASTER | CMD_SLAVE, "SYNC", commandSyncProc, 1},
    {CMD_MASTER | CMD_SLAVE, "REPLACK", commandReplACKProc, 1},
    {CMD_MASTER | CMD_SLAVE, "INFO", commandInfoProc, 1},
    {CMD_MASTER | CMD_SLAVE, "HEARTBEAT", commandHeartBeatProc, 1},
    {CMD_MASTER | CMD_SLAVE, "SELECT", commandSelectProc, 2},
    {CMD_WRITE | CMD_MASTER, "EXPIRE", commandExpireProc, 3},
    {CMD_READ | CMD_MASTER, "TTL", commandTtlProc, 2},
    {CMD_MASTER, "MULTI", commandMultiProc, 1},
    {CMD_MASTER, "EXEC", commandExecProc, 1},
    {CMD_MASTER, "WATCH", commandWatchProc, 1},
};


// command dictType
static unsigned long commandDictHashFunction(const void* key)
{
    unsigned long hash = 5381;
    const char* str = key;
    while (*str)
    {
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
    if (key == NULL)
    {
        return NULL;
    }
    size_t size = strlen((char*)key);
    char* res = malloc(size + 1);
    strcpy(res, key);
    return (void*)res;
}

static void* commandDictValDup(void* privdata, const void* obj)
{
    if (obj == NULL)
    {
        return NULL;
    }
    redisCommand* res = malloc(sizeof(redisCommand));
    memcpy(res, obj, sizeof(redisCommand));
    return (void*)res;
}

dictType commandDictType = {
    .hashFunction = commandDictHashFunction,
    .keyCompare = commandDictKeyCompare,
    .keyDup = commandDictKeyDup,
    .valDup = commandDictValDup,
    .keyDestructor = commandDictKeyDestructor,
    .valDestructor = commandDictValDestructor
};

/**
 * @brief 添加全局命令表， 如果命令不支持，在处理时候禁止
 *
 */
void loadCommands()
{
    if (server->commands != NULL)
    {
        dictRelease(server->commands);
    }
    server->commands = dictCreate(&commandDictType, NULL);
    for (int i = 0; i < sizeof(commandsTable) / sizeof(commandsTable[0]); i++)
    {
        redisCommand* cmd = malloc(sizeof(redisCommand));
        memcpy(cmd, &commandsTable[i], sizeof(redisCommand));
        dictAdd(server->commands, cmd->name, cmd);
    }
    char* rolestr = getRoleStr(server->role);
    log_info("Load commands for role %s", rolestr);
    free(rolestr);
}

void _encodingStr(int encoding, char* buf, int maxlen)
{
    switch (encoding)
    {
    case REDIS_ENCODING_EMBSTR:
        strncpy(buf, "embstr", maxlen - 1);
        break;
    case REDIS_ENCODING_INT:
        strncpy(buf, "integer", maxlen - 1);
        break;
    case REDIS_ENCODING_RAW:
        strncpy(buf, "raw", maxlen - 1);
        break;
    default:
        strncpy(buf, "unknown", maxlen - 1);
        break;
    }
}

/**
 *
 * @param client
 * @warning set命令 值必须传入
 */
void commandSetProc(redisClient* client)
{
    log_debug("Set proc..key: %s", client->argv[1]);
    if (client->argc == 2)
    {
        // TODO 在之前通过arity校验
        client->last_errno = ERR_VALUE_MISSED;
        sprintf(client->err_msg, "%s", resp.valmissed);
        addWrite(client, resp.valmissed);
    }
    else
    {
        sds* key = sdsnew(client->argv[1]);
        robj* v = robjCreateStringObject(client->argv[2]);
        int retcode = dbAdd(client->db, key, v);
        if (retcode == DICT_OK)
        {
            addWrite(client, resp.ok);
            server->dirty++;
        }
        else
        {
            client->last_errno = ERR_KEY_EXISTS;
            sprintf(client->err_msg, "-ERR:Duplicate key\r\n");
            addWrite(client, resp.dupkey);
        }
    }
}

void commandGetProc(redisClient* client)
{
    sds* k = sdsnew(client->argv[1]);
    robj* res = (robj*)dbGet(client->db, k);
    if (res == NULL)
    {
        addWrite(client, resp.keyNotFound);
    }
    else
    {
        addWrite(client, respEncodeBulkString(robjGetValStr(res)));
    }
}

void commandDelProc(redisClient* client)
{
    sds* k = sdsnew(client->argv[1]);
    int retcode = dbDelete(client->db, k);
    if (retcode == DICT_OK)
    {
        server->dirty++;
        addWrite(client, resp.ok);
    }
    else
    {
        addWrite(client, resp.keyNotFound);
    }
}

void commandObjectProc(redisClient* client)
{
    char* key = client->argv[2];
    char* op = client->argv[1];
    if (strcasecmp(op, "ENCODING") == 0)
    {
        robj* val = dbGet(client->db, sdsnew(key));
        if (val == NULL)
        {
            addWrite(client, resp.keyNotFound);
        }
        else
        {
            // TODO maybe we should use the valEncode() function
            char buf[1024];
            _encodingStr(val->encoding, buf, sizeof(buf));
            addWrite(client, respEncodeBulkString(buf));
        }
    }
}

void commandByeProc(redisClient* client)
{
    client->toclose = 1;
    addWrite(client, resp.bye);
}

void masterToSlave(const char* ip, int port)
{
    log_info("Master => Slave");
    // TODO
    assert(server->master == NULL);
    server->role = REDIS_CLUSTER_SLAVE;
    server->masterhost = ip;
    server->masterport = port;
    loadCommands();
}

// 127.0.0.1:6668
void commandSlaveofProc(redisClient* client)
{
    char* s = strdup(client->argv[1]);
    char* ip = strtok(s, ":");
    int port = atoi(strtok(NULL, ":"));
    masterToSlave(ip, port);

    addWrite(client, resp.ok);
    connectMaster();
}


void commandPingProc(redisClient* client)
{
    // todo replstate要小心设置。
    client->replState = REPL_STATE_MASTER_WAIT_PING;
    addWrite(client, resp.pong);
}

void commandSyncProc(redisClient* client)
{
    client->replState = REPL_STATE_MASTER_WAIT_SEND_FULLSYNC; // 状态等待clientbuf 发送出FULLSYNC
    addWrite(client, resp.sync);
}

void commandReplconfProc(redisClient* client)
{
    //  暂不处理，不影响
    addWrite(client, resp.ok);
    client->flags = REDIS_CLIENT_SLAVE; // 设置对端为slave
}

void commandReplACKProc(redisClient* client)
{
    client->lastinteraction = server->unixtime;
    addWrite(client, resp.ok);
}

char* getRoleStr(int role)
{
    char* buf = malloc(16);
    switch (role)
    {
    case REDIS_CLUSTER_MASTER:
        sprintf(buf, "master");
        break;
    case REDIS_CLUSTER_SENTINEL:
        sprintf(buf, "sentinel");
        break;
    case REDIS_CLUSTER_SLAVE:
        sprintf(buf, "slave");
        break;
    default:
        sprintf(buf, "unknown");
        break;
    }
    return buf;
}


void generateInfoRespContent(int* argc, char** argv[])
{
    assert(server->role == REDIS_CLUSTER_MASTER);
    listNode* node;
    redisClient* c;

    *argc = 2; // runid, role
    // slaves
    node = listHead(server->clients);
    while (node != NULL)
    {
        c = node->value;
        if (c->flags == REDIS_CLIENT_SLAVE)
        {
            *argc += 1;
        }
        node = node->next;
    }

    *argv = malloc(*argc * sizeof(char*));
    char buf[REDIS_MAX_STRING] = {0};
    size_t len = 0;
    int argi = 0;
    // 1.runid
    len = snprintf(buf, REDIS_MAX_STRING, "run_id:%d", server->id);
    (*argv)[argi] = malloc(len + 1);
    strncpy((*argv)[argi], buf, len + 1);
    memset(buf, 0, REDIS_MAX_STRING);
    argi++;

    // 2. role
    char* rolestr = getRoleStr(server->role);
    len = snprintf(buf, REDIS_MAX_STRING, "role:%s", rolestr);
    (*argv)[argi] = malloc(len + 1);
    strncpy((*argv)[argi], buf, len + 1);
    memset(buf, 0, REDIS_MAX_STRING);
    argi++;

    // 3. slaves
    int slavei = 0;
    node = listHead(server->clients);
    while (node != NULL)
    {
        c = node->value;
        if (c->flags == REDIS_CLIENT_SLAVE)
        {
            len = snprintf(buf, REDIS_MAX_STRING, "slave%d:ip=%s,port=%d,state=online",
                           slavei, c->ip, c->port
            );
            (*argv)[argi] = malloc(len + 1);
            strncpy((*argv)[argi], buf, len + 1);
            memset(buf, 0, REDIS_MAX_STRING);
            argi++;
            slavei++;
        }
        node = node->next;
    }
}

void commandInfoProc(redisClient* client)
{
    char** argv;
    int argc;
    generateInfoRespContent(&argc, &argv);
    char* res = respEncodeArrayString(argc, argv);
    log_debug("INFO PROC: res : %s", res);
    addWrite(client, res);
}

void commandHeartBeatProc(redisClient* client)
{
    addWrite(client, resp.ok);
}

void commandSelectProc(redisClient* client)
{
    //
    char* s = client->argv[1];
    long dbid;
    if (!string2long(s, &dbid))
    {
        addWrite(client, resp.err);
    }
    else
    {
        client->db = &server->db[dbid];
        client->dbid = (int)dbid;
        char buf[32] = {0};
        snprintf(buf, sizeof(buf), "OK, db is %d", (int)dbid);
        addWrite(client, respEncodeBulkString(buf));
    }
}

/**
 * expire key ns
 * 秒为单位
 * @param client
 */
void commandExpireProc(redisClient* client)
{
    sds* key = sdsnew(client->argv[1]);
    char* expire = client->argv[2]; //
    long expireat;
    if (string2long(expire, &expireat))
    {
        expireat += time(NULL);
        if (dbSetExpire(client->db, key, expireat) == 0)
        {
            server->dirty++;
            addWrite(client, resp.ok);
        }
        else
        {
            addWrite(client, resp.err);
        }
    }
    else
    {
        addWrite(client, resp.err);
    }
}

void commandTtlProc(redisClient* client)
{
    sds* key = sdsnew(client->argv[1]);
    if (dictContains(server->db->expires, key))
    {
        long ttl = dbGetTTL(client->db, key);
        char buf[64] = {0};
        snprintf(buf, sizeof(buf), "ttl:%lds", ttl);
        addWrite(client, respEncodeBulkString(buf));
    }
    else
    {
        addWrite(client, resp.keyNotFound);
    }
}

void commandMultiProc(redisClient* client)
{
    client->flags |= REDIS_MULTI;
    addWrite(client, resp.ok);
}

void commandExecProc(redisClient* client)
{
    // 清除multi状态 进入 exec状态
    client->flags &= ~REDIS_MULTI;
    client->flags |= REDIS_EXEC;
    if (client->flags &= REDIS_DIRTY_CAS)
    {
        // 事务安全已经破坏，拒绝执行
        addWrite(client, respEncodeBulkString("err dirty"));
    }
    else
    {
        // 执行事务队列的命令
        for (int i = 0; i < client->multiCmdCount; ++i)
        {
            sds* cmd = client->multcmds[i];
            sdsclear(client->readBuf);
            sdscatsds(client->readBuf, cmd);
            processClientQueryBuf(client);
            sdsclear(cmd);
        }
        addWrite(client, respEncodeBulkString("Exec ok"));
    }

    client->flags &= ~REDIS_EXEC;
    client->multiCmdCount = 0;
}

/**
 * 在multi之前执行watch
 * 在exec执行之前，监视任意数量键
 * 在exec执行时，检查监视的键是否被修改过，如果被修改过，拒绝事务，保证事务安全
 * @param client
 */
void commandWatchProc(redisClient* client)
{
    for (int i = 1; i < client->argc; ++i)
    {
        sds* key = sdsnew(client->argv[i]);
        dbAddWatch(client->db, key, client);
    }
    addWrite(client, resp.ok);
}

void appendServerSaveParam(time_t sec, int changes)
{
    server->saveParams = realloc(server->saveParams, sizeof(struct saveparam) * (server->saveCondSize + 1));
    server->saveParams[server->saveCondSize].seconds = sec;
    server->saveParams[server->saveCondSize].changes = changes;
    server->saveCondSize++;
}

/**
 * @brief 初始化服务器配置
 *
 * @param [in] server
 */
void initServerConfig()
{
    // 加载配置文件必要参数
    char* role = get_config(server->configfile,"role");
    if (!strncasecmp(role, "sentinel", 8)) server->role = REDIS_CLUSTER_SENTINEL;
    if (!strncasecmp(role, "master", 6)) server->role = REDIS_CLUSTER_MASTER;
    if (!strncasecmp(role, "slave", 5)) server->role = REDIS_CLUSTER_SLAVE;
    char* port = get_config(server->configfile,"port");
    server->port = atoi(port);
    char* consistency = get_config(server->configfile,"consistency");
    if (!strncasecmp(consistency, "rdb", 3)) server->rdbOn = true;
    if (!strncasecmp(consistency, "aof", 3)) server->aofOn = true;
    char* dbnum = get_config(server->configfile,"dbnum");
    server->dbnum = atoi(dbnum);
    char* rdbfile = get_config(server->configfile,"rdb_file");
    server->rdbfile = fullPath(rdbfile);

    if (server->role & REDIS_CLUSTER_SLAVE)
    {
        // 加载master
        char* master = get_config(server->configfile,"master");
        char* ip = strtok(master, ":");
        int port = atoi(strtok(NULL, ":"));
        server->masterhost = ip;
        server->masterport = port;
        log_debug("Slave load master: %s:%d", ip, port);
    }

    // bagsave
    server->saveCondSize = 0;
    server->saveParams = NULL;
    appendServerSaveParam(900, 1);
    appendServerSaveParam(300, 10000);
    appendServerSaveParam(10, 1); //10秒内修改一次

    server->maxclients = REDIS_MAX_CLIENTS;
    loadCommands();

    log_debug("√ init server config.  ");
}


void updateServerTime()
{
    server->unixtime = time(NULL);
    server->mstime = mstime();
}

void closeClients()
{
    // 构造关闭链表
    listNode* node = listHead(server->clients);
    redisClient* client;
    listNode* next = NULL;
    while (node != NULL)
    {
        next = node->next;
        client = (redisClient*) listNodeValue(node);
        if (client->flags & CLIENT_TO_CLOSE)
        {
            log_debug("have found a client to close !");
            listAddNodeTail(server->clientsToClose, listCreateNode(client));
            listDelNode(server->clients, node);
        }
        node = next;
    }

    node = listHead(server->clientsToClose);
    while (node)
    {
        redisClient* client = node->value;
        log_debug("Close client [%d]%s:%d", client->fd, client->ip, client->port);
        freeClient(client);
        listDelNode(server->clientsToClose, node);
        node = listHead(server->clientsToClose);
    }
}

void prepareShutdown()
{
    bgSaveIfNeeded();

    // TODO :

    // 4. 自动释放部分文件、网络资源
    exit(0);
}


/**
 * @brief 检查处理 socket read/recv后
 *
 * @param [in] c
 * @param [in] n nread或者nwrite
 * @return 如果检查后需要关闭对端，返回false
 */
bool checkSockReadWrite(redisClient* c, int n)
{
    if (n == 0)
    {
        // 对端正常关闭，释放client. fd不能延迟， 因为fd一直可读epoll一直返回。
        log_info("Close by peer. [%d]", c->fd);

        return false;
    }
    if (n < 0)
    {
        // 错误处理
        log_error("Error check sock read, [%d] %s", c->fd, strerror(errno));
        return false;
    }
    return true;
}

/**
 * @brief INfo命令回复
 *
 * @param [in] el
 * @param [in] fd
 * @param [in] privdata
 */
void sentinelReadInfo(aeEventLoop* el, int fd, void* privdata)
{
    redisClient* client = (redisClient*)privdata;
    char buf[1024] = {0};
    rio r;
    rioInitWithSocket(&r, fd);
    ssize_t nread = rioRead(&r, buf, sizeof(buf));
    if (checkSockReadWrite(client, nread))
    {
        sdscat(client->readBuf, buf);
        log_debug("Sentinel read info : %s", buf);
        client->lastinteraction = server->unixtime;
        sdsclear(client->readBuf);
    } else
    {
        clientToclose(client);
    }
}

/**
 * @brief sentinel定时任务10s
 *
 * @param [in] eventLoop
 * @param [in] id
 * @param [in] clientData
 */
int sentinelInfoCron(aeEventLoop* eventLoop, long long id, void* clientData)
{
    log_info("sentinel info cron.");
    if (server->role == REDIS_CLUSTER_SENTINEL)
    {
        // 定时发送info命令到monitor
        listNode* node = listHead(server->clients);

        while (node)
        {
            redisClient* client = node->value;
            assert(client);
            log_debug("ready info to %s:%d", client->ip, client->port);
            addWrite(node->value, resp.info);
            // 对于主动消息，我们通过自己创建读事件处理器来
            if (aeCreateFileEvent(server->eventLoop, client->fd, AE_READABLE, sentinelReadInfo, client) == AE_ERROR)
            {
                log_debug("TODO !something for sentinel ae failed");
            }
            if (aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendToClient, client) == AE_ERROR)
            {
                log_debug("TODO !something for sentinel ae failed");
            }
            node = node->next;
        }
    }
    return 5000;
}
void masterCheckSlave()
{
    // 可能应该单独吧slave放在一个链表
    listNode* node = listHead(server->clients);
    redisClient* client;
    while (node)
    {
        client = (redisClient*)node->value;
        if (client->flags & REDIS_CLIENT_SLAVE)
        {
            log_debug("Heartbeat %d", server->unixtime - client->lastinteraction);
            // 检查心跳时间
            if (server->unixtime - client->lastinteraction > MASTER_SLAVE_TIMEOUT)
            {
                log_debug("Master lost client[%d] 's heartbeat. will close", client->fd);
                clientToclose(client);
            }
        }
        node = node->next;
    }

}
int masterCron(struct aeEventLoop* eventLoop, long long id, void* clientData)
{
    // 检查SAVE条件，执行BGSAVE
    if (server->rdbOn)
        bgSaveIfNeeded();
    // 主 感知从是否断线了.如果断线就应该清除client了
    masterCheckSlave();
    return 3000;
}

/**
 * @brief 服务器定时： 主/从/sentinel  都有各自的
 *
 * @param [in] eventLoop
 * @param [in] id
 * @param [in] clientData
 * @return int 周期时间
 */
int serverCron(struct aeEventLoop* eventLoop, long long id, void* clientData)
{
    int period = 3000;
    if (server->role == REDIS_CLUSTER_MASTER)
    {
        period = masterCron(eventLoop, id, clientData);
    }
    if (server->role == REDIS_CLUSTER_SLAVE)
    {
        period = slaveCron(eventLoop, id, clientData);
    }
    // 更新server时间
    updateServerTime();

    // TODO 由于ae中优先处理文件事件，这就会导致，epollwait会有些待关闭的fd，会产生错误
    closeClients();

    if (server->shutdownAsap)
    {
        // 检测到需要关闭。在shutdown中exit。
        prepareShutdown();
    }
    return period;
}


void sigChildHandler(int sig)
{
    pid_t pid;
    int stat;
    // 匹配任意子进程结束
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
    {
        if (server->rdbChildPid == pid)
        {
            // 检查退出状态
            if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0)
            {
                server->lastSave = server->unixtime;
                log_debug("server know %d finished", pid);
            }
            server->rdbChildPid = -1;
            server->dirty = 0;
            server->isBgSaving = 0;
        }
    }
}

/**
 * @brief CTRL C 处理。
 *
 * @param [in] sig
 * @note 第一次 持久化、关闭资源
 *  第二次 强制退出
 */
void sigIntHandler(int sig)
{
    // TODO: 效果不一致？
    if (server->shutdownAsap == 0)
    {
        server->shutdownAsap = 1;
    }
    else if (server->shutdownAsap == 1)
    {
        exit(1);
    }
}

void initServerSignalHandlers()
{
    signal(SIGCHLD, sigChildHandler);
    signal(SIGINT, sigIntHandler);
}

void initServer()
{
    initServerSignalHandlers();

    robjInit();

    server->id = getpid();

    server->unixtime = time(NULL);
    server->mstime = mstime();

    server->db = calloc(server->dbnum, sizeof(redisDb));
    for (int i = 0; i < server->dbnum; i++)
    {
        dbInit(server->db + i, i);
    }
    server->dirty = 0;
    server->lastSave = server->unixtime;

    server->rdbChildPid = -1;
    server->isBgSaving = 0;
    server->rdbfd = -1; //
    if (server->rdbOn)
    {
        log_debug("load rdb from %s", server->rdbfile);
        rdbLoad();
    }

    server->clients = listCreate();
    server->clientsToClose = listCreate();

    server->eventLoop = aeCreateEventLoop(server->maxclients);
    server->bindaddr = NULL;
    int fd = anetTcpServer(server->port, server->bindaddr, server->maxclients);
    if (fd == -1)
    {
        exit(EXIT_FAILURE);
    }
    log_debug(" create server, listening.....");

    if (aeCreateFileEvent(server->eventLoop, fd, AE_READABLE, acceptTcpHandler, NULL) == AE_ERROR)
    {
        log_error("ae accept fd failed. unexpected!");
        exit(EXIT_FAILURE);
    }
    log_debug(" create file event for ACCEPT, listening.....");
    // 注册定时任务
    aeCreateTimeEvent(server->eventLoop, 1000, serverCron, NULL);
    log_debug(" create time event for serverCron");

    // sentinel特性，
    if (server->role == REDIS_CLUSTER_SENTINEL)
    {
        char* monitor = get_config(server->configfile,"monitor");
        assert(monitor != NULL);
        dictType commandDictType = {
            .hashFunction = commandDictHashFunction,
            .keyCompare = commandDictKeyCompare,
            .keyDup = commandDictKeyDup,
            .valDup = commandDictValDup,
            .keyDestructor = commandDictKeyDestructor,
            .valDestructor = commandDictValDestructor
        };
        server->instances = dictCreate(&commandDictType, NULL); // 键是名字，值是client
        char* name = strtok(monitor, ",");
        char* host = strtok(NULL, ",");
        char* port = strtok(NULL, ",");
        int fd = anetTcpConnect(host, atoi(port));
        assert(fd);
        redisClient* client = redisClientCreate(fd, host, atoi(port));
        client->flags = REDIS_CLIENT_MASTER;
        strcpy(client->name, name);

        log_info("sentinel monitor connect ok, %s-%s:%d", client->name, client->ip, client->port);
        listAddNodeTail(server->clients, listCreateNode(client));
        // 注册info定时任务
        aeCreateTimeEvent(server->eventLoop, 10000, sentinelInfoCron, NULL);
    }

    // TODO 从没有aof吧？
    // AOF。
    if (server->aofOn)
    {
        aof_init();
        aof_load();
    }

    //
    if (server->role & REDIS_CLUSTER_SLAVE)
    {
        connectMaster();
    }
    log_info("√ server init finished.  ROLE:%s.", getRoleStr(server->role));
}

/**
 * @brief 1. 从服务器拒绝执行来自普通客户端的写命令
 *
 * @param [in] cmd
 * @return int 支持返回1， 不支持返回0
 */
int isSupportedCmd(redisClient* c, redisCommand* cmd)
{
    assert(c);
    assert(cmd);
    if (server->role == REDIS_CLUSTER_SLAVE &&
        c->flags == REDIS_CLIENT_NORMAL &&
        cmd->flags & CMD_WRITE
    )
    {
        return 0;
    }

    return 1;
}

redisCommand* lookupCommand(redisClient* c, const char* name)
{
    assert(c);
    assert(name);
    assert(server->commands);
    dictIterator* iter = dictGetIterator(server->commands);
    dictEntry* entry;
    while ((entry = dictIterNext(iter)) != NULL)
    {
        redisCommand* cmd = entry->v.val;
        assert(cmd);
        if (isSupportedCmd(c, cmd) && strcasecmp(cmd->name, name) == 0)
        {
            dictReleaseIterator(iter);
            return cmd;
        }
    }
    log_debug("cant lookup cmd ! %s", name);
    dictReleaseIterator(iter);
    return NULL;
}

/**
 * @brief 主向从命令传播
 *
 * @param [in] s 原封不动的resp字符串
 */
void commandPropagate(sds* s)
{
    log_debug("Command propagate !");
    assert(s);
    assert(server);
    assert(server->role == REDIS_CLUSTER_MASTER);
    redisClient* c;
    int slaves = 0;
    listNode* node = listHead(server->clients);
    while (node)
    {
        c = node->value;
        assert(c);
        if (c->flags == REDIS_CLIENT_SLAVE)
        {
            slaves++;
            // 对端是slave
            log_debug("Propagate to %d slave, [%d]-%s:%d", slaves, c->fd, c->ip, c->port);
            sdscat(c->writeBuf, s->buf);
            if ( aeCreateFileEvent(server->eventLoop, c->fd, AE_READABLE, readFromClient, c) == AE_ERROR)
            {
                log_debug("command propagate ae failed! ");
                clientToclose(c);
            }
            if (aeCreateFileEvent(server->eventLoop, c->fd, AE_WRITABLE, sendToClient, c) == AE_ERROR)
            {
                log_debug("command propagate ae failed! ");
                clientToclose(c);
            }
        }
        node = node->next;
    }
    log_debug("Count propagate %d slaves.", slaves);
}

/**
 * 写命令 , 为watch的客户端 设置dirty标识
 * @param client
 */
void touchWatchKey(redisClient* client)
{
    sds* key = sdsnew(client->argv[1]);
    if (dbIsWatching(client->db, key))
    {
        list* clients = dictFetchValue(client->db->watched_keys, key);
        listNode* node = listHead(clients);
        while (node)
        {
            redisClient* watch_client = listNodeValue(node);
            watch_client->flags |= REDIS_DIRTY_CAS;
            listDelNode(clients, node);
            node = listHead(clients);
        }
    }
}

/**
 * @brief 调用执行命令。已有argc,argv[]
 *
 * @param [in] c
 */
void processCommand(redisClient* c)
{
    redisCommand* cmd;
    assert(c);
    // argv[0] 一定是字符串，sds
    char* cmdname = c->argv[0];
    // log_debug("process CMD %s", cmdname);
    cmd = lookupCommand(c, cmdname);
    if (cmd == NULL)
    {
        log_debug("Will ret invalid!");
        addWrite(c, resp.invalidCommand);
    }
    else
    {
        // 写命令写入aof
        if (!(c->flags & REDIS_CLIENT_FAKE) && server->aofOn && (cmd->flags & CMD_WRITE))
        {
            sdscatsds(server->aof.active_buf, c->readBuf);
        }
        // 读写数据库时候，惰性删除 访问的键
        if (cmd->flags & (CMD_READ | CMD_WRITE))
        {
            expireIfNeed(c->db, sdsnew(c->argv[1]));
        }
        cmd->proc(c);
        // 监视键更新
        if (cmd->flags & CMD_WRITE)
        {
            touchWatchKey(c);
        }
    }

    if (cmd && (cmd->flags & CMD_WRITE) && server->role == REDIS_CLUSTER_MASTER)
    {
        commandPropagate(c->readBuf);
    }
    // log_debug("Process ok , clear!");
    sdsclear(c->readBuf);
    for (int i = 0; i < c->argc; ++i)
    {
        free(c->argv[i]);
    }
    c->argc = 0;
}

void multiInQueue(redisClient* c)
{
}

/**
 * @brief 处理一次RESP协议的请求。
 * @param [in] client
 *
 */
void processClientQueryBuf(redisClient* client)
{
    if (client->readBuf == NULL) return;
    sds* s = (sds*)(client->readBuf);
    sds* scpy = sdsdump(s);
    int argc;
    char** argv;
    int ret = resp_decode(scpy->buf, &argc, &argv);
    if (ret == 0)
    {
        // 按照命令执行

        // 如果处于事务状态，设置事务队列，暂不执行
        if ((client->flags & REDIS_MULTI)
            && strncasecmp(argv[0], "exec", 4) != 0)
        {
            // 加入事务队列(即readbuf暂存一条resp)，返回queued
            clientMultiAdd(client);
            addWrite(client, respEncodeBulkString("queued"));
            sdsclear(client->readBuf);
        }
        else
        {
            // 执行命令
            client->argv = argv;
            client->argc = argc;
            processCommand(client);
        }
        if (aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendToClient, client) == AE_ERROR)
        {
            log_debug("process client query buf ae failed!!");
            clientToclose(client);
        }
    }
    else if (*scpy->buf == '+' || *scpy->buf == '-')
    {
        // 按照响应执行
        log_info("Get RESP from client %s", resp_str(scpy->buf));
        // slave从 会在这里收到响应。
        if (client->flags & REDIS_CLIENT_MASTER)
        {
            server->master->lastinteraction = server->unixtime;
        }
    }
    sdsfree(scpy);
}

/**
 * @brief
 *
 * @param [in] el
 * @param [in] fd
 * @param [in] privData
 * @deprecated 逻辑上由readFromClient 统一处理
 */
void readRespFromClient(aeEventLoop* el, int fd, void* privData)
{
    redisClient* client = (redisClient*)privData;
    char buf[1024] = {0};
    rio r;
    rioInitWithFD(&r, fd);
    ssize_t nread = rioRead(&r, buf, sizeof(buf));
    if (checkSockReadWrite(client, nread))
    {
        log_info("Get RESP from client.  %s", resp_str(buf));
    } else
    {
        log_debug("read resp from client failed!");
        clientToclose(client);
    }
    // 恢复默认的读事件处理
    if (aeCreateFileEvent(el, fd, AE_READABLE, readFromClient, client) == AE_ERROR)
    {
        clientToclose(client);
    }
}

/**
 * @brief 普通客户端fd 读处理。 默认TCP接受后注册。
 *  内容都是*开头数组字符串
 * @param [in] el
 * @param [in] fd
 * @param [in] privData
 */
void readFromClient(aeEventLoop* el, int fd, void* privData)
{
    redisClient* client = (redisClient*)privData;
    char buf[1024] = {0};
    rio r;
    rioInitWithFD(&r, fd);
    ssize_t nread = rioRead(&r, buf, sizeof(buf));
    if (checkSockReadWrite(client, nread))
    {
        sdscat(client->readBuf, buf);
        processClientQueryBuf(client);
        sdsclear(client->readBuf);
    } else
    {
        log_debug("read from client failed");
        clientToclose(client);
    }
}

/**
 * @brief 主读取RDB文件发送到slave, 切换fd写事件处理为 sendRDB
 *
 * @param [in] client
 */
void saveRDBToSlave(redisClient* client)
{
    struct stat st;
    long rdb_len;
    char length_buf[64];
    size_t length_len;
    rio sio;
    size_t nwritten = 0;

    if (server->rdbfd == -1)
    {
        server->rdbfd = open(server->rdbfile, O_RDWR);
    }

    if (fstat(server->rdbfd, &st) == -1)
    {
        log_error("Stat RDB file: %s failed: %s", server->rdbfile, strerror(errno));
        return;
    }

    rdb_len = st.st_size;

    // 发送 $length\r\n
    sprintf(length_buf, "$%lu\r\n", rdb_len);
    length_len = strlen(length_buf);
    rioInitWithSocket(&sio, client->fd);
    nwritten = rioWrite(&sio, length_buf, length_len);
    log_debug("send RDB length field: %d", rdb_len);
    if (nwritten == 0 && sio.error)
    {
        close(client->fd);
        return;
    }
    if (nwritten != length_len)
    {
        log_error("nwritten != length_len");
        return;
    }

    // 发送 RDB 数据, 通过sendfile,
    off_t offset = 0;
    ssize_t sent = sendfile(client->fd, server->rdbfd, &offset, rdb_len);
    if (sent < 0)
    {
        log_error("Failed to send RDB FILE to client %d: %s", client->fd, strerror(errno));
        return;
    }

    log_debug("Send rdb data to slave %d， size:%lld", client->fd, rdb_len);
}

void sendToClient(aeEventLoop* el, int fd, void* privdata)
{
    redisClient* client = (redisClient*)privdata;
    char* msg = client->writeBuf->buf;

    size_t msg_len = sdslen(client->writeBuf);
    ssize_t nwritten;

    nwritten = write(client->fd, msg, msg_len);
    if (!checkSockReadWrite(client, nwritten))
    {
        log_debug("Send to client failed");
        clientToclose(client);
        return;
    }


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

    // 写完FULLSYNC之后触发状态转移
    if (client->replState == REPL_STATE_MASTER_WAIT_SEND_FULLSYNC)
    {
        client->replState = REPL_STATE_MASTER_SEND_RDB;
        saveRDBToSlave(client); // 发送 RDB
    }
    aeDeleteFileEvent(el, fd, AE_WRITABLE); // 普通命令回复结束
    if (client->toclose)
    {
        // 发送完再关闭
        clientToclose(client);
        aeDeleteFileEvent(el, fd, AE_READABLE); // epoll 删除fd 防止后续epoll一直对他读就绪
    }
}

