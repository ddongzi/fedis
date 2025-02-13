#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "list.h"

// 复制函数
void *dupFunction(void *ptr) {
    return strdup((char *)ptr);
}

// 释放函数
void freeFunction(void *ptr) {
    free(ptr);
}

// 比较函数
int compareFunction(void *ptr, void *key) {
    return strcmp((char *)ptr, (char *)key);
}

void test_listCreate() {
    list *l = listCreate();
    assert(l != NULL);
    listRelease(l);
    printf("✅ test_listCreate passed.\n");
}

void test_listAddNode() {
    list *l = listCreate();
    listAddNodeHead(l, listCreateNode("Head"));
    listAddNodeTail(l, listCreateNode("Tail"));
    assert(strcmp(listHead(l)->value, "Head") == 0);
    assert(strcmp(listTail(l)->value, "Tail") == 0);
    listRelease(l);
    printf("✅ test_listAddNode passed.\n");
}

void test_listInsertNode() {
    list *l = listCreate();
    listNode *n1 = listCreateNode("A");
    listNode *n2 = listCreateNode("B");
    listNode *n3 = listCreateNode("C");
    listAddNodeHead(l, n1);
    listAddNodeTail(l, n3);
    listInsertNode(l, n1, n2, 1);
    assert(strcmp(listNextNode(n1)->value, "B") == 0);
    listRelease(l);
    printf("✅ test_listInsertNode passed.\n");
}

void test_listSearchKey() {
    list *l = listCreate();
    listAddNodeHead(l, listCreateNode("findme"));
    assert(listSearchKey(l, "findme") != NULL);
    listRelease(l);
    printf("✅ test_listSearchKey passed.\n");
}

void test_listIndex() {
    list *l = listCreate();
    listAddNodeHead(l, listCreateNode("first"));
    listAddNodeTail(l, listCreateNode("second"));
    assert(strcmp(listIndex(l, 0)->value, "first") == 0);
    assert(strcmp(listIndex(l, 1)->value, "second") == 0);
    listRelease(l);
    printf("✅ test_listIndex passed.\n");
}

void test_listDelNode() {
    list *l = listCreate();
    listNode *n = listCreateNode("delete me");
    listAddNodeHead(l, n);
    listDelNode(l, n);
    assert(listHead(l) == NULL);
    listRelease(l);
    printf("✅ test_listDelNode passed.\n");
}

void test_listRotate() {
    list *l = listCreate();
    listAddNodeTail(l, listCreateNode("A"));
    listAddNodeTail(l, listCreateNode("B"));
    listAddNodeTail(l, listCreateNode("C"));
    listRotate(l);
    assert(strcmp(listHead(l)->value, "C") == 0);
    listRelease(l);
    printf("✅ test_listRotate passed.\n");
}

void test_listDup() {
    list *src = listCreate();
    listSetDupMethod(src, dupFunction);
    listSetFreeMethod(src, freeFunction);
    listAddNodeHead(src, listCreateNode(strdup("dup1")));
    listAddNodeTail(src, listCreateNode(strdup("dup2")));
    list *dest = listCreate();
    listDup(src, dest);
    assert(listLength(dest) == listLength(src));
    assert(strcmp(listHead(dest)->value, "dup1") == 0);
    listRelease(src);
    listRelease(dest);
    printf("✅ test_listDup passed.\n");
}

void test_listSetMethods() {
    list *l = listCreate();
    listSetDupMethod(l, dupFunction);
    listSetFreeMethod(l, freeFunction);
    listSetCompareMethod(l, compareFunction);
    assert(listGetDupMethod(l) != NULL);
    assert(listGetFreeMethod(l) != NULL);
    assert(listGetCompareMethod(l) != NULL);
    listRelease(l);
    printf("✅ test_listSetMethods passed.\n");
}

void test_listPrevNext() {
    list *l = listCreate();
    listNode *n1 = listCreateNode("A");
    listNode *n2 = listCreateNode("B");
    listAddNodeHead(l, n1);
    listAddNodeTail(l, n2);
    assert(listNextNode(n1) == n2);
    assert(listPrevNode(n2) == n1);
    listRelease(l);
    printf("✅ test_listPrevNext passed.\n");
}

void test_listNodeValue() {
    list *l = listCreate();
    listNode *n = listCreateNode("value");
    listAddNodeHead(l, n);
    assert(strcmp((char *)listNodeValue(n), "value") == 0);
    listRelease(l);
    printf("✅ test_listNodeValue passed.\n");
}

int main() {
    test_listCreate();
    test_listAddNode();
    test_listInsertNode();
    test_listSearchKey();
    test_listIndex();
    test_listDelNode();
    test_listRotate();
    test_listDup();
    test_listSetMethods();
    test_listPrevNext();
    test_listNodeValue();
    return 0;
}
