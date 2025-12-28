/**
 * 测试事务 WATCH MULTI EXEC
 */
#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>


extern "C" {
#include "resp.h"
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include "sds.h"
}

class Client
{
public:
    Client(): sock(-1)
    {
    }
    ~Client()
    {
        if (sock != -1) close(sock);
    }
    bool connectServer(const std::string host, int port)
    {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr);
        return connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0;
    }
    void sendCommand(std::string command)
    {

        char buffer[1024];
        char* args[16]; // 最多16个参数
        int argc = 0;

        char* cmd = strdup(command.c_str());
        char* token = strtok(cmd, " ");
        while (token!= nullptr && argc < 16) {
            args[argc++] = token;
            token = strtok(nullptr, " ");
        }
        int len = snprintf(buffer, sizeof(buffer), "*%d\r\n", argc);
        for (int i = 0; i < argc; i++) {
            len += snprintf(buffer + len, sizeof(buffer) - len,
                "$%lu\r\n%s\r\n", strlen(args[i]), args[i]);
        }
        send(sock, buffer, len, 0);
        free(cmd);
    }
    std::string readResponse()
    {
        char buffer[1024]={0};
        int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';

            return buffer;
        }
        if (bytes_received == 0)
        {
            // 对端关闭
            std::cout << "Closed by peer" << std::endl;
            disconnectServer();
            return nullptr;
        }

        if (bytes_received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 阻塞超时，
                std::cout << "Connection timed out" << std::endl;
                return nullptr;
            } else
            {
                std::cout << "Receive err" << strerror(errno) << std::endl;
                disconnectServer();
                return nullptr;
            }
        }
    }
    void disconnectServer()
    {
        close(sock);
    }
private:
    long id;
    int sock;
};

struct ThreadArgs
{
    pthread_barrier_t* barrier;
    const char* serv_ip;
    int port;
};

void* thread_start(void* arg)
{
    ThreadArgs *targs = (ThreadArgs*)arg;
    Client client;
    if (!client.connectServer(targs->serv_ip, targs->port))
    {
        return nullptr;
    }
    // 同步点
    pthread_barrier_wait(targs->barrier);

    client.sendCommand("watch count");
    client.readResponse();

    client.sendCommand("get count");
    std::string response = client.readResponse();
    int current_count = atoi(response.c_str());

    client.sendCommand("multi");
    client.readResponse();

    std::string set_cmd = "set count " + std::to_string(current_count + 1);
    client.sendCommand(set_cmd);
    client.readResponse();

    client.sendCommand("exec");
    client.readResponse();

    // 如果正在watch是事务，就会失败
    return nullptr;
}


TEST(TransactionTest, t)
{
    // 10个客户端，同时执行。 初始count = 0, 预期count=1
    // WATCH count , GET count,
    // MULTI SET count count+1
    // EXEC
    const int threadNum = 10;
    pthread_t threads[threadNum];
    pthread_barrier_t barrier;
    pthread_barrier_init(&barrier, NULL, threadNum);

    ThreadArgs args = {&barrier, "127.0.0.1", 6666};

    //
    Client admin;
    admin.connectServer("127.0.0.1", 6666);
    admin.sendCommand("set count 0");
    admin.readResponse();

    for (int i = 0; i < threadNum; ++i)
    {
        pthread_create(&threads[i], NULL, thread_start, &args);
    }
    // 等待所有完成
    for (int i = 0; i < threadNum; ++i)
    {
        pthread_join(threads[i], NULL);
    }
    admin.sendCommand("get count");
    std::string response = admin.readResponse();
    pthread_barrier_destroy(&barrier);

    const std::string final_count = resp_str(response.c_str());
    EXPECT_EQ(final_count, "1");
}