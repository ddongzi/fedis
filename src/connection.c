/**
 * @file connection.c
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-04-16
 * 
 * @copyright Copyright (c) 2025
 * 
 */


#include "connection.h"

int connListen(ConnectionListener* listener)
{
    return listener->type->listen(listener);
}
