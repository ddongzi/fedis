#include <gtest/gtest.h>
extern "C" {
#include <string.h>
#include "resp.h"
#include "log.h"
}
TEST(Resptest, HandleArrayString)
{
    // 正常
    char* s= resp_str("*2\r\n$3\r\nGET\r\n$3\r\nsex\r\n");
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "[GET,sex,]");

    // *不完整
    s= resp_str("*");
    EXPECT_EQ(s, nullptr);

    // 空数组
    s= resp_str("*0\r\n");
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "[]");

    // null数组
    s= resp_str("*-1\r\n");
    EXPECT_STREQ(s, "NULL");

    // 非法数组
    s= resp_str("*-2\r\n");
    EXPECT_EQ(s, nullptr);

    // 后续空缺。会越界
    s= resp_str("*1\r\n");
    EXPECT_EQ(s, nullptr);

    // 后续空缺，越界
    s = resp_str("*2\r\n$3\r\nGET\r\n");
    EXPECT_EQ(s, nullptr);

    // 内容错误
    s = resp_str("*1\r\n*1\r\nGET\r\n");
    EXPECT_EQ(s, nullptr);

    // 内容错误
    s = resp_str("*1\r\n$1\r\nGET\r\n");
    EXPECT_EQ(s, nullptr);

    // 内容错误，
    s = resp_str("*1\r\n$1\r\nGET");
    EXPECT_EQ(s, nullptr);

    // sscanf非法字符
    s = resp_str("*a\r\n$1\r\nGET");
    EXPECT_EQ(s, nullptr);

    s = resp_str("*1\r\n$ \r\nGET");
    EXPECT_EQ(s, nullptr);

    free(s);
}
TEST(Resptest, HandleString)
{
    char* s = resp_str("$3\r\nsex\r\n");
    EXPECT_STREQ(s, "sex");

    s = resp_str("$-2\r\n");
    EXPECT_EQ(s, nullptr);

    s = resp_str("$-1\r\n");
    EXPECT_STREQ(s, "NULL");

    s = resp_str("$3\r\n");
    EXPECT_EQ(s, nullptr);

    s = resp_str("$a\r\n");
    EXPECT_EQ(s, nullptr);

    free(s);
}
TEST(Resptest, HandleInteger)
{
    char* s = resp_str(":10\r\n");
    EXPECT_STREQ(s, "10");

    s = resp_str(":");
    EXPECT_EQ(s, nullptr);

    s = resp_str(":\r\n");
    EXPECT_EQ(s, nullptr);
    free(s);
}
TEST(Resptest, HandleSimpleString)
{
    char* s = resp_str("+ok\r\n");
    EXPECT_STREQ(s, "ok");

    s = resp_str("+");
    EXPECT_EQ(s, nullptr);

    s = resp_str("+\r\n");
    EXPECT_EQ(s, nullptr);

    s = resp_str("-ERR: duplicate key\r\n");
    EXPECT_STREQ(s, "ERR: duplicate key");
    free(s);
}
TEST(Resptest, EncodeArrayString)
{
    int argc = 1;
    char* argv[8];

    argv[0] = "hello";
    char *s =respEncodeArrayString(argc, argv);
    EXPECT_STREQ(s, "*1\r\n$5\r\nhello\r\n");

    argc = 2;
    argv[1] = "world";
    s = respEncodeArrayString(argc, argv);
    EXPECT_STREQ(s, "*2\r\n$5\r\nhello\r\n$5\r\nworld\r\n");

    free(s);
}

TEST(Resptest, EncodeBulkString)
{
    char *s =respEncodeBulkString("hello");
    EXPECT_STREQ(s, "$5\r\nhello\r\n");

    free(s);
}