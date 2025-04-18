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
int connRead(Connection* conn, void* buf, size_t buflen)
{
    return conn->type->read(conn, buf, buflen);
}
int connWrite(Connection* conn, const void* data, size_t datalen)
{
    return conn->type->write(conn, data, datalen);
}
ConnectionType* connGetConnType(int type)
{
    switch (type)
    {
    case TYPE_SOCKET:
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
 * @brief 如果callback空，那就是 delete
 * 
 * @param [in] conn 
 * @param [in] writeHandler 
 * @return int 
 */
int connSetWriteHandler(Connection* conn, ConnectionCallbackFunc writeHandler)
{
    return conn->type->setWriteHandler(conn, writeHandler);
}
/**
 * @brief 
 * 
 * @param [in] conn 
 * @param [in] readHandler 
 * @return int 
 */
int connSetReadHandler(Connection* conn, ConnectionCallbackFunc readHandler)
{
    return conn->type->setReadHandler(conn, readHandler);
}

void connClose(Connection* conn)
{
    conn->type->connClose(conn);
}
