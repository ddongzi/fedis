#include <gtest/gtest.h>
extern "C" {
#include <string.h>
#include "resp.h"
#include "log.h"
#include "redis.h"
}

TEST(CommandTest, Set)
{
    // 构建一个为客户端伪劣客户端，不需要server
}