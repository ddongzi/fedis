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
#include "log.h"
int connListen(ConnectionListener *listener)
{
    return listener->type->listen(listener);
}
ConnectionType* connGetConnType(int type)
{
    switch (type)
    {
    case SOCKET:
        return &CT_SOCKET;
        break;
    
    default:
        log_error("Connetion type unknown !!");
        break;
    }
}

Connection* connCreate(aeEventLoop* el, int type)
{
    return connGetConnType(type)->connCreate(el);
}

int connConnect(Connection* conn, char* ip, char* port, ConnectionCallbackFunc callback)
{
    return conn->type->connect(conn, ip, port, callback);
}

/**
 * @brief 
 * 
 * @param [in] conn 
 * @param [in] callback 
 * @return int 
 */
int connSetReadHandler(Connection* conn, ConnectionCallbackFunc callback)
{
    return conn->type->setReadHandler(conn, callback);
}
