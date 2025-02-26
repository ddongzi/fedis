/**
 * @file rio.c
 * @author your name (you@domain.com)
 * @brief 统一 I/O 抽象，支持文件、内存、网络
 * @version 0.1
 * @date 2025-02-25
 * 
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>

#include "rio.h"
/* 基于文件io函数 */
size_t rioReadFromFile(rio* rio, void* buf, size_t len)
{
    FILE* fp = (FILE*)rio->data;
    return fread(buf, 1, len, fp);
}

size_t rioWriteToFile(rio* rio, const void* buf, size_t len)
{
    FILE* fp = (FILE*)rio->data;
    return fwrite(buf, 1, len, fp);
}

off_t rioTellFromFile(rio* rio)
{
    FILE* fp = (FILE*)rio->data;
    return ftell(fp);
}

void rioFlushToFile(rio* rio)
{
    FILE* fp = (FILE*)rio->data;
    fflush(fp);
}
/*基于内存sds的 rio函数*/
/**
 * @brief 从rio读取(前len字节) 最长len字节到 char*buf
 * 
 * @param [in] r 
 * @param [out] buf char*
 * @param [in] len 
 * @return size_t 读到的字节数
 */
size_t rioReadFromBuffer(rio *r, void *buf, size_t len) {
    sds *b = (sds *)r->data;
    size_t nread = len > sdslen(b) ? sdslen(b) : len;
    memcpy(buf, b->buf, nread);
    sdsrange(b, nread, sdslen(b));  // 
    return nread;
}
/**
 * @brief 向RIO追加写入len字节
 * 
 * @param [in] r 
 * @param [in] buf 字节buf
 * @param [in] len 
 * @return size_t nwritten
 * @note 不产生错误，因为动态扩展
 */
size_t rioWriteToBuffer(rio *r, const void *buf, size_t len) {
    sds *b = (sds *)r->data;
    sdscatlen(b, buf, len);
    return len;
}
off_t rioTellFromBuffer(rio *r) {
    // 内存缓冲区无需 tell
    return -1;  // 无法获取tell，返回-1
}
void rioFlushToBuffer(rio *r) {
    // 内存缓冲区无需刷新
}


/*基于网络的 rio*/
size_t rioReadFromSocket(rio *r, void *buf, size_t len) {
    int fd = *(int *)r->data;
    return recv(fd, buf, len, 0);
}
size_t rioWriteToSocket(rio *r, const void *buf, size_t len) {
    int fd = *(int *)r->data;
    return send(fd, buf, len, 0);
}
off_t rioTellFromSocket(rio *r) {
    // 网络流不支持 tell
    return -1;
}
void rioFlushToSocket(rio *r) {
    // 网络流通常不需要 flush
}
/*基于文件描述符的 rio*/
size_t rioReadFromFD(rio *r, void *buf, size_t len) {
    int fd = *(int *)r->data;  // 获取 fd
    return read(fd, buf, len);
}

size_t rioWriteToFD(rio *r, const void *buf, size_t len) {
    int fd = *(int *)r->data;  // 获取 fd
    return write(fd, buf, len);
}

off_t rioTellFromFD(rio *r) {
    int fd = *(int *)r->data;
    return lseek(fd, 0, SEEK_CUR);  // 获取当前位置
}

void rioFlushToFD(rio *r) {
    // 对于 fd，flush 通常无效，POSIX `write` 直接写入内核
}


void rioInitWithFile(rio *r, FILE *fp) {
    r->read = rioReadFromFile;
    r->write = rioWriteToFile;
    r->tell = rioTellFromFile;
    r->flush = rioFlushToFile;
    r->data = fp;
}

void rioInitWithBuf(rio *r, sds *buffer) 
{
    r->read = rioReadFromBuffer;
    r->write = rioWriteToBuffer;
    r->tell = rioTellFromBuffer;
    r->flush = rioFlushToBuffer;
    r->data = buffer;
}

void rioInitWithSocket(rio *r, int fd) {
    r->read = rioReadFromSocket;
    r->write = rioWriteToSocket;
    r->tell = rioTellFromSocket;
    r->flush = rioFlushToSocket;
    r->data = malloc(sizeof(int));
    *(int *)r->data = fd;
}
void rioInitWithFD(rio *r, int fd) {
    int *fd_ptr = malloc(sizeof(int));
    *fd_ptr = fd;
    
    r->read = rioReadFromFD;
    r->write = rioWriteToFD;
    r->tell = rioTellFromFD;
    r->flush = rioFlushToFD;
    r->data = fd_ptr;
}


size_t rioWrite(rio *r, const void *buf, size_t len)
{
    if (r == NULL || buf == NULL) {
        return RIO_ERR_NULL;
    }
    return r->write(r, buf, len);
}
size_t rioRead(rio *r, void *buf, size_t len)
{
    if (r == NULL || buf == NULL) {
        return RIO_ERR_NULL;
    }
    return r->read(r, buf, len);
}
off_t rioTell(rio *r)
{
    if (r == NULL ) {
        return RIO_ERR_NULL;
    }
    return r->tell(r);
}
void rioFlush(rio *r)
{
    if (r == NULL ) {
        return ;
    }
    r->flush(r);
}
