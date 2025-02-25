/**
 * @file io.h
 * @author your name (you@domain.com)
 * @brief 统一 I/O 抽象，支持文件、内存、网络。对应注册不同的操作函数
 * @version 0.1
 * @date 2025-02-25
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef IO_H
#define IO_H
#include <stdlib.h>

enum {
    IO_OK = 0,       // 无错误
    IO_ERR_NULL,     // 传入 NULL 指针
    IO_ERR_OPEN,     // 文件打开失败
    IO_ERR_READ,     // 读取失败
    IO_ERR_WRITE,    // 写入失败
    IO_ERR_SEEK,     // `lseek` 失败
    IO_ERR_CLOSE     // 关闭失败
}

typedef struct io {
    int (*read)(struct io *r, void *buf, size_t len);    // 读操作
    int (*write)(struct io *r, const void *buf, size_t len); // 写操作
    off_t (*tell)(struct io *r);  // 当前文件偏移
    void (*flush)(struct io *r);  // 刷新
    void *data;  // 数据源（File*, ioBuffer* , int fd, int socket）
} io;
// 内存buffer封装
typedef struct {
    char *buffer;
    size_t pos;
    size_t size;
} ioBuffer;

size_t ioWrite(io *r, const void *buf, size_t len);
size_t ioRead(io *r, void *buf, size_t len);
off_t ioTell(io *r);
void ioFlush(io *r);

void ioInitWithFile(io *r, FILE *fp);   ///< 普通文件IO。 fread, fwrite,
void ioInitWithBuf(io *r, char *buf, size_t size);    ///< 内存IO
void ioInitWithSocket(io *r, int socket);   ///< 网络IO，  send,recv
void ioInitWithFD(io *r, int fd);   //< 其他文件IO ,write,read
#endif