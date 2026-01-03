#include "ringbuffer.h"
#include <stdlib.h>
#include <string.h>

RingBuffer* ringBufferCreate()
{
    RingBuffer* rb = malloc(sizeof(RingBuffer));
    rb->tail = 0;
    rb->head = 0;
    memset(rb->data, 0, RBUFFER_SIZE);
    return rb;
}
long ringBufferSize(RingBuffer* rb)
{ 
    return (rb->tail + RBUFFER_SIZE - rb->head) % RBUFFER_SIZE ;
}
bool ringBufferIsEmpty(RingBuffer* rb) 
{
    return rb->head == rb->tail;
}
bool ringBufferIsFull(RingBuffer* rb)
{
    return ringBufferSize(rb) >= RBUFFER_SIZE - 1;
}

bool ringBufferEnQeueue(RingBuffer* rb, uint8_t data)
{
    if (!rb) return false;
    if (ringBufferSize(rb) + 1 > RBUFFER_SIZE - 1) return false;

    rb->data[rb->tail] = data;
    rb->tail = (rb->tail + 1) % RBUFFER_SIZE;
    return true;
}
bool ringBufferEnQeueueBulk(RingBuffer* rb, uint8_t data[], long size)
{
    if (!rb) return false;
    if (ringBufferSize(rb) + size > RBUFFER_SIZE - 1) return false;
    
    long last_tail = (rb->tail + size) % RBUFFER_SIZE;

    if (last_tail > rb->head) {
        memcpy(rb->data + rb->tail, data, size);
    } else {
        long n = RBUFFER_SIZE - rb->tail;
        memcpy(rb->data + rb->tail, data, n);
        memcpy(rb->data, data + n, size - n);
    }
    rb->tail = last_tail;

    return true;
}
/**
 * 出队size个数据
 * @param [in] RingBuffer*
 * @param [out] data
 * @param [in] size
 * @return
 */
bool ringBufferDequeueBulk(RingBuffer* rb, uint8_t data[], long size)
{
    if (!rb) return false;
    if (size > ringBufferSize(rb)) return false;
    
    if (rb->head < rb->tail) {
        memcpy(data, rb->data + rb->head, size);
        rb->head = rb->head + size;
    } else {
        long n = RBUFFER_SIZE - rb->head;
        memcpy(data, rb->data + rb->head, n);
        memcpy(data + n, rb->data, size - n);
        rb->head = size - n;
    }
    return true;
}
bool ringBufferDequeue(RingBuffer* rb, uint8_t *data)
{
    if (!rb) return false;
    if (ringBufferSize(rb) < 1) return false;
    *data = rb->data[rb->head];
    rb->head = (rb->head + 1) % RBUFFER_SIZE;
    return true;
}
