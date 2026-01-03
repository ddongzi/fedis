#include <gtest/gtest.h>
extern "C" {
#include <string.h>
#include "ringbuffer.h"
}

class RingBufferTest:public::testing::Test
{
protected:
    RingBuffer* rb;
    void SetUp() override {
        rb = ringBufferCreate();
        ASSERT_NE(rb, nullptr);
    }
    void TearDown() override {
        free(rb);
    }
};
// 单字节
TEST_F(RingBufferTest, BasicEnqDeq) {
    uint8_t data_in = 0xAB;
    uint8_t data_out = 0;

    EXPECT_TRUE(ringBufferEnQeueue(rb, data_in));
    EXPECT_TRUE(ringBufferDequeue(rb, &data_out));
    EXPECT_EQ(data_in, data_out);
    
    // 空了之后再取应该失败
    EXPECT_FALSE(ringBufferDequeue(rb, &data_out));
}
TEST_F(RingBufferTest, FullAndOverflow) {
    uint8_t dummy = 0xFF;
    
    // 填充 1023 个字节
    for (int i = 0; i < RBUFFER_SIZE - 1; ++i) {
        EXPECT_TRUE(ringBufferEnQeueue(rb, (uint8_t)(i % 256)));
    }
    
    // 第 1024 个应该失败（因为 head == tail 表示空，需留一个空位区分）
    EXPECT_FALSE(ringBufferEnQeueue(rb, dummy));
}
TEST_F(RingBufferTest, BulkOperations) {
    uint8_t input[5] = {1, 2, 3, 4, 5};
    uint8_t output[5] = {0};

    EXPECT_TRUE(ringBufferEnQeueueBulk(rb, input, 5));
    EXPECT_TRUE(ringBufferDequeueBulk(rb, output, 5));
    
    // 使用 memcmp 比较内存内容
    EXPECT_EQ(0, memcmp(input, output, 5));
}
TEST_F(RingBufferTest, WrapAround) {
    // 1. 先填充并清空一部分，让 tail 靠近缓冲区末尾
    // 假设 RBUFFER_SIZE 是 1024
    uint8_t junk[1000];
    ringBufferEnQeueueBulk(rb, junk, 1000);
    ringBufferDequeueBulk(rb, junk, 1000);

    // 此时 tail 应该在 1000 附近
    // 2. 写入 50 字节，这会导致数据跨越索引 1023 回到 0
    uint8_t input[50];
    for(int i=0; i<50; i++) input[i] = i;
    
    EXPECT_TRUE(ringBufferEnQeueueBulk(rb, input, 50));
    
    uint8_t output[50];
    EXPECT_TRUE(ringBufferDequeueBulk(rb, output, 50));
    
    // 3. 验证跨越边界后的数据完整性
    EXPECT_EQ(0, memcmp(input, output, 50));
}
TEST_F(RingBufferTest, InvalidParameters) {
    uint8_t data[2000]; // 超过 RBUFFER_SIZE
    
    // 入队长度超过剩余空间
    EXPECT_FALSE(ringBufferEnQeueueBulk(rb, data, 1024));
    
    // 出队长度超过当前存储量
    ringBufferEnQeueue(rb, 0x01);
    EXPECT_FALSE(ringBufferDequeueBulk(rb, data, 10));
}
