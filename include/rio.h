/**
 * @file rio.h
 * @author your name (you@domain.com)
 * @brief 统一 I/O 抽象，支持文件、内存、网络。提供读写、定位、flush
 * @version 0.1
 * @date 2025-02-25
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef RIO_H
#define RIO_H
#include <stdlib.h>
#include <stdio.h>
#include "sds.h"
// TODO 错误码只打印 或 内部属性，不作为返回值，不然影响语义    
enum {
    RIO_OK = 0,       // 无错误
    RIO_ERR_NULL,     // 传入 NULL 指针
    RIO_ERR_OPEN,     // 文件打开失败
    RIO_ERR_READ,     // 读取失败
    RIO_ERR_WRITE,    // 写入失败
    RIO_ERR_TELL,     // `lseek` 失败
    RIO_ERR_CLOSE     // 关闭失败
};

typedef struct rio {
    ssize_t (*read)(struct rio *r, void *buf, size_t len);    // 读操作
    ssize_t (*write)(struct rio *r, const void *buf, size_t len); // 写操作
    off_t (*tell)(struct rio *r);  // 当前文件偏移
    void (*flush)(struct rio *r);  // 刷新
    void *data;  // 数据源（File*, sds* , int fd, int socket）
    int error;  // 错误码，非0表示有错误。
}rio;

// 向rio中写入 len长度的buf
size_t rioWrite(rio *r, const void *buf, size_t len);
// 从rio读出 最大len长度的buf
size_t rioRead(rio *r, void *buf, size_t len);
off_t rioTell(rio *r);
// flush 的含义并不是清空缓冲区，而是确保数据被提交到目标（磁盘、网络、文件描述符等）。
void rioFlush(rio *r);

void rioInitWithFile(rio *r, FILE *fp);   ///< 普通文件IO。 fread, fwrite,
void rioInitWithBuf(rio *r, sds *buf);    ///< 内存IO
void rioInitWithSocket(rio *r, int socket);   ///< 网络IO，  send,recv
void rioInitWithFD(rio *r, int fd);   //< 其他文件IO ,write,read
#endif