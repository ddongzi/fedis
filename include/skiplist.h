#include "redis.h"

typedef struct zskiplistNode{
    struct zskiplistNode* backward; 
    
    double score;
    robj* obj;
    // 额外链接
    struct zskiplistLevel {
        zskiplistNode* forward;
        unsigned int span;  // 跨度，
    } level[];

} zskiplistNode;