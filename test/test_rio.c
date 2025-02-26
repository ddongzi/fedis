#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <assert.h>
#include "rio.h"

void test_file_rio() {
    FILE *fp = fopen("test.txt", "w+");
    assert(fp);
    rio file_rio;
    rioInitWithFile(&file_rio, fp);
    
    const char *data = "Hello, File RIO!";
    assert(rioWrite(&file_rio, data, strlen(data)) == strlen(data));
    rioFlush(&file_rio);
    fseek(fp, 0, SEEK_SET);
    
    char buf[50] = {0};
    assert(rioRead(&file_rio, buf, strlen(data)) == strlen(data));
    assert(strcmp(buf, data) == 0);
    
    // 测试异常情况
    assert(rioRead(NULL, buf, 10) == RIO_ERR_NULL);
    assert(rioWrite(NULL, data, 10) == RIO_ERR_NULL);
    
    fclose(fp);
    remove("test.txt");
    printf("File RIO test passed.\n");
}

void test_buffer_rio() {
    rio buffer_rio;
    sds* s = sdsempty();
    rioInitWithBuf(&buffer_rio, s);

    const char *data = "Hello, Buffer RIO!";
    assert(rioWrite(&buffer_rio, data, strlen(data)) == strlen(data));
    rioFlush(&buffer_rio);

    char buf[50] = {0};
    assert(rioRead(&buffer_rio, buf, strlen(data)) == strlen(data));

    // ��试异常情况
    assert(rioRead(NULL, buf, 10) == RIO_ERR_NULL);
    assert(rioWrite(NULL, data, 10) == RIO_ERR_NULL);
    
    sdsfree(s);
    printf("Buffer RIO test passed.\n");
}

void test_socket_rio() {
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    rio socket_rio;
    rioInitWithSocket(&socket_rio, sv[0]);
    
    const char *msg = "Socket Test";
    assert(rioWrite(&socket_rio, msg, strlen(msg)) == strlen(msg));
    
    char buf[50] = {0};
    assert(rioRead(&socket_rio, buf, strlen(msg)) == strlen(msg));
    assert(strcmp(buf, msg) == 0);
    
    // 测试异常情况
    assert(rioRead(NULL, buf, 10) == RIO_ERR_NULL);
    assert(rioWrite(NULL, msg, 10) == RIO_ERR_NULL);
    
    close(sv[0]);
    close(sv[1]);
    printf("Socket RIO test passed.\n");
}

void test_fd_rio() {
    int fd = open("test_fd.txt", O_RDWR | O_CREAT, 0644);
    assert(fd >= 0);
    rio fd_rio;
    rioInitWithFD(&fd_rio, fd);
    
    const char *data = "FD Test";
    assert(rioWrite(&fd_rio, data, strlen(data)) == strlen(data));
    rioFlush(&fd_rio);
    lseek(fd, 0, SEEK_SET);
    
    char buf[50] = {0};
    assert(rioRead(&fd_rio, buf, strlen(data)) == strlen(data));
    assert(strcmp(buf, data) == 0);
    
    // 测试异常情况
    assert(rioWrite(&fd_rio, NULL, 10) == RIO_ERR_NULL);
    assert(rioRead(&fd_rio, NULL, 10) == RIO_ERR_NULL);
    
    close(fd);
    remove("test_fd.txt");
    printf("FD RIO test passed.\n");
}

int main() {
    test_file_rio();
    test_buffer_rio();
    test_fd_rio();
    test_socket_rio();
    printf("All RIO tests passed successfully!\n");
    return 0;
}
