#include "sds.h"
#include <stdio.h>
#include <assert.h>

// 测试 sdsnew
void test_sdsnew() {
    printf("Testing sdsnew...\n");

    // 正常情况
    sds* str = sdsnew("hello");
    assert(str != NULL);
    assert(sdslen(str) == 5);
    assert(sdsavail(str) >= 0);
    assert(strcmp(str->buf, "hello") == 0);
    sdsfree(str);

    // 空字符串
    str = sdsnew("");
    assert(str != NULL);
    assert(sdslen(str) == 0);
    assert(sdsavail(str) >= 0);
    assert(strcmp(str->buf, "") == 0);
    sdsfree(str);

    // NULL 输入
    str = sdsnew(NULL);
    assert(str == NULL);

    printf("sdsnew tests passed!\n\n");
}

// 测试 sdsempty
void test_sdsempty() {
    printf("Testing sdsempty...\n");

    sds* str = sdsempty();
    assert(str != NULL);
    assert(sdslen(str) == 0);
    assert(sdsavail(str) >= 0);
    assert(strcmp(str->buf, "") == 0);
    sdsfree(str);

    printf("sdsempty tests passed!\n\n");
}

// 测试 sdsfree
void test_sdsfree() {
    printf("Testing sdsfree...\n");

    // 正常情况
    sds* str = sdsnew("test");
    sdsfree(str);

    // NULL 输入, assert
    sdsfree(NULL);

    printf("sdsfree tests passed!\n\n");
}

// 测试 sdslen
void test_sdslen() {
    printf("Testing sdslen...\n");

    sds* str = sdsnew("hello");
    assert(sdslen(str) == 5);
    sdsfree(str);

    // 空字符串
    str = sdsnew("");
    assert(sdslen(str) == 0);
    sdsfree(str);

    // NULL 输入
    assert(sdslen(NULL) == 0);

    printf("sdslen tests passed!\n\n");
}

// 测试 sdsavail
void test_sdsavail() {
    printf("Testing sdsavail...\n");

    sds* str = sdsnew("hello");
    assert(sdsavail(str) >= 0);
    sdsfree(str);

    // 空字符串
    str = sdsnew("");
    assert(sdsavail(str) >= 0);
    sdsfree(str);

    // NULL 输入
    assert(sdsavail(NULL) == 0);

    printf("sdsavail tests passed!\n\n");
}

// 测试 sdsdump
void test_sdsdump() {
    printf("Testing sdsdump...\n");

    sds* str = sdsnew("hello");
    sds* copy = sdsdump(str);
    assert(copy != NULL);
    assert(sdslen(copy) == sdslen(str));
    assert(strcmp(copy->buf, str->buf) == 0);
    sdsfree(str);
    sdsfree(copy);

    // 空字符串
    str = sdsnew("");
    copy = sdsdump(str);
    assert(copy != NULL);
    assert(sdslen(copy) == 0);
    assert(strcmp(copy->buf, "") == 0);
    sdsfree(str);
    sdsfree(copy);

    // NULL 输入
    copy = sdsdump(NULL);
    assert(copy == NULL);

    printf("sdsdump tests passed!\n\n");
}

// 测试 sdsclear
void test_sdsclear() {
    printf("Testing sdsclear...\n");

    sds* str = sdsnew("hello");
    sdsclear(str);
    assert(sdslen(str) == 0);
    assert(strcmp(str->buf, "") == 0);
    sdsfree(str);

    // 空字符串
    str = sdsnew("");
    sdsclear(str);
    assert(sdslen(str) == 0);
    assert(strcmp(str->buf, "") == 0);
    sdsfree(str);

    // NULL 输入
    sdsclear(NULL);

    printf("sdsclear tests passed!\n\n");
}

// 测试 sdscat
void test_sdscat() {
    printf("Testing sdscat...\n");

    sds* str = sdsnew("hello");
    sdscat(str, " world");
    assert(sdslen(str) == 11);
    assert(strcmp(str->buf, "hello world") == 0);
    sdsfree(str);

    // 空字符串拼接
    str = sdsnew("");
    sdscat(str, "hello");
    assert(sdslen(str) == 5);
    assert(strcmp(str->buf, "hello") == 0);
    sdsfree(str);

    // NULL 输入
    str = sdsnew("hello");
    sdscat(str, NULL);
    assert(sdslen(str) == 5);
    assert(strcmp(str->buf, "hello") == 0);
    sdsfree(str);

    printf("sdscat tests passed!\n\n");
}

// 测试 sdscatsds
void test_sdscatsds() {
    printf("Testing sdscatsds...\n");

    sds* str1 = sdsnew("hello");
    sds* str2 = sdsnew(" world");
    sdscatsds(str1, str2);
    assert(sdslen(str1) == 11);
    assert(strcmp(str1->buf, "hello world") == 0);
    sdsfree(str1);
    sdsfree(str2);

    // 空字符串拼接
    str1 = sdsnew("");
    str2 = sdsnew("hello");
    sdscatsds(str1, str2);
    assert(sdslen(str1) == 5);
    assert(strcmp(str1->buf, "hello") == 0);
    sdsfree(str1);
    sdsfree(str2);

    // NULL 输入
    str1 = sdsnew("hello");
    sdscatsds(str1, NULL);
    assert(sdslen(str1) == 5);
    assert(strcmp(str1->buf, "hello") == 0);
    sdsfree(str1);

    printf("sdscatsds tests passed!\n\n");
}

// 测试 sdscpy
void test_sdscpy() {
    printf("Testing sdscpy...\n");

    sds* str = sdsnew("hello");
    sdscpy(str, "world");
    assert(sdslen(str) == 5);
    assert(strcmp(str->buf, "world") == 0);
    sdsfree(str);

    // 空字符串
    str = sdsnew("");
    sdscpy(str, "hello");
    assert(sdslen(str) == 5);
    assert(strcmp(str->buf, "hello") == 0);
    sdsfree(str);

    // NULL 输入
    str = sdsnew("hello");
    sdscpy(str, NULL);
    assert(sdslen(str) == 0);
    assert(strcmp(str->buf, "") == 0);
    sdsfree(str);

    printf("sdscpy tests passed!\n\n");
}

// 测试 sdsgrowzero
void test_sdsgrowzero() {
    printf("Testing sdsgrowzero...\n");

    sds* str = sdsnew("hello");
    sdsgrowzero(str, 10);
    assert(sdslen(str) == 5);
    assert(strcmp(str->buf, "hello") == 0); // 前 5 个字符不变
    assert(str->buf[5] == '\0'); // 新增部分用零填充
    sdsfree(str);

    // 扩展长度小于当前长度, 不支持操作
    str = sdsnew("hello");
    sdsgrowzero(str, 3);
    assert(sdslen(str) == 5);
    assert(strcmp(str->buf, "hello") == 0);
    sdsfree(str);

    // NULL 输入
    sdsgrowzero(NULL, 10);

    printf("sdsgrowzero tests passed!\n\n");
}

// 测试 sdsrange
void test_sdsrange() {
    printf("Testing sdsrange...\n");

    sds* str = sdsnew("hello world");
    sdsrange(str, 6, 10);
    assert(sdslen(str) == 5);
    assert(strcmp(str->buf, "world") == 0);
    sdsfree(str);

    // 负索引不支持
    // str = sdsnew("hello world");
    // sdsrange(str, -5, -1);
    // assert(sdslen(str) == 5);
    // assert(strcmp(str->buf, "world") == 0);
    // sdsfree(str);

    // 空字符串
    str = sdsnew("");
    sdsrange(str, 0, 0);
    assert(sdslen(str) == 0);
    assert(strcmp(str->buf, "") == 0);
    sdsfree(str);

    // NULL 输入
    sdsrange(NULL, 0, 0);

    printf("sdsrange tests passed!\n\n");
}

// 测试 sdstrim
void test_sdstrim() {
    printf("Testing sdstrim...\n");

    sds* str = sdsnew("  hello world  ");
    sdstrim(str, " ");
    assert(sdslen(str) == 11);
    assert(strcmp(str->buf, "hello world") == 0);
    sdsfree(str);

    // 空字符串
    str = sdsnew("");
    sdstrim(str, " ");
    assert(sdslen(str) == 0);
    assert(strcmp(str->buf, "") == 0);
    sdsfree(str);

    // NULL 输入
    sdstrim(NULL, " ");

    printf("sdstrim tests passed!\n\n");
}

// 测试 sdscmp
void test_sdscmp() {
    printf("Testing sdscmp...\n");

    sds* str1 = sdsnew("hello");
    sds* str2 = sdsnew("hello");
    assert(sdscmp(str1, str2) == 0);
    sdsfree(str1);
    sdsfree(str2);

    // 不同字符串
    str1 = sdsnew("hello");
    str2 = sdsnew("world");
    assert(sdscmp(str1, str2) != 0);
    sdsfree(str1);
    sdsfree(str2);

    // 空字符串
    str1 = sdsnew("");
    str2 = sdsnew("");
    assert(sdscmp(str1, str2) == 0);
    sdsfree(str1);
    sdsfree(str2);

    // NULL 输入
    assert(sdscmp(NULL, NULL) == 0);
    assert(sdscmp(sdsnew("hello"), NULL) != 0);
    assert(sdscmp(NULL, sdsnew("hello")) != 0);

    printf("sdscmp tests passed!\n\n");
}

int main() {
    test_sdsnew();
    test_sdsempty();
    test_sdsfree();
    test_sdslen();
    test_sdsavail();
    test_sdsdump();
    test_sdsclear();
    test_sdscat();
    test_sdscatsds();
    test_sdscpy();
    test_sdsgrowzero();
    test_sdsrange();
    test_sdstrim();
    test_sdscmp();

    printf("All tests passed!\n");
    return 0;
}