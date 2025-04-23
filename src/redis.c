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

    listAddNodeTail(server->clientsToClose, listCreateNode(client));
    addWrite(client, shared.bye);
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}

void commandSlaveofProc(redisClient* client)
{
    sds* s = (sds*)(client->argv[1]->ptr);
    char* host = s->buf;
    server->role = REDIS_CLUSTER_SLAVE;
    server->masterhost = strdup(host);
    server->masterport = (int)(client->argv[2]->ptr);
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
    addWrite(client, shared.ok);
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}
void commandInfoProc(redisClient* client)
{
    char* argv[] = {"test_run_id:aaaaa", "role:master","slave0:ip=127.0.0.1,port=11,state=online"};
    char* res = respFormat(3, argv);
    log_debug("INFO PROC: res : %s", res);
    addWrite(client, res);
    aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
}
// 全局命令表，包含sentinel等所有命令
redisCommand commandsTable[] = {
    {CMD_MASTER,                "SET", commandSetProc, -3},
    {CMD_MASTER | CMD_SLAVE,    "GET", commandGetProc, 2},
    {CMD_MASTER,                "DEL", commandDelProc, 2},
    {CMD_MASTER | CMD_SLAVE,    "OBJECT", commandObjectProc, 3},
    {CMD_ALL,                   "BYE", commandByeProc, 1},
    {CMD_MASTER | CMD_SLAVE,    "SLAVEOF", commandSlaveofProc, 3},
    {CMD_ALL,                   "PING", commandPingProc, 1},
    {CMD_SLAVE,                 "REPLCONF", commandReplconfProc, 3},
    {CMD_SLAVE,                 "SYNC", commandSyncProc, 1},
    {CMD_SLAVE,                 "REPLACK", commandReplACKProc, 1},
    {CMD_ALL,                   "REPLACK", commandInfoProc, 1},
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
    server->port = REDIS_SERVERPORT;
    server->dbnum = REDIS_DEFAULT_DBNUM;
    server->saveCondSize = 0;
    server->saveParams = NULL;
    appendServerSaveParam(900, 1);
    appendServerSaveParam(300, 10000);
    appendServerSaveParam(10, 1);

    server->rdbFileName = "/home/dong/fedis/data/1.rdb";
    
    server->maxclients = REDIS_MAX_CLIENTS;

    dictType commandDictType = {
        .hashFunction = commandDictHashFunction,
        .keyCompare = commandDictKeyCompare,
        .keyDup =  commandDictKeyDup,
        .valDup =  commandDictValDup,
        .keyDestructor = commandDictKeyDestructor,
        .valDestructor = commandDictValDestructor
    };

    server->commands = dictCreate(&commandDictType, NULL);
    for (int i = 0; i < sizeof(commandsTable) / sizeof(commandsTable[0]); i++) {
        if (server->role == REDIS_CLUSTER_MASTER && (commandsTable[i].flags & CMD_MASTER)) {
            redisCommand* cmd = malloc(sizeof(redisCommand));
            memcpy(cmd, &commandsTable[i], sizeof(redisCommand));
            dictAdd(server->commands, cmd->name, cmd);
            continue;
        }
        if (server->role == REDIS_CLUSTER_SLAVE && (commandsTable[i].flags & CMD_SLAVE)) {
            redisCommand* cmd = malloc(sizeof(redisCommand));
            memcpy(cmd, &commandsTable[i], sizeof(redisCommand));
            dictAdd(server->commands, cmd->name, cmd);
            continue;
        }
        if (server->role == REDIS_CLUSTER_SENTINEL && (commandsTable[i].flags & CMD_SENTINEL)) {
            redisCommand* cmd = malloc(sizeof(redisCommand));
            memcpy(cmd, &commandsTable[i], sizeof(redisCommand));
            dictAdd(server->commands, cmd->name, cmd);
            continue;
        }
    }
    
    log_debug("server. commands role %d: %zu", server->role, dictSize(server->commands));



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
void freeClient(redisClient* client)
{
    log_debug("free client %d\n", client->fd);
    close(client->fd);
    // TODO 如果 query reply buf还有怎么办？
    sdsfree(client->readBuf);
    sdsfree(client->writeBuf);
    // 正常情况，每次执行完命令argv就destroy了，临时
    if (client->argv) {
        for(int i = 0; i < client->argc; i++) {
            robjDestroy(client->argv[i]);
        }
    }
    free(client);
}
void closeClients()
{
    listNode* node = listHead(server->clientsToClose);
    while (node) {
        redisClient* client = node->value;
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
 * @brief sentinel定时任务10s
 * 
 * @param [in] eventLoop 
 * @param [in] id 
 * @param [in] clientData 
 */
int sentinelInfoCron(struct aeEventLoop* eventLoop, long long id, void* clientData)
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
            aeCreateFileEvent(server->eventLoop, client->fd, AE_WRITABLE, sendReplyToClient, client);
            node = node->next;
        }
    }
    return 10000;
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
    
    log_info("√ server init finished.  %d.", server->role);


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
        aeCreateFileEvent(server->eventLoop, fd, AE_READABLE, readQueryFromClient, client);
        // 注册info定时任务
        aeCreateTimeEvent(server->eventLoop, 10000, sentinelInfoCron, NULL);
    
    }

}


redisCommand* lookupCommand( const char* name)
{
    dictIterator* iter = dictGetIterator(server->commands);
    dictEntry* entry;
    while((entry = dictIterNext(iter)) != NULL) {
        redisCommand* cmd = entry->v.val;
        assert(cmd);
        log_debug("looKUP command %s", cmd->name);
        if (strcasecmp(cmd->name, name) == 0) {
            dictReleaseIterator(iter);
            return cmd;
        }
    }
    dictReleaseIterator(iter);
    return NULL;
}
/**
 * @brief 调用执行命令。已有argc,argv[]
 * 
 * @param [in] c 
 */
void processCommand(redisClient * c)
{
    redisCommand* cmd;
    // argv[0] 一定是字符串，sds

    cmd = lookupCommand(((sds*)(c->argv[0]->ptr))->buf);
    if (cmd == NULL) {
        addWrite(c, shared.invalidCommand);
        return;
    }
    cmd->proc(c);
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

    sds* s = (sds*)(client->readBuf);
    int argc;
    char** argv;
    int ret = resp_decode(s->buf, &argc, &argv);
    assert(ret == 0);
    for(int i = 0; i < argc; i++) {
        robj* obj = robjCreateStringObject(argv[i]);
        client->argv = realloc(client->argv, sizeof(robj*) * (client->argc + 1));
        client->argv[client->argc] = obj;
        client->argc++;
    }
    sdsclear(client->readBuf); // 不需要，过程中已经读取完了
    processCommand(client);
}

void readQueryFromClient(aeEventLoop *el, int fd, void *privData)
{
    redisClient *client = (redisClient *)privData;
    char buf[1024] = {0};
    rio r;
    rioInitWithFD(&r, fd);
    size_t nread = rioRead(&r, buf, sizeof(buf));
    if (nread == 0)
    {
        if (r.error)
            close(fd);
        return;
    }
    log_debug("deal query from client, %s", respParse(buf));

    sdscat(client->readBuf, buf);
    log_debug(" processing query from client, %s", buf);
    processClientQueryBuf(client);
    log_debug(" processed query from client");
    // 写事件转移到各命令 自注册
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

    // 发送完毕转换状态,  不等client响应？
    client->replState = REPL_STATE_MASTER_CONNECTED;
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
        log_error("error writing %d ", fd);
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
}