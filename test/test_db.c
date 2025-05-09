#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "db.h"
#include "robj.h"

/* 创建测试用的 robj 作为值 */
robj *createTestValue(const char *str) {
    return robjCreateStringObject(str);
}

/* 测试 dbCreate 和 dbFree */
void test_dbCreateFree() {
    log_debug("Running %s...\n", __func__);
    redisDb *db = dbCreate(1);
    assert(db != NULL);
    assert(db->dict != NULL);
    assert(db->id == 1);
    dbFree(db);
    log_debug("✅ %s passed!\n", __func__);
}

/* 测试 dbAdd 和 dbGet */
void test_dbAddFind() {
    log_debug("Running %s...\n", __func__);
    redisDb *db = dbCreate(1);
    robj *key = robjCreateStringObject("GET");
    robj *value = createTestValue("VALUE");

    assert(dbAdd(db, key, value) == 0);
    robj *found = dbGet(db, key);
    assert(found != NULL);
    assert(strcmp(((sds*)(found->ptr))->buf, "VALUE") == 0);

    dbFree(db);
    log_debug("✅ %s passed!\n", __func__);
}

/* 测试重复键 */
void test_dbOverwriteKey() {
    log_debug("Running %s...\n", __func__);
    redisDb *db = dbCreate(1);
    robj *key = robjCreateStringObject("SET");
    robj *value1 = createTestValue("VALUE1");
    robj *value2 = createTestValue("VALUE2");

    assert(dbAdd(db, key, value1) == 0);
    assert(dbAdd(db, key, value2) == -1); // 不能重复添加

    robj *found = dbGet(db, key);
    assert(found == value1); // 旧值应该还在

    dbFree(db);
    log_debug("✅ %s passed!\n", __func__);
}

/* 测试 dbDelete */
void test_dbDelete() {
    log_debug("Running %s...\n", __func__);
    redisDb *db = dbCreate(1);
    robj *key = robjCreateStringObject("DEL");
    robj *value = createTestValue("DELETE_ME");

    assert(dbAdd(db, key, value) == 0);
    assert(dbDelete(db, key) == 0);
    // 已经释放key了，
    robj* key2 = robjCreateStringObject("DEL");
    assert(dbGet(db, key) == NULL); // 删除后找不到

    dbFree(db);
    log_debug("✅ %s passed!\n", __func__);
}

/* 测试删除不存在的键 */
void test_dbDeleteNonExistent() {
    log_debug("Running %s...\n", __func__);
    redisDb *db = dbCreate(1);
    robj *key = robjCreateStringObject("NON_EXISTENT");

    assert(dbDelete(db, key) == -1); // 删除不存在的键

    dbFree(db);
    log_debug("✅ %s passed!\n", __func__);
}

/* 测试 dbClear */
void test_dbClear() {
    log_debug("Running %s...\n", __func__);
    redisDb *db = dbCreate(1);
    robj *key1 = robjCreateStringObject("KEY1");
    robj *key2 = robjCreateStringObject("KEY2");

    robj *value1 = createTestValue("DATA1");
    robj *value2 = createTestValue("DATA2");

    assert(dbAdd(db, key1, value1) == 0);
    assert(dbAdd(db, key2, value2) == 0);

    dbClear(db);
    assert(dbGet(db, key1) == NULL);
    assert(dbGet(db, key2) == NULL);

    dbFree(db);
    log_debug("✅ %s passed!\n", __func__);
}

/* 测试 NULL 参数 */
void test_dbNullCases() {
    printf("Running %s...\n", __func__);
    redisDb *db = dbCreate(1);
    robj *key = robjCreateStringObject("NULL");

    assert(dbAdd(NULL, key, NULL) == -1);
    assert(dbAdd(db, NULL, NULL) == -1);
    assert(dbGet(NULL, key) == NULL);
    assert(dbGet(db, NULL) == NULL);
    assert(dbDelete(NULL, key) == -1);
    assert(dbDelete(db, NULL) == -1);

    dbFree(db);
    printf("✅ %s passed!\n", __func__);
}

/* 运行所有测试 */
int main() {
    test_dbCreateFree();
    test_dbAddFind();
    test_dbOverwriteKey();
    test_dbDelete();
    test_dbDeleteNonExistent();
    test_dbClear();
    test_dbNullCases();
    printf("\n🎉 All tests passed!\n");
    return 0;
}
