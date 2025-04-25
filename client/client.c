#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <strings.h>

#define BUFFER_SIZE 1024

// 连接 Redis 服务器
int connect_to_redis(const char *host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return -1;
    }
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        return -1;
    }
    return sock;
}

// 发送 Redis 命令（符合 RESP 协议）
void send_command(int sock, const char *command) 
{
    char buffer[BUFFER_SIZE];
    char* args[16]; // 最多16个参数
    int argc = 0;

    char* cmd = strdup(command);
    char* token = strtok(cmd, " ");
    while (token!= NULL && argc < 16) {
        args[argc++] = token;
        token = strtok(NULL, " ");
    }
    int len = snprintf(buffer, BUFFER_SIZE, "*%d\r\n", argc);
    for (int i = 0; i < argc; i++) {
        len += snprintf(buffer + len, BUFFER_SIZE - len, "$%d\r\n%s\r\n", strlen(args[i]), args[i]);
    }
    printf("%s\n", buffer);
    send(sock, buffer, len, 0);
    free(cmd);
}

// 读取 Redis 响应
int read_response(int sock) {
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("<<<: %s\n", buffer);
        if (strcmp(buffer, "+bye" ) == 0) {
            return 1;
        }
    } else {
        printf("Failed to receive response from Redis.\n");
    }
    return 0;  // 0表示正常读取，1表示断开连接
}

// ./a.out --port 6667
int main(int argc, char* argv[]) {
    const char* redis_host = "127.0.0.1";
    int redis_port ;

    for (size_t i = 0; i < argc; i++)
    {
        if (strcasecmp("--port", argv[i]) == 0 && i + 1 < argc ) {
            redis_port = atoi( argv[i+1]);
            break;
        }
    }
    printf("Connect-> %s:%d\n", redis_host, redis_port);
    

    int sock = connect_to_redis(redis_host, redis_port);
    if (sock < 0) return 1;
    
    char command[BUFFER_SIZE];
    while (1) {
        printf(">>>: ");
        fgets(command, BUFFER_SIZE, stdin);
        command[strcspn(command, "\n")] = 0;  // 移除换行符
        
        if (strcmp(command, "exit") == 0) break;
        send_command(sock, command);
        if (read_response(sock) == 1) {
            break;  // 断开连接时，主循环结束
        }
    }
    
    close(sock);
    return 0;
}
