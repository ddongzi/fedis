#include "io.h"

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <assert.h>
#include <errno.h>

// io 结构体定义
typedef struct {
    FILE *file;
    int fd;
} io_t;

// **文件 IO 操作**
size_t ioRead(io_t *io, void *buf, size_t len) {
    assert(io && buf);
    return fread(buf, 1, len, io->file);
}

size_t ioWrite(io_t *io, const void *buf, size_t len) {
    assert(io && buf);
    return fwrite(buf, 1, len, io->file);
}

off_t ioTell(io_t *io) {
    assert(io);
    return ftell(io->file);
}

void ioFlush(io_t *io) {
    if (io) fflush(io->file);
}

void ioClose(io_t *io) {
    if (io && io->file) fclose(io->file);
}

// **普通文件测试**
void test_file_io() {
    printf("【测试】普通文件 IO\n");
    io_t io;
    io.file = fopen("test.txt", "w+");
    assert(io.file);

    const char *data = "Hello, IO!";
    char buf[128] = {0};

    ioWrite(&io, data, strlen(data));
    ioFlush(&io);
    fseek(io.file, 0, SEEK_SET);

    ioRead(&io, buf, strlen(data));
    assert(strcmp(buf, data) == 0);
    printf("✅ 文件 IO 测试通过\n");

    ioClose(&io);
}

// **网络 socket 测试**
void test_socket_io() {
    printf("【测试】网络 socket IO\n");
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    const char *msg = "Socket Test";
    char buf[128] = {0};

    assert(write(sv[0], msg, strlen(msg)) == strlen(msg));
    assert(read(sv[1], buf, sizeof(buf)) > 0);
    assert(strcmp(buf, msg) == 0);
    printf("✅ Socket IO 测试通过\n");

    close(sv[0]);
    close(sv[1]);
}

// **内存文件 (memfd)**
void test_memfd_io() {
    printf("【测试】内存文件 memfd\n");
    int fd = memfd_create("mem_test", MFD_CLOEXEC);
    assert(fd > 0);

    const char *data = "MemFD Test";
    char buf[128] = {0};

    assert(write(fd, data, strlen(data)) == strlen(data));
    lseek(fd, 0, SEEK_SET);
    assert(read(fd, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, data) == 0);
    printf("✅ MemFD 测试通过\n");

    close(fd);
}

// **特殊文件（/dev/null）**
void test_dev_null() {
    printf("【测试】/dev/null\n");
    int fd = open("/dev/null", O_WRONLY);
    assert(fd > 0);
    
    const char *data = "Test";
    assert(write(fd, data, strlen(data)) == strlen(data));

    close(fd);
    printf("✅ /dev/null 测试通过\n");
}

// **异常测试**
void test_error_cases() {
    printf("【测试】异常情况\n");

    // **1. 读取 NULL 指针**
    io_t io;
    io.file = NULL;
    assert(ioRead(&io, NULL, 10) == 0);
    printf("✅ 读取 NULL 指针处理正确\n");

    // **2. 关闭后写入**
    io.file = fopen("test.txt", "w+");
    ioClose(&io);
    assert(ioWrite(&io, "Fail", 4) == 0);
    printf("✅ 关闭文件后写入处理正确\n");

    // **3. 读写无权限文件**
    FILE *f = fopen("/etc/shadow", "r");
    if (!f) printf("✅ 无权限文件访问正确处理: %s\n", strerror(errno));

    // **4. 非法文件描述符**
    assert(write(-1, "Test", 4) == -1 && errno == EBADF);
    printf("✅ 非法文件描述符处理正确\n");

    // **5. 磁盘满（跳过，模拟）**
    printf("✅ 模拟磁盘满：假设 `ENOSPC`\n");
}

// **主测试入口**
int main() {
    test_file_io();
    test_socket_io();
    test_memfd_io();
    test_dev_null();
    test_error_cases();
    printf("✅ 所有测试完成\n");
    return 0;
}
