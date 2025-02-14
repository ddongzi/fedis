#include "redis.h"
#include <strings.h>
#include <stdarg.h>
#include "rdb.h"
#include <signal.h>
#include <sys/wait.h>

struct redisServer* server;


void addReply(redisClient* client, const char* fmt, ...) 
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, 128, fmt, ap);
    va_end(ap);

    sdscat(client->replyBuf, buf);
}

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
        addReply(client, "+OK");
        server->dirty++;
    } else {
        addReply(client, "-ERR set error");
    }

}
void commandGetProc(redisClient* client)
{
    robj* res = (robj*)dbGet(client->db, client->argv[1]);
    if (res == NULL) { 
        addReply(client, "-ERR key not found");
    } else {
        
        addReply(client, ((sds*)(res->ptr))->buf);
    }
}
void commandDelProc(redisClient* client)
{
    int retcode = dbDelete(client->db, client->argv[1]);
    if (retcode == DICT_OK) {
        server->dirty++;

        addReply(client, "+OK");
    } else {
        addReply(client, "-ERR key not found");
    }
}
void commandObjectProc(redisClient* client)
{
    robj* key = client->argv[2];
    robj* op = client->argv[1];
    if (strcasecmp(((sds*)(op->ptr))->buf, "ENCODING") == 0) {
        robj* val = dbGet(client->db, key);
        if (val == NULL) {
            addReply(client, "-ERR key not found");
        } else {
            // TODO maybe we should use the valEncode() function
            char buf[1024];
            _encodingStr(val->encoding, buf, sizeof(buf));
            addReply(client, "+%s", buf);
        }
    }
}

void commandByeProc(redisClient* client)
{

    listAddNodeTail(server->clientsToClose, listCreateNode(client));
    addReply(client, "+bye");
}

redisCommand commandsTable[] = {
    {"SET", commandSetProc, -3},
    {"GET", commandGetProc, 2},
    {"DEL", commandDelProc, 2},
    {"OBJECT", commandObjectProc, 3},
    {"BYE", commandByeProc, 1}
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
    server = malloc(sizeof(struct redisServer));
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

    printf("√ init server config.\n");
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
    printf("free client %d\n", client->fd);
    close(client->fd);
    // TODO 如果 query reply buf还有怎么办？
    sdsfree(client->queryBuf);
    sdsfree(client->replyBuf);
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
    printf("clientsToClose!!! , size : %d\n", listSize(server->clientsToClose));
    while (node) {
        redisClient* client = node->value;
        listDelNode(server->clientsToClose, node);
        freeClient(client);
        node = listHead(server->clientsToClose);
    }

}
/**
 * @brief 定时任务
 * 
 * @param [in] eventLoop 
 * @param [in] id 
 * @param [in] clientData 
 * @return int 周期时间
 */
int serverCron(struct aeEventLoop* eventLoop, long long id, void* clientData)
{
    // TODO 
    printf("● server cron.\n");

    // TODO 检查SAVE条件，执行BGSAVE    
    bgSaveIfNeeded();

    // 更新server时间
    updateServerTime();

    // 关闭clients
    closeClients();

    return 5000;
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
                printf("server know %d finished\n", pid);
            }
            server->rdbChildPid = -1;
            server->dirty = 0;
            server->isBgSaving = 0;
        }
    }
}
void initServerSignalHandlers()
{
    signal(SIGCHLD, sigChildHandler);
}

void initServer()
{
    initServerConfig();
    initServerSignalHandlers();
    
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

    server->clients = listCreate();
    server->clientsToClose = listCreate();

    server->eventLoop = aeCreateEventLoop(server->maxclients);
    server->neterr = calloc(1024, sizeof(char));
    memset(server->neterr, 0, 1024);
    int fd = anetTcpServer(server->neterr, server->port, server->bindaddr, server->maxclients);
    if (fd == -1) {
        printf("NET Error: %s\n", server->neterr);
        exit(1);
    }
    printf("● create server, listening.....\n");
    
    aeCreateFileEvent(server->eventLoop, fd, AE_READABLE, acceptTcpHandler, NULL);
    printf("● create file event for ACCEPT, listening.....\n");
    // 注册定时任务
    aeCreateTimeEvent(server->eventLoop, 1000, serverCron, NULL);
    printf("● create time event for serverCron\n");

    printf("√ init server .\n");
}

redisClient *redisClientCreate(int fd)
{
    redisClient *c = malloc(sizeof(redisClient));
    c->fd = fd;
    c->flags = 0;
    c->queryBuf = sdsempty();
    c->replyBuf = sdsempty();
    c->dbid = 0;
    c->db = &server->db[c->dbid];
    c->argc = 0;
    c->argv = NULL;
    return c;

}
redisCommand* lookupCommand(const char* cmd)
{
    for (int i = 0; i < sizeof(commandsTable) / sizeof(commandsTable[0]); i++) {
        if (strcasecmp(commandsTable[i].name, cmd) == 0) {
            return &commandsTable[i];
        }
    }
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
        addReply(c, "Invalid command");
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
 *  *3\r\n
    $3\r\nSET\r\n
    $2\r\nk1\r\n
    $2\r\nv1\r\n
 * @param [in] client 
 */
void processClientQueryBuf(redisClient* client)
{
    if (client->queryBuf == NULL )  return;

    sds* s = (sds*)(client->queryBuf);
    while (sdslen(s)) {
        // 预期：只有一行，一轮
        if (client->argc  == 0) {
            // 解析 argc
            if (*(s->buf) != '*') {
                return; // 非预期
            }
            char* p = strchr(s->buf, '\n');
            client->argc = atoi(s->buf + 1);
            sdsrange(s, p - s->buf + 1, s->len - 1);    // 移除argc行，处理完
        }
        int remain = client->argc;
        client->argv = calloc(client->argc, sizeof(robj *));
        while (remain> 0) {
            char* p = strchr(s->buf, '\n') ; 
            int len = atoi(s->buf + 1);
            sdsrange(s, p -s->buf + 1, s->len - 1); // 移除len字段
            p = strchr(s->buf, '\r');
            *p = '\0';

            // TODO 如何知道要string 还是数字
            client->argv[client->argc - remain] = robjCreateStringObject(s->buf);
            // 处理完一行
            sdsrange(s, p - s->buf + 2, s->len - 1);
            remain--;
        }
    }
    // sdsclear(client->queryBuf); // 不需要，过程中已经读取完了
    processCommand(client);
}
