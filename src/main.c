#include "redis.h"


int main(int argc, char **argv)
{
    initServerConfig();

    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            server->port = atoi(argv[i + 1]);
            i++;
        }
    }
    
    initServer();
    aeMain(server->eventLoop);

    return 0;
}
