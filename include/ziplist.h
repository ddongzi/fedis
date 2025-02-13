#include <stdint.h>
#include <stdlib.h>

typedef struct ziplist {
    uint32_t zlbytes;   // 整个压缩列表占用字节数。 
    uint32_t zltail;    // 尾节点到列表起始地址的偏移，通过该偏移，无需遍历即可确定尾节点位置
    uint16_t zllen;     // 节点数量。当值小于UINT16_MAX(65535),值即是数量。当大于65535,真实数量需要遍历得到
    uint8_t entryX[];    // 
    // uint8_t zlend;  // 特殊标记0XFF（255），标记压缩列表末尾
} ziplist;

ziplist* ziplistNew();
// 创建一个给定值的新节点，并且插入头部或者尾部
void ziplistPush(ziplist* list, uint8_t* s, unsigned int len, int where);
// 创建一个给定值的新节点，插入到指定node节点之后
void ziplistInsert(ziplist* list, uint8_t* node ,uint8_t* s, unsigned int len);
// 返回指定索引上的节点
uint8_t* ziplistIndex(ziplist* list, int index);
// 查找返回给定值的节点
uint8_t* ziplistFind(ziplist* list, uint8_t* s, unsigned int len);
// 返回给定节点的前节点
uint8_t* ziplistPrev(ziplist* list, uint8_t* node);
// 返回给定节点的后节点
uint8_t* ziplistNext(ziplist* list, uint8_t* node);
// 返回节点值
uint8_t* ziplistGet(ziplist* list, uint8_t* node);
// 删除指定节点
uint8_t* ziplistDelete(ziplist* list, uint8_t* node);
// 删除连续多个节点
uint8_t* ziplistDeleteRange(ziplist* list, int start, int end);
// 返回列表目前占用内存字节数
uint32_t* ziplistBlobLen(ziplist* list);
// 返回列表目前包含节点数
uint16_t* ziplistLen(ziplist* list);

