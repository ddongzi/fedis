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

#include "redis.h"
#include "rdb.h"
#include "log.h"
#include "rio.h"
#include "repli.h"
#include "net.h"
#include "conf.h"


struct redisServer* server;

struct sharedObjects shared;

void _encodingStr(int encoding, char *buf, int maxlen) 
{
    switch (encoding) {
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

void commandSetProc(redisClient* client)
{
    int retcode = dbAdd(client->db, client->argv[1], client->argv[2]);
    if (retcode == DICT_OK) {
        addWrite(client, shared.ok);
        server->dirty++;
    } else {
        addWrite(client, shared.err);
    }
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);

}
void commandGetProc(redisClient* client)
{
    robj* res = (robj*)dbGet(client->db, client->argv[1]);
    if (res == NULL) { 
        addWrite(client, robjCreateStringObject("-ERR key not found"));
    } else {
        addWrite(client, res);
    }
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}
void commandDelProc(redisClient* client)
{
    int retcode = dbDelete(client->db, client->argv[1]);
    if (retcode == DICT_OK) {
        server->dirty++;

        addWrite(client, shared.ok);
    } else {
        addWrite(client, shared.keyNotFound);
    }
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}
void commandObjectProc(redisClient* client)
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
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);

}

void commandByeProc(redisClient* client)
{
    client->toclose = 1;
    addWrite(client, shared.bye);
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}

void masterToSlave(const char* ip, int port)
{
    log_info("Master => Slave");
    server->role = REDIS_CLUSTER_SLAVE;
    server->rdbFileName = RDB_FILENAME_2;
    // TODO 能否直接=
    server->masterhost = ip;
    server->masterport = port;
    loadCommands();    
}

// 127.0.0.1:6668
void commandSlaveofProc(redisClient* client)
{
    sds* s = (sds*)(client->argv[1]->ptr);
    char* hp = s->buf;
    char* ip = strtok(hp, ":");
    int port =  atoi(strtok(NULL, ":"));
    masterToSlave(ip, port);

    addWrite(client, shared.ok);
    connectMaster();
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}


void commandPingProc(redisClient* client)
{
    // todo replstate要小心设置。
    client->replState = REPL_STATE_MASTER_WAIT_PING;
    addWrite(client, shared.pong);
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}

void commandSyncProc(redisClient* client)
{
    client->replState = REPL_STATE_MASTER_WAIT_SEND_FULLSYNC;  // 状态等待clientbuf 发送出FULLSYNC
    addWrite(client, shared.sync);
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}

void commandReplconfProc(redisClient* client)
{
    //  暂不处理，不影响
    addWrite(client, shared.ok);
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}
void commandReplACKProc(redisClient* client)
{
    //  ���不处理，不影响
    client->flags = REDIS_CLIENT_SLAVE; // 设置对端为slave
    addWrite(client, shared.ok);
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}

char* getRoleStr(int role)
{
    char* buf = calloc(1, 16);
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


void generateInfoRespContent(int *argc, char** argv[])
{
    assert(server->role == REDIS_CLUSTER_MASTER);
    listNode* node;
    redisClient* c;

    *argc = 2; // runid, role
    // slaves
    node = listHead(server->clients);
    while(node != NULL) {
        c = node->value;
        if (c->flags == REDIS_CLIENT_SLAVE) {
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
    while(node != NULL) {
        c = node->value;
        if (c->flags == REDIS_CLIENT_SLAVE) {
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
    char* res = resp_encode(argc, argv);
    log_debug("INFO PROC: res : %s", res);
    addWrite(client, robjCreateStringObject(res));
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}
// 全局命令表，包含sentinel等所有命令

redisCommand commandsTable[] = {
    {CMD_WRITE | CMD_MASTER,                "SET", commandSetProc, -3},
    {CMD_RED | CMD_MASTER | CMD_SLAVE,    "GET", commandGetProc, 2},
    {CMD_WRITE |CMD_MASTER,                "DEL", commandDelProc, 2},
    {CMD_RED | CMD_MASTER | CMD_SLAVE,    "OBJECT", commandObjectProc, 3},
    {CMD_RED | CMD_ALL,                   "BYE", commandByeProc, 1},
    {CMD_RED | CMD_MASTER | CMD_SLAVE,    "SLAVEOF", commandSlaveofProc, 3},
    {CMD_RED |CMD_ALL,                   "PING", commandPingProc, 1},
    {CMD_RED | CMD_MASTER | CMD_SLAVE,    "REPLCONF", commandReplconfProc, 3},
    {CMD_RED |CMD_SLAVE | CMD_MASTER,    "SYNC", commandSyncProc, 1},
    {CMD_RED |CMD_SLAVE | CMD_MASTER,    "REPLACK", commandReplACKProc, 1},
    {CMD_RED |CMD_ALL,                   "INFO", commandInfoProc, 1},
};


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
dictType commandDictType = {
    .hashFunction = commandDictHashFunction,
    .keyCompare = commandDictKeyCompare,
    .keyDup =  commandDictKeyDup,
    .valDup =  commandDictValDup,
    .keyDestructor = commandDictKeyDestructor,
    .valDestructor = commandDictValDestructor
};



void appendServerSaveParam(time_t sec, int changes)
{
    server->saveParams = realloc(server->saveParams, sizeof(struct saveparam) * (server->saveCondSize + 1));
    server->saveParams[server->saveCondSize].seconds = sec;
    server->saveParams[server->saveCondSize].changes = changes;
    server->saveCondSize++;
}

/**
 * @brief 添加全局命令表， 如果命令不支持，在处理时候禁止
 * 
 */
void loadCommands()
{
    if (server->commands != NULL) {
        dictRelease(server->commands);
    }
    server->commands = dictCreate(&commandDictType, NULL);
    for (int i = 0; i < sizeof(commandsTable) / sizeof(commandsTable[0]); i++) {
        redisCommand* cmd = malloc(sizeof(redisCommand));
        memcpy(cmd, &commandsTable[i], sizeof(redisCommand));
        dictAdd(server->commands, cmd->name, cmd);
    }
    char* rolestr = getRoleStr(server->role);
    log_info("Load commands for role %s", rolestr);
    free(rolestr);
}

/**
 * @brief 初始化服务器配置
 * 
 * @param [in] server 
 */
void initServerConfig()
{
    server->port = REDIS_SERVERPORT;
    server->dbnum = REDIS_DEFAULT_DBNUM;
    server->saveCondSize = 0;
    server->saveParams = NULL;
    appendServerSaveParam(900, 1);
    appendServerSaveParam(300, 10000);
    appendServerSaveParam(10, 1);
    
    server->rdbFileName = RDB_FILENAME_1;
    
    server->maxclients = REDIS_MAX_CLIENTS;
    loadCommands();

    log_debug("√ init server config.\n");
}


/**
 * @brief 返回当前ms级时间戳
 * 
 * @return long long 
 */
long long mstime(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec) * 1000 + (tv.tv_usec / 1000);
}

void updateServerTime()
{
    server->unixtime = time(NULL);
    server->mstime = mstime();
}

void closeClients()
{
    log_debug("Will close %d clients. Now have %d clients", listLength(server->clientsToClose), listLength(server->clients));
    listNode* node = listHead(server->clientsToClose);
    while (node) {
        redisClient* client = node->value;
        log_debug("Close client [%d]%s:%d", client->fd, client->ip, client->port);
        listDelNode(server->clientsToClose, node);
        freeClient(client);
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
 * @brief 对socket recv后进行处理： 正常，
 * 
 * @param [in] c 
 * @param [in] nread 
 */
int checkSockRead(redisClient* c, int nread)
{
    if (nread == 0) {
        // 对端正常关闭，释放client. fd不能延迟， 因为fd一直可读epoll一直返回。
        log_debug("Close by peer. [%d]", c->fd);
        clientToclose(c);
        return 0;
    } else if (nread < 0) {
        // 错误处理
        log_error("Error check sock read, [%d] %s", c->fd, strerror(errno));
        clientToclose(c);
        return -1;
    } else {
        // 正常有数据
        return 1;

    }
}
/**
 * @brief INfo命令回复
 * 
 * @param [in] el 
 * @param [in] fd 
 * @param [in] privdata 
 */
void sentinelReadInfo(aeEventLoop *el, int fd, void *privdata)
{
    redisClient *client = (redisClient *)privdata;
    char buf[1024] = {0};
    rio r;
    rioInitWithFD(&r, fd);
    ssize_t nread = rioRead(&r, buf, sizeof(buf));
    int checked = checkSockRead(client, nread);
    if (checked) {
        sdscat(client->readBuf, buf);
        log_debug("Sentinel read info : %s", buf);
        client->lastinteraction = server->unixtime;
    }
}

/**
 * @brief sentinel定时任务10s
 * 
 * @param [in] eventLoop 
 * @param [in] id 
 * @param [in] clientData 
 */
int sentinelInfoCron( aeEventLoop* eventLoop, long long id, void* clientData)
{
    log_info("sentinel info cron.");
    if (server->role == REDIS_CLUSTER_SENTINEL) {
        // 定时发送info命令到monitor
        listNode* node = listHead(server->clients);
        
        while (node) {
            redisClient* client = node->value;
            assert(client);
            log_debug("ready info to %s:%d", client->ip, client->port);
            addWrite(node->value, shared.info);
            // 对于主动消息，我们通过自己创建读事件处理器来
            aeCreateFileEvent(server->eventLoop, client->fd, AE_READABLE, sentinelReadInfo, client);
            aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
            node = node->next;
        }
    }
    return 5000;
}
/**
 * @brief 定时任务 1s
 * 
 * @param [in] eventLoop 
 * @param [in] id 
 * @param [in] clientData 
 * @return int 周期时间
 */
int serverCron(struct aeEventLoop* eventLoop, long long id, void* clientData)
{
    log_debug("server cron.");

    if (server->role == REDIS_CLUSTER_MASTER) {
        // 检查SAVE条件，执行BGSAVE    
        bgSaveIfNeeded();
    }


    // 更新server时间
    updateServerTime();

    // TODO 作为服务器角色，应该立马关闭对应fd, 防止epoll一直无意义的转
    // 关闭clients
    closeClients();

    if (server->shutdownAsap) {
        // 检测到需要关闭。在shutdown中exit。
        prepareShutdown();
    }

    return 3000;
}


void sigChildHandler(int sig)
{
    pid_t pid;
    int stat;
    // 匹配任意子进程结束
    while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
        if (server->rdbChildPid == pid) {
            // 检查退出状态
            if (WIFEXITED(stat) && WEXITSTATUS(stat) == 0) {
                server->lastSave = server->unixtime;
                log_debug("server know %d finished\n", pid);
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
    if (server->shutdownAsap == 0) {
        server->shutdownAsap = 1;
    } else if (server->shutdownAsap == 1) {
        exit(1);
    }
}
void initServerSignalHandlers()
{
    signal(SIGCHLD, sigChildHandler);
    signal(SIGINT, sigIntHandler);
}

/**
 * @brief Create a Shared Objects object
 * 
 */
void createSharedObjects()
{
    // 1. 0-999整数
    for (int i = 0; i < REDIS_SHAREAD_MAX_INT; i++) {
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

    // REQUEST
    shared.ping = robjCreateStringObject("*1\r\n$4\r\nPING\r\n");
    shared.info = robjCreateStringObject("*1\r\n$4\r\nINFO\r\n");
}

void initServer()
{
    initServerSignalHandlers();
    
    createSharedObjects();

    server->id = getpid();

    server->unixtime = time(NULL);
    server->mstime = mstime();

    server->db = calloc(server->dbnum, sizeof(redisDb));
    for (int i = 0; i < server->dbnum; i++) {
        dbInit(server->db + i, i);
    }
    server->dirty = 0;
    server->lastSave = server->unixtime;

    server->rdbChildPid = -1;
    server->isBgSaving = 0;
    server->rdbfd = -1; //
    log_debug("●  load rdb from %s\n", server->rdbFileName);
    rdbLoad();

    server->clients = listCreate();
    server->clientsToClose = listCreate();

    server->eventLoop = aeCreateEventLoop(server->maxclients);
    server->bindaddr = NULL;
    int fd = anetTcpServer(server->port, server->bindaddr, server->maxclients);
    if (fd == -1) {
        exit(1);
    }



    log_debug("● create server, listening.....\n");
    
    aeCreateFileEvent(server->eventLoop, fd, AE_READABLE, acceptTcpHandler, NULL);
    log_debug("● create file event for ACCEPT, listening.....\n");
    // 注册定时任务
    aeCreateTimeEvent(server->eventLoop, 1000, serverCron, NULL);
    log_debug("● create time event for serverCron\n");
    


    // sentinel特性，
    if (server->role == REDIS_CLUSTER_SENTINEL) {
        char* monitor = get_config(server->configfile, "monitor");
        assert(monitor != NULL);
        dictType commandDictType = {
            .hashFunction = commandDictHashFunction,
            .keyCompare = commandDictKeyCompare,
            .keyDup =  commandDictKeyDup,
            .valDup =  commandDictValDup,
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


    log_info("√ server init finished.  ROLE:%s.", getRoleStr(server->role));


}
/**
 * @brief 1. 从服务器拒绝执行来自普通客户端的写命令
 * 
 * @param [in] cmd 
 * @return int 
 */
int isSupportedCmd(redisClient* c, redisCommand* cmd)
{
    assert(c);
    assert(cmd);
    if (server->role == REDIS_CLUSTER_SLAVE &&
        c->flags == REDIS_CLIENT_NORMAL &&
        cmd->flags & CMD_WRITE
    ) {
        log_debug("UNsupported cmd %s", cmd->name);
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
    while((entry = dictIterNext(iter)) != NULL) {
        redisCommand* cmd = entry->v.val;
        assert(cmd);
        if (isSupportedCmd(c, cmd) && strcasecmp(cmd->name, name) == 0) {
            log_debug("FIND cmd %s", name);
            dictReleaseIterator(iter);
            return cmd;
        }
    }
    log_debug("cant lookup cmd ! %s", name);
    dictReleaseIterator(iter);
    return NULL;
}

/**
 * @brief 原封不动的sds , resp字符串
 * 
 * @param [in] s 
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
    while (node) {
        c = node->value;
        assert(c);
        if (c->flags == REDIS_CLIENT_SLAVE) {
            slaves++;
            // 对端是slave
            log_debug("Propagate to %d slave, [%d]-%s:%d", slaves, c->fd, c->ip, c->port);
            sdscat(c->writeBuf, s->buf);
            aeCreateFileEvent(server->eventLoop, c->fd, AE_READABLE, readRespFromClient, c);
            aeCreateFileEvent(server->eventLoop, c->fd, AE_WRITABLE, sendReplyToClient, c);
        }
        node = node->next;
    }
    log_debug("Count propagate %d slaves.", slaves);
}
/**
 * @brief 调用执行命令。已有argc,argv[]
 * 
 * @param [in] c 
 */
void processCommand(redisClient * c)
{
    redisCommand* cmd;
    assert(c);
    // argv[0] 一定是字符串，sds
    char* cmdname = ((sds*)(c->argv[0]->ptr))->buf;
    log_debug("process CMD %s", cmdname);
    cmd = lookupCommand(c, cmdname);
    if (cmd == NULL) {
        log_debug("Will ret invalid!");
        addWrite(c, shared.invalidCommand);
        aeCreateFileEvent(server->eventLoop, c->fd, AE_WRITABLE, sendReplyToClient, c);
    } else {
        cmd->proc(c);
    }

    if ( cmd && (cmd->flags & CMD_WRITE) && server->role == REDIS_CLUSTER_MASTER) {
        commandPropagate(c->readBuf);
    }
    log_debug("Process ok , clear!");
    sdsclear(c->readBuf);
    c->argc = 0;
    for (int i = 0; i < c->argc; i++) {
        robjDestroy(c->argv[i]);
    }
    c->argv = NULL;
}
/**
 * @brief 解析协议到 argc, argv[]
    *1\r\n$4\r\nping\r\n
    $3\r\nSET\r\n
    $2\r\nk1\r\n
    $2\r\nv1\r\n
 * @param [in] client 
 */
void processClientQueryBuf(redisClient* client)
{
    if (client->readBuf == NULL )  return;
    // TODO 后续命令传播需要，命令处理完再清除
    sds* s = (sds*)(client->readBuf);
    sds* scpy = sdsdump(s);
    int argc;
    char** argv;
    int ret = resp_decode(scpy->buf, &argc, &argv);
    assert(ret == 0);
    for(int i = 0; i < argc; i++) {
        robj* obj = robjCreateStringObject(argv[i]);
        client->argv = realloc(client->argv, sizeof(robj*) * (client->argc + 1));
        client->argv[client->argc] = obj;
        client->argc++;
    }
    // 不一定是query 因为client是对端，可能是响应。
    processCommand(client);
    sdsfree(scpy);
}
/**
 * @brief 读取+OK, -ERR 格式
 * 
 * @param [in] el 
 * @param [in] fd 
 * @param [in] privData 
 */
void readRespFromClient(aeEventLoop *el, int fd, void *privData)
{
    redisClient *client = (redisClient *)privData;
    char buf[1024] = {0};
    rio r;
    rioInitWithFD(&r, fd);
    ssize_t nread = rioRead(&r, buf, sizeof(buf));
    int checked = checkSockRead(client, nread);
    if (checked) {
        log_info("Get RESP from client %s", buf);    
        client->lastinteraction = server->unixtime;
    }
}
/**
 * @brief 读取命令，*2\r\n$3\r\nget\r\n$3\r\nfoo\r\n
 * 
 * @param [in] el 
 * @param [in] fd 
 * @param [in] privData 
 */
void readQueryFromClient(aeEventLoop *el, int fd, void *privData)
{
    redisClient *client = (redisClient *)privData;
    char buf[1024] = {0};
    rio r;
    rioInitWithFD(&r, fd);
    ssize_t nread = rioRead(&r, buf, sizeof(buf));
    // TODO
    int checked = checkSockRead(client, nread);
    if (checked) {
        sdscat(client->readBuf, buf);
        log_debug(" processing query from client, %s, Client buf: %s", buf, client->readBuf->buf);
        processClientQueryBuf(client);
        log_debug(" processed query from client");
        client->lastinteraction = server->unixtime;
    }
}

/**
 * @brief 主读取RDB文件发送到slave, 切换fd写事件处理为 sendRDB
 *
 * @param [in] client
 */
void saveRDBToSlave(redisClient *client)
{
    struct stat st;
    long rdb_len;
    char length_buf[64];
    size_t length_len;
    rio sio;
    size_t nwritten = 0;

    if (server->rdbfd == -1)
    {
        server->rdbfd = open(server->rdbFileName, O_RDONLY);
        if (server->rdbfd == -1)
        {
            log_error("Open RDB file: %s failed: %s", server->rdbFileName, strerror(errno));
            return;
        }
    }
    if (fstat(server->rdbfd, &st) == -1)
    {
        log_error("Stat RDB file: %s failed: %s", server->rdbFileName, strerror(errno));
        return;
    }

    rdb_len = st.st_size;
    log_debug("Stat RDB file , size: %d", st.st_size);

    // 发送 $length\r\n
    sprintf(length_buf, "$%lu\r\n", rdb_len);
    length_len = strlen(length_buf);
    rioInitWithSocket(&sio, client->fd);
    nwritten = rioWrite(&sio, length_buf, length_len);
    log_debug("send RDB length: %s", length_buf);
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

    log_debug("RDB sent to slave %d， size:%lld", client->fd, rdb_len);
}

void sendReplyToClient(aeEventLoop *el, int fd, void *privdata)
{
    redisClient *client = (redisClient *)privdata;
    char *msg = client->writeBuf->buf;
    size_t msg_len = sdslen(client->writeBuf);
    ssize_t nwritten;

    rio sio;
    rioInitWithSocket(&sio, fd);
    nwritten = rioWrite(&sio, msg, msg_len);
    if (nwritten == 0 && sio.error)
    {
        close(fd);
        log_error("error writing %d , nwritten %d , error %d", fd, nwritten, sio.error);
        return;
    }

    log_debug("reply to client %d, %.*s (%zd bytes)", client->fd, (int)nwritten, msg, nwritten);

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
    client->lastinteraction = server->unixtime;
    if (client->toclose) {
        // 发送完再关闭
        clientToclose(client);
        aeDeleteFileEvent(el, fd, AE_READABLE); // epoll 删除fd 防止后续epoll一直对他读就绪
    }
}

