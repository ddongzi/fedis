#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

/**
 * 环形缓冲区。 定长。
 */
#include <stdbool.h>
#include <stdint.h>
typedef  struct
{
    unsigned char data[1024];
    long head; // 数据开头
    long tail; // 数据结尾
}RingBuffer;


RingBuffer* ringBufferCreate(long size);
void ringBufferDestroy(RingBuffer* rb);
bool ringBufferAdd(RingBuffer* rb, uint8_t data[], long size);

#endif
