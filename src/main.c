#include "redis.h"


int main()
{
    initServer();
    aeMain(server->eventLoop);

    return 0;
}
