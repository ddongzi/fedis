#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

/**
 * 环形缓冲区。 定长。
 */
#include <stdbool.h>
#include <stdint.h>
#define RBUFFER_SIZE 1024
/**
 * 固定大小1023环形缓冲
 * head = tail 为空。 所以少一个
 */
typedef  struct
{
    unsigned char data[RBUFFER_SIZE];
    long head; // 
    long tail; // 
}RingBuffer;
RingBuffer* ringBufferCreate();
bool ringBufferDequeue(RingBuffer* rb, uint8_t *data);
bool ringBufferDequeueBulk(RingBuffer* rb, uint8_t data[], long size);
bool ringBufferEnQeueueBulk(RingBuffer* rb, uint8_t data[], long size);
bool ringBufferEnQeueue(RingBuffer* rb, uint8_t data);

// TODO 增加满了 覆盖写入
#endif
