#include "list.h"

list *listCreate()
{
    list *l = (list *)malloc(sizeof(list));
    if (l == NULL)
        return NULL;
    l->head = l->tail = NULL;
    l->len = 0;
    l->dup = NULL;
    l->free = NULL;
    l->compare = NULL;
    return l;
}
/**
 * @brief 创建一个自由node
 * 
 * @param [in] val 
 * @return listNode* 
 */
listNode* listCreateNode(void* val)
{
    listNode *node = (listNode *)malloc(sizeof(listNode));
    if (node == NULL)
        return NULL;
    node->value = val;
    node->prev = node->next = NULL;
    return node;
}
void listAddNodeHead(list *list, listNode *node)
{
    if (list->len == 0)
    {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    }
    else
    {
        node->prev = NULL;
        node->next = list->head;
        list->head->prev = node;
        list->head = node;
    }
    list->len++;
}
void listAddNodeTail(list *list, listNode *node)
{
    if (list->len == 0)
    {
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    }
    else
    {
        node->prev = list->tail;
        node->next = NULL;
        list->tail->next = node;
        list->tail = node;
    }
    list->len++;
}
void listInsertNode(list *list, listNode *old_node, listNode *new_node, int after)
{
    if (after)
    {
        new_node->prev = old_node;
        new_node->next = old_node->next;
        if (list->tail == old_node)
        {
            list->tail = new_node;
        }
    }
    else
    {
        new_node->next = old_node;
        new_node->prev = old_node->prev;
        if (list->head == old_node)
        {
            list->head = new_node;
        }
    }
    if (new_node->prev != NULL)
    {
        new_node->prev->next = new_node;
    }
    if (new_node->next != NULL)
    {
        new_node->next->prev = new_node;
    }
    list->len++;
}
listNode *listSearchKey(list *list, void *key)
{
    listNode *node = list->head;
    while (node != NULL)
    {
        if (list->compare)
        {
            if (list->compare(node->value, key))
            {
                return node;
            }
        }
        else
        {
            if (node->value == key)
            {
                return node;
            }
        }
        node = node->next;
    }
    return NULL;
}
listNode *listIndex(list *list, int index)
{
    listNode *node;
    if (index < 0)
    {
        index = (-index) - 1;
        node = list->tail;
        while (index-- && node)
        {
            node = node->prev;
        }
    }
    else
    {
        node = list->head;
        while (index-- && node)
        {
            node = node->next;
        }
    }
    return node;
}
void listDelNode(list *list, listNode *node)
{
    if (node->prev)
    {
        node->prev->next = node->next;
    }
    else
    {
        list->head = node->next;
    }
    if (node->next)
    {
        node->next->prev = node->prev;
    }
    else
    {
        list->tail = node->prev;
    }
    if (list->free)
    {
        list->free(node->value);
    }
    free(node);
    list->len--;
}
void listRotate(list *list)
{
    listNode *tail = list->tail;
    if (list->len <= 1)
    {
        return;
    }
    list->tail = tail->prev;
    list->tail->next = NULL;
    tail->prev = NULL;
    tail->next = list->head;
    list->head->prev = tail;
    list->head = tail;
}
void listRelease(list *list)
{
    unsigned long len = list->len;
    listNode *current = list->head;
    listNode *next;
    while (len--)
    {
        next = current->next;
        if (list->free)
        {
            list->free(current->value);
        }
        free(current);
        current = next;
    }
    free(list);
}
void listDup(list *src, list *dest)
{
    listNode *node = src->head;
    while (node)
    {
        listNode *new_node = (listNode *)malloc(sizeof(listNode));
        if (new_node == NULL)
        {
            return;
        }
        new_node->value = src->dup ? src->dup(node->value) : node->value;
        if (new_node->value == NULL)
        {
            free(new_node);
            return;
        }
        listAddNodeTail(dest, new_node);
        node = node->next;
    }
}

listNode *listPrevNode(listNode *node)
{
    return node->prev;
}
listNode *listNextNode(listNode *node)
{
    return node->next;
}
void *listNodeValue(listNode *node)
{
    return node->value;
}
void listSetDupMethod(list *list, void *(*dup)(void *ptr))
{
    list->dup = dup;
}
void *(*listGetDupMethod(list *list))(void *ptr)
{
    return list->dup;
}
void listSetFreeMethod(list *list, void (*free)(void *ptr))
{
    list->free = free;
}
void (*listGetFreeMethod(list *list))(void *ptr)
{
    return list->free;
}
void listSetCompareMethod(list *list, int (*compare)(void *ptr, void *key))
{
    list->compare = compare;
}
int (*listGetCompareMethod(list *list))(void *ptr, void *key)
{
    return list->compare;
}
unsigned long listLength(list *list)
{
    return list->len;
}
listNode *listHead(list *list)
{
    return list->head;
}
listNode *listTail(list *list)
{
    return list->tail;
}
