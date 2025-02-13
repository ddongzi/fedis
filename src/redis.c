#include "redis.h"
#include <strings.h>


struct redisServer* server;

// TODO 只能传入s现在
void addReply(redisClient* client, char* s) 
{
    sdscat(client->replyBuf, s);
}
void commandSetProc(redisClient* client)
{
    int retcode = dbAdd(client->db, client->argv[1], client->argv[2]);
    if (retcode == DICT_OK) {
        addReply(client, "+OK");
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
        addReply(client, "+OK");
    } else {
        addReply(client, "-ERR key not found");
    }
}

redisCommand commandsTable[] = {
    {"SET", commandSetProc, -3},
    {"GET", commandGetProc, 2},
    {"DEL", commandDelProc, 2},
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

    return 5000;
}



void initServer()
{
    initServerConfig(server);
    
    server->db = calloc(server->dbnum, sizeof(redisDb));
    for (int i = 0; i < server->dbnum; i++) {
        dbInit(server->db + i, i);
    }
    server->clients = listCreate();
    server->eventLoop = aeCreateEventLoop(server->maxclients);
    server->neterr = calloc(1024, sizeof(char));
    memset(server->neterr, 0, 1024);
    int fd = anetTcpServer(server->neterr, server->port, server->bindaddr, server->maxclients);

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
    free(c->argv);
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
            client->argv[client->argc - remain] = robjCreateString(s->buf, len);
            // 处理完一行
            sdsrange(s, p - s->buf + 2, s->len - 1);
            remain--;
        }
    }
    // sdsclear(client->queryBuf); // 不需要，过程中已经读取完了
    processCommand(client);
}
