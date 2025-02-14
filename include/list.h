#ifndef LIST_H
#define LIST_H

#include <stdlib.h>

typedef struct listNode
{
    struct listNode *prev;
    struct listNode *next;
    void *value;
} listNode;
typedef struct list
{
    listNode *head;
    listNode *tail;
    unsigned long len;                  // 链表节点数量
    void *(*dup)(void *ptr);            // 节点复制函数
    void (*free)(void *ptr);           // 节点释放函数:释放node value
    int (*compare)(void *ptr, void *key); // 节点 值对比函数
} list;

void listSetDupMethod(list *list, void *(*dup)(void *ptr));
void *(*listGetDupMethod(list *list))(void *ptr);
void listSetFreeMethod(list *list, void (*free)(void *ptr));
void (*listGetFreeMethod(list *list))(void *ptr);
void listSetCompareMethod(list *list, int (*compare)(void *ptr, void *key));
int (*listGetCompareMethod(list *list))(void *ptr, void *key);
unsigned long listLength(list *list);
listNode *listHead(list *list);
listNode *listTail(list *list);

listNode *listPrevNode(listNode *node);
listNode *listNextNode(listNode *node);
void *listNodeValue(listNode *node);

list *listCreate();
listNode* listCreateNode(void* val);
void listAddNodeHead(list *list, listNode *node);
void listAddNodeTail(list *list, listNode *node);
void listInsertNode(list *list, listNode *old_node, listNode *new_node, int after);
listNode *listSearchKey(list *list, void *key);
listNode *listIndex(list *list, int index);
void listDelNode(list *list, listNode *node);
void listRotate(list *list);         // 将尾部节点弹出，加到头前，成为新的头
void listDup(list *src, list *dest); // 复制链表
void listRelease(list *list);        // 释放链表及节点

unsigned long listSize(list *list); //

#endif