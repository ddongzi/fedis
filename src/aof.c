#include "aof.h"
#include "conf.h"
#include "redis.h"
#include <stdio.h>
#include "log.h"
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "util.h"
#include <pthread.h>
#include <stdlib.h>

#include "resp.h"

/**
 * 服务器启动时候，通过fakeclient读取aof执行
 */
void aof_load()
{
    log_info("Aof load...");
    redisClient* fkc = redisFakeClientCreate();
    // load aof 到 readbuf
    struct AOF* aof = &(server->aof);
    char buf[1024] = {0};
    sds* sbuf = sdsempty();
    int nread;
    while ((nread = read(aof->fd, buf , sizeof(buf))) > 0)
    {
        // 由于aof巨大，很可能一个resp就很长，导致超越一个buf，所以先使用sds
        sdscatlen(sbuf, buf, nread);
    }

    // 从buf中读取一些完整的resp
    char* endptr;
    while (( endptr = respParse(sbuf->buf, sbuf->len)) != NULL)
    {
        sdscatlen(fkc->readBuf, sbuf->buf, endptr - sbuf->buf + 1);
        // 找到了一个有效的resp
        processClientQueryBuf(fkc);
        sdsrange(sbuf, endptr - sbuf->buf + 1, sdslen(sbuf) - 1);
    }

}

void aof_init()
{
    struct AOF* aof = &(server->aof);
    aof->active_buf = sdsempty();
    aof->io_buf = sdsempty();
    pthread_mutex_init(&aof->aof_mutex, NULL);
    pthread_cond_init(&aof->aof_cond, NULL);
    aof->isAofing = false;
    aof->lastAofTime = 0;

    char* aofFileName = get_config("aof_file");
    FILE* fp = fopen(fullPath(aofFileName), "a+");
    if (fp == NULL) {
        log_error("Aof (%s) open failed %s", aofFileName ,strerror(errno));
        return ;
    }
    aof->fd = fileno(fp);

    pthread_t aof_thread;
    int err = pthread_create(&aof_thread, NULL, erverySecAOF, NULL);
    if (err != 0) {
        log_error("Fatal: can't create AOF thread: %s\n", strerror(err));
    }
    pthread_detach(aof_thread);
    log_info("Aof thread : [%zu]", aof_thread);
}

bool needAOF() {
    struct AOF* aof = &(server->aof);
    if (aof->isAofing) return false;
    
    char* aofAction = get_config("appendfsync");
    if (strcmp(aofAction, "everysec") == 0) {
        if (mstime() - aof->lastAofTime > 1000) {
            if (sdslen(aof->active_buf) > 0) 
                return true;
        }
    }
    return false;
}

// everysec有一个常驻线程处理
void* erverySecAOF(void* arg)
{
    struct AOF* aof = &(server->aof);
    while (true)
    {
        pthread_mutex_lock(&aof->aof_mutex);
        while (!needAOF())
        {
            pthread_cond_wait(&aof->aof_cond, &aof->aof_mutex);
        }
        aof->isAofing = true;
        sds* tmpbuf = aof->active_buf; // 主线程加入的. 现在清空，刷入iobuf
        aof->active_buf = aof->io_buf; // iobuf一定是空的。
        aof->io_buf = tmpbuf;
        pthread_mutex_unlock(&aof->aof_mutex);

        log_info("AOF start..");
        // do io
        ssize_t nwritten = write(aof->fd, aof->io_buf->buf, aof->io_buf->len);
        if (nwritten <= 0)
            log_error("aof write error !");
        fsync(aof->fd);
        sdsclear(aof->io_buf);
        log_info("AOF finished, written %u bytes", nwritten);

        pthread_mutex_lock(&aof->aof_mutex);
        aof->isAofing = false;
        pthread_mutex_unlock(&aof->aof_mutex);
    }
}

void flushAppendOnlyFile()
{
    pthread_cond_signal(&server->aof.aof_cond);
}


