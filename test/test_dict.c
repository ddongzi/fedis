#include "dict.h"
#include <assert.h>
#include <string.h>

/* 哈希函数 */
unsigned long simpleHash(const void* key) {
    const char* str = (const char*)key;
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

/* key 复制 */
void* keyDup(void* privdata, const void* key) {
    (void)privdata;
    return strdup((const char*)key);
}

/* value 复制 */
void* valDup(void* privdata, const void* val) {
    if (val == NULL) return NULL;
    (void)privdata;
    return strdup((const char*)val);
}

/* key 比较 */
int keyCompare(void* privdata, const void* key1, const void* key2) {
    (void)privdata;
    return strcmp((const char*)key1, (const char*)key2);
}

/* key & value 释放 */
void keyDestructor(void* privdata, void* key) {
    (void)privdata;
    free(key);
}

void valDestructor(void* privdata, void* val) {
    (void)privdata;
    free(val);
}

/* 初始化 dictType */
dictType type = {
    .hashFunction = simpleHash,
    .keyDup = keyDup,
    .valDup = valDup,
    .keyCompare = keyCompare,
    .keyDestructor = keyDestructor,
    .valDestructor = valDestructor
};

/* 1. 测试字典创建 */
void test_dictCreate() {
    dict* d = dictCreate(&type, NULL);
    assert(d != NULL);
    assert(d->ht[0].size == 0);
    assert(d->ht[0].used == 0);
    dictRelease(d);
    printf("✅ test_dictCreate passed.\n");
}

/* 2. 测试插入 key-value */
void test_dictAdd() {
    dict* d = dictCreate(&type, NULL);
    assert(dictAdd(d, "name", "Alice") == DICT_OK);
    assert(dictAdd(d, "age", "25") == DICT_OK);
    assert(dictAdd(d, "city", "New York") == DICT_OK);
    dictRelease(d);
    printf("✅ test_dictAdd passed.\n");
}

/* 3. 测试查找 key */
void test_dictFetchValue() {
    dict* d = dictCreate(&type, NULL);
    dictAdd(d, "name", "Alice");
    assert(strcmp(dictFetchValue(d, "name"), "Alice") == 0);
    assert(dictFetchValue(d, "unknown") == NULL);
    dictRelease(d);
    printf("✅ test_dictFetchValue passed.\n");
}

/* 4. 测试删除 key */
void test_dictDelete() {
    dict* d = dictCreate(&type, NULL);
    dictAdd(d, "name", "Alice");
    assert(dictDelete(d, "name") == DICT_OK);
    assert(dictFetchValue(d, "name") == NULL);
    assert(dictDelete(d, "name") == DICT_ERR); // 删除不存在的 key
    dictRelease(d);
    printf("✅ test_dictDelete passed.\n");
}

/* 5. 测试重复 key 处理 */
void test_dictAddDuplicate() {
    dict* d = dictCreate(&type, NULL);
    assert(dictAdd(d, "name", "Alice") == DICT_OK);
    assert(dictAdd(d, "name", "Bob") == DICT_ERR); // 不能插入相同 key
    assert(strcmp(dictFetchValue(d, "name"), "Alice") == 0);
    dictRelease(d);
    printf("✅ test_dictAddDuplicate passed.\n");
}

/* 6. 测试 NULL 处理 */
void test_dictNullCases() {
    dict* d = dictCreate(&type, NULL);
    assert(dictAdd(NULL, "name", "Alice") == DICT_ERR);
    assert(dictFetchValue(NULL, "name") == NULL);
    assert(dictDelete(NULL, "name") == DICT_ERR);
    dictRelease(d);
    printf("✅ test_dictNullCases passed.\n");
}

/* 7. 触发扩容 */
void test_dictExpand() {
    dict* d = dictCreate(&type, NULL);
    for (int i = 0; i < 50; i++) {
        char key[16], val[16];
        sprintf(key, "key%d", i);
        sprintf(val, "val%d", i);
        dictAdd(d, key, val);
    }
    assert(d->ht[0].size > DICT_INITIAL_SIZE);
    dictRelease(d);
    printf("✅ test_dictExpand passed.\n");
}

/* 8. 触发缩容 */
void test_dictShrink() {
    dict* d = dictCreate(&type, NULL);
    for (int i = 0; i < 50; i++) {
        char key[16], val[16];
        sprintf(key, "key%d", i);
        sprintf(val, "val%d", i);
        dictAdd(d, key, val);
    }
    // 确保rehash完毕，才能触发缩容
    for (int i = 0; i < 50; i++) {
        char key[16], val[16];
        sprintf(key, "key%d", i);
        sprintf(val, "val%d", i);
        dictFetchValue(d, key);
    }
    for (int i = 0; i < 45; i++) {
        char key[16];
        sprintf(key, "key%d", i);

        dictDelete(d, key);
    }
    assert(d->ht[0].size > 0);
    dictRelease(d);
    printf("✅ test_dictShrink passed.\n");
}

/* 9. 测试迭代器 */
void test_dictIterator() {
    dict* d = dictCreate(&type, NULL);
    dictAdd(d, "name", "Alice");
    dictAdd(d, "age", "25");
    dictAdd(d, "city", "New York");

    dictIterator* iter = dictGetIterator(d);
    dictEntry* entry;
    int count = 0;
    while ((entry = dictIterNext(iter)) != NULL) {
        assert(entry->key != NULL);
        printf("Iterating key %s\n", entry->key);
        count++;
    }
    dictReleaseIterator(iter);
    assert(count == 3);
    dictRelease(d);
    printf("✅ test_dictIterator passed.\n");
}

/* 9. 测试dictType 各函数为空 */
void test_dictDictTypeNull()
{
    dictType type;
    type.hashFunction = NULL;
    type.keyDup = NULL;
    type.valDup = NULL;
    type.keyCompare = NULL;
    type.keyDestructor = NULL;
    type.valDestructor = NULL;

    dict* d = dictCreate(&type, NULL);
    assert(d == NULL);
    type.hashFunction = simpleHash;
    type.keyCompare = keyCompare;
    d = dictCreate(&type, NULL);
    assert(dictAdd(d, "name", "Alice") == DICT_OK);
    assert(strcmp(dictFetchValue(d, "name"), "Alice")== 0);
    assert(dictDelete(d, "name") == DICT_OK);
    dictRelease(d);
    printf("✅ test_dictDictTypeNull passed.\n");
}


int main() {
    test_dictCreate();
    test_dictAdd();
    test_dictFetchValue();
    test_dictDelete();
    test_dictAddDuplicate();
    test_dictNullCases();
    test_dictExpand();
    test_dictShrink();
    test_dictIterator();
    test_dictDictTypeNull();
    
    printf("\n🎉 All tests passed!\n");
    return 0;
}
