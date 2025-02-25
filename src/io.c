/**
 * @file io.c
 * @author your name (you@domain.com)
 * @brief 统一 I/O 抽象，支持文件、内存、网络
 * @version 0.1
 * @date 2025-02-25
 * 
 */

#include "io.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

/* 基于文件io函数 */
size_t ioReadFromFile(io* io, void* buf, size_t len)
{
    FILE* fp = (FILE*)io->data;
    return fread(buf, 1, len, fp);
}

size_t ioWriteToFile(io* io, const void* buf, size_t len)
{
    FILE* fp = (FILE*)io->data;
    return fwrite(buf, 1, len, fp);
}

off_t ioTellFromFile(io* io)
{
    FILE* fp = (FILE*)io->data;
    return ftell(fp);
}

void ioFlushToFile(io* io)
{
    FILE* fp = (FILE*)io->data;
    fflush(fp);
}
/*基于内存的 io函数*/


size_t ioReadFromBuffer(io *r, void *buf, size_t len) {
    ioBuffer *b = (ioBuffer *)r->data;
    if (b->pos + len > b->size) len = b->size - b->pos; // 防止越界
    memcpy(buf, b->buffer + b->pos, len);
    b->pos += len;
    return len;
}

size_t ioWriteToBuffer(io *r, const void *buf, size_t len) {
    ioBuffer *b = (ioBuffer *)r->data;
    if (b->pos + len > b->size) return 0; // 缓冲区溢出
    memcpy(b->buffer + b->pos, buf, len);
    b->pos += len;
    return len;
}

off_t ioTellFromBuffer(io *r) {
    ioBuffer *b = (ioBuffer *)r->data;
    return b->pos;
}

void ioFlushToBuffer(io *r) {
    // 内存缓冲区无需刷新
}
/*基于网络的 io*/
size_t ioReadFromSocket(io *r, void *buf, size_t len) {
    int fd = *(int *)r->data;
    return recv(fd, buf, len, 0);
}
size_t ioWriteToSocket(io *r, const void *buf, size_t len) {
    int fd = *(int *)r->data;
    return send(fd, buf, len, 0);
}
off_t ioTellFromSocket(io *r) {
    // 网络流不支持 tell
    return -1;
}
void ioFlushToSocket(io *r) {
    // 网络流通常不需要 flush
}
/*基于文件描述符的 io*/
size_t ioReadFromFD(io *r, void *buf, size_t len) {
    int fd = *(int *)r->data;  // 获取 fd
    return read(fd, buf, len);
}

size_t ioWriteToFD(io *r, const void *buf, size_t len) {
    int fd = *(int *)r->data;  // 获取 fd
    return write(fd, buf, len);
}

off_t ioTellFromFD(io *r) {
    int fd = *(int *)r->data;
    return lseek(fd, 0, SEEK_CUR);  // 获取当前位置
}

void ioFlushToFD(io *r) {
    // 对于 fd，flush 通常无效，POSIX `write` 直接写入内核
}


void ioInitWithFile(io *r, FILE *fp) {
    r->read = ioReadFromFile;
    r->write = ioWriteToFile;
    r->tell = ioTellFromFile;
    r->flush = ioFlushToFile;
    r->data = fp;
}

void ioInitWithBuf(io *r, char *buffer, size_t size) {
    ioBuffer *b = malloc(sizeof(ioBuffer));
    b->buffer = buffer;
    b->pos = 0;
    b->size = size;
    
    r->read = ioReadFromBuffer;
    r->write = ioWriteToBuffer;
    r->tell = ioTellFromBuffer;
    r->flush = ioFlushToBuffer;
    r->data = b;
}

void ioInitWithSocket(io *r, int fd) {
    r->read = ioReadFromSocket;
    r->write = ioWriteToSocket;
    r->tell = ioTellFromSocket;
    r->flush = ioFlushToSocket;
    r->data = malloc(sizeof(int));
    *(int *)r->data = fd;
}
void ioInitWithFD(io *r, int fd) {
    int *fd_ptr = malloc(sizeof(int));
    *fd_ptr = fd;
    
    r->read = ioReadFromFD;
    r->write = ioWriteToFD;
    r->tell = ioTellFromFD;
    r->flush = ioFlushToFD;
    r->data = fd_ptr;
}


size_t ioWrite(io *r, const void *buf, size_t len)
{
    return r->write(r, buf, len);
}
size_t ioRead(io *r, void *buf, size_t len)
{
    return r->read(r, buf, len);
}
off_t ioTell(io *r)
{
    return r->tell(r);
}
void ioFlush(io *r)
{
    r->flush(r);
}
