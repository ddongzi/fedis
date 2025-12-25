#include <gtest/gtest.h>

#include "sds.h"

extern "C" {
#include <string.h>
#include "log.h"
#include "robj.h"
}
TEST(RobjTest, StringEncoding)
{
    robj* o;
    sds* s;
    // long型str
    o = robjCreateStringObject("12");
    EXPECT_EQ(o->encoding, REDIS_ENCODING_INT);
    EXPECT_EQ(o->type, REDIS_STRING);
    EXPECT_EQ((long)o->ptr, 12);
    robjDestroy(o);

    // long： 5字节
    o = robjCreateStringObject("0x1111111111");
    EXPECT_EQ(o->encoding, REDIS_ENCODING_INT);
    EXPECT_EQ(o->type, REDIS_STRING);
    EXPECT_EQ((long)o->ptr, 0x1111111111);
    robjDestroy(o);

    // long 越界：9字节. 表示为嵌入字符串
    o = robjCreateStringObject("0x111111111111111111");
    EXPECT_EQ(o->encoding, REDIS_ENCODING_EMBSTR);
    EXPECT_EQ(o->type, REDIS_STRING);
    s = (sds*)o->ptr;
    EXPECT_STREQ(s->buf, "0x111111111111111111");
    robjDestroy(o);

    // 嵌入字符串: 31字节
    o = robjCreateStringObject("2helloworldhelloworldhelloworld");
    EXPECT_EQ(o->encoding, REDIS_ENCODING_EMBSTR);
    EXPECT_EQ(o->type, REDIS_STRING);
    s = (sds*)o->ptr;
    EXPECT_STREQ(s->buf, "2helloworldhelloworldhelloworld");
    robjDestroy(o);

    // raw字符串: 32字节
    o = robjCreateStringObject("12helloworldhelloworldhelloworld");
    EXPECT_EQ(o->encoding, REDIS_ENCODING_RAW);
    EXPECT_EQ(o->type, REDIS_STRING);
    s = (sds*)o->ptr;
    EXPECT_STREQ(s->buf, "12helloworldhelloworldhelloworld");
    robjDestroy(o);
}
