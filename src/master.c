#include "master.h"
#include "slave.h"
#include "log.h"
#include <fcntl.h>
#include <sys/stat.h>
#include "server.h"
#include <signal.h>

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

void masterInit()
{
    initServerSignalHandlers();

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
    log_debug("● load rdb from %s", server->rdbFileName);
    rdbLoad(master->db, master->dbnum, master->rdbFileName);
}
// master才支持bgsave
void appendServerSaveParam(time_t sec, int changes)
{
    master->saveParams = realloc(master->saveParams, sizeof(struct saveparam) * (master->saveCondSize + 1));
    master->saveParams[master->saveCondSize].seconds = sec;
    master->saveParams[master->saveCondSize].changes = changes;
    master->saveCondSize++;
}
void masterInitConfig()
{
    master->dbnum = REDIS_DEFAULT_DBNUM;
    master->saveCondSize = 0;
    master->saveParams = NULL;
    appendServerSaveParam(900, 1);
    appendServerSaveParam(300, 10000);
    appendServerSaveParam(10, 1);
    server->rdbFileName = "/home/dong/fedis/data/1.rdb";
}

void bgsave()
{
    pid_t pid = fork();
    if (pid == 0) {
        rdbSave();
        exit(0);
    } else if (pid < 0) {
        log_debug("Error: fork failed");
    }
    master->isBgSaving = 1;
    master->rdbChildPid = pid;
}

void bgSaveIfNeeded()
{
    // 检查是否在BGSAVE
    if (master->isBgSaving) {
        log_debug("BGSAVE is running, no need....\n");
        return;
    }

    for(int i = 0; i < master->saveCondSize; i++) {
        time_t interval = time(NULL) - master->lastSave;
        if (interval >= master->saveParams[i].seconds && 
            master->dirty >= master->saveParams[i].changes
        ) {
            log_debug("CHECK BGSAVE OK, %d, %d\n", interval, master->dirty);
            bgsave();
            break;
        }
    }
}
/**
 * @brief 主读取RDB文件发送到slave, 切换fd写事件处理为 sendRDB
 *
 * @param [in] client
 */
void masterRDBToSlave(Connection* conn)
{
    Client* client = conn->privData;
    struct stat st;
    long rdb_len;
    char length_buf[64];
    size_t length_len;
    size_t nwritten = 0;
    if (master->rdbfd == -1)
    {
        master->rdbfd = open(master->rdbFileName, O_RDONLY);
        if (master->rdbfd == -1)
        {
            log_error("Open RDB file: %s failed: %s", master->rdbFileName, strerror(errno));
            return;
        }
    }
    {
        if (fstat(master->rdbfd, &st) == -1)
        log_error("Stat RDB file: %s failed: %s", master->rdbFileName, strerror(errno));
        return;
    }

    rdb_len = st.st_size;
    log_debug("Stat RDB file , size: %d", st.st_size);

    // 发送 $length\r\n
    sprintf(length_buf, "$%lu\r\n", rdb_len);
    length_len = strlen(length_buf);
    nwritten = connWrite(conn, length_buf, length_len);
    log_debug("send RDB length: %s", length_buf);
    if (nwritten <= 0 || nwritten != length_len) {
        log_error("rdb length write failed");
    }

    // 发送 RDB 数据, 通过sendfile,
    off_t offset = 0;
    ssize_t sent = sendfile(conn->fd, master->rdbfd, &offset, rdb_len);
    if (sent < 0)
    {
        log_error("Failed to send RDB FILE to client %d: %s", conn->fd, strerror(errno));
        return;
    }

    // 发送完毕转换状态,  不等client响应？
    SlaveClientInstance* instance = client->privdata;
    instance->replState = REPL_STATE_MASTER_CONNECTED;
    log_debug("RDB sent to slave %d， size:%lld", conn->fd, rdb_len);
}