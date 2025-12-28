#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <strings.h>
#include <sys/time.h>
#include <errno.h>
#include "resp.h"
#include "linenoise.h"
#include "sds.h"
#define BUFFER_SIZE 1024

const char* redis_host = "127.0.0.1";
int redis_port = 6666;
int sock;

// 连接 Redis 服务器
int connect_to_redis(const char *host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return -1;
    }
    printf("~~~ Connecting to [%d]%s:%d\n", sock, host, port);
    
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
    send(sock, buffer, len, 0);
    free(cmd);
}

// 读取 Redis 响应
// -1 关闭client， 1 异常读取， 0正常
int read_response(int sock) {
    char buffer[BUFFER_SIZE];
    int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        char* endptr;
        sds* sbuf = sdsnew(buffer);
        char buf[512] = {0};
        while (( endptr = respParse(sbuf->buf, sbuf->len)) != NULL)
        {
            memcpy(buf, sbuf->buf, endptr - sbuf->buf + 1);
            printf("<<< %s\n", resp_str(buf));
            sdsrange(sbuf, endptr - sbuf->buf + 1, sdslen(sbuf) - 1);
        }
        if (strstr(buffer, "+bye" ) != NULL) {
            return -1;
        }
        return 0;
    } else if (bytes_received == 0)
    {
        // 对端关闭
        printf("Connection closed by peer.\n");
        return -1;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // 阻塞超时，
            return 0;
        } else {
            // 真正错误
            perror("recv failed");
            close(sock);
            sock = connect_to_redis(redis_host, redis_port);
            return 1;
        }
    }
    
}

void completion(const char* buf, linenoiseCompletions* lc)
{
    if (!strncasecmp(buf, "object", 6))
    {
        linenoiseAddCompletion(lc, "object encoding");
    }
    if (!strncasecmp(buf, "s", 1))
    {
        linenoiseAddCompletion(lc, "set");
    }
    if (!strncasecmp(buf, "g", 1))
    {
        linenoiseAddCompletion(lc, "get");
    }
}
char *hints(const char *buf, int *color, int *bold) {
    if (!strcasecmp(buf, "SET")) {
        *color = 35; // 紫色
        return " <key> <value>";
    }
    if (!strcasecmp(buf, "get")) {
        *color = 35; // 紫色
        return " <key>";
    }
    return NULL;
}

/**
 *
 * @param cmd
 * @return 响应
 */
void handleInterCommand(char* cmd)
{
    if (strncasecmp(cmd, "/help", 5) == 0)
    {
        printf("set k v\n");
        printf("get k\n");
        printf("object encoding k\n");
        printf("expire k\n");
    }
}


int main(int argc, char* argv[])
{
    char* line;
    // TAB
    linenoiseSetCompletionCallback(completion);
    // 提示
    linenoiseSetHintsCallback(hints);

    // history file
    linenoiseHistoryLoad("history.txt");

    sock = connect_to_redis(redis_host, redis_port);
    if (sock < 0) return 1;

    while (1)
    {
        line = linenoise(">>> ");
        if (line == NULL) break;
        if (line[0] == '\0')
        {
            free(line);
            continue;
        }
        // 内部指令
        if (line[0] == '/')
        {
            handleInterCommand(line);
        } else
        {
         // fedis命令
            linenoiseHistoryAdd(line);
            linenoiseHistorySave("history.txt");

            send_command(sock, line);
            if (read_response(sock) == -1) { // bye
                break;  // 断开连接时，主循环结束
            }
        }
        free(line);
    }
    close(sock);
    return 0;
}
