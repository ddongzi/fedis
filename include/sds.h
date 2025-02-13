#ifndef SDS_H
#define SDS_H
#include <stdlib.h>
#include <string.h>
#include <assert.h>
typedef struct sds{
    int len;    // 字符串长度，不含'\0'
    int free;   // 剩余空间，如果为0表示没有空闲。不含'\0'
    char *buf; // 内容以'\0'结束。至少一个字节长度'\0'. 
} sds;

// 从c字符串创建sds
sds* sdsnew(const char* s);
// 创建一个空sds
sds* sdsempty();
// 释放sds
void sdsfree(sds* ss);
// sds长度
int sdslen(const sds* ss);
// sds空闲长度
int sdsavail(const sds* ss);
// 返回一个sds副本,,copy
sds* sdsdump(const sds* ss);
// 情况sds字符串内容
void sdsclear(sds* ss);
// 将C字符串拼接到SDS末尾
void sdscat(sds* dest, const char* s);
// 将sds字符串拼接到sds末尾
void sdscatsds(sds* dest, sds* src);
// 将C字符串覆盖写入SDS
void sdscpy(sds* dest, const char* s);
// 用空字符将SDS len扩展到指定长度
void sdsgrowzero(sds* ss, int newlen);
// 裁剪， 去掉区间外数据
void sdsrange(sds* ss, int start, int end);
// 去除两端无关字符
void sdstrim(sds* sds, const char* s);
// 比较两个sds字符串相同
int sdscmp(const sds* sds1, const sds* sds2);

#endif

