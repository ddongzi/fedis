/**
 * @file cns.c 处理具体 connection - server- client 关系
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2025-04-15
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "cns.h"
#include "log.h"
#include "client.h"

/**
 * @brief accept后创建 client, conn回调， client处理
 * 
 * @param [in] conn 
 */
void cnsAcceptHandler(Connection* conn)
{
    Client* c;
    if (conn->state != CONN_STATE_ACCEPTING) {
        log_error("CNS accept error, state is wrong");
        return;
    }
    conn->privData = clientCreate(conn);    

    conn->type->accept(conn, srvAcceptHandler);
}

