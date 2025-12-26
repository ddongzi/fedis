#include "util.h"
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/time.h>
#include  <stdbool.h>
/**
 * 打印字符数组缓冲区内容，格式化输出字符和十六进制值
 * @param prefix 打印的前缀字符串，用于标识来源。 自定义标识
 * @param buf 字符数组缓冲区指针
 * @param len 缓冲区长度，必须明确指定
 */
void printBuf(const char* prefix, const char* buf, int len) {
    if (!buf) {
        printf("%s: (null)\n", prefix);
        return;
    }

    if (len == 0) {
        printf("%s: (empty)\n", prefix);
        return;
    }

    printf("%s: (length=%zu)\n", prefix, len);
    printf("--------------------------------------------------\n");
    printf("Offset | Char | Hex  | Content\n");
    printf("-------|------|------|----------------------------\n");

    size_t i;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)buf[i];
        printf("%6zu |   %c  | 0x%02x | ", i, isprint(c) ? c : '.', c);

        // 打印连续的字符内容，每行最多 16 个字节
        size_t start = i - (i % 16);
        size_t j;
        for (j = start; j < start + 16 && j < len; j++) {
            printf("%c", isprint((unsigned char)buf[j]) ? buf[j] : '.');
        }
        printf("\n");

        // 每 16 字节换行
        if ((i + 1) % 16 == 0 && i + 1 < len) {
            printf("-------|------|------|----------------------------\n");
        }
    }

    // 如果最后一行未满 16 字节，补齐格式
    if (i % 16 != 0) {
        for (size_t j = i % 16; j < 16; j++) {
            printf("       |      |      | ");
        }
        printf("\n");
    }

    printf("--------------------------------------------------\n");
}

char* fullPath(char* path)
{
    char buf[128] = {0};
    snprintf(buf, sizeof(buf), "%s/%s", PROJECT_ROOT, path);
    return strdup(buf);
}

/**
 * @brief 返回当前ms级时间戳
 * 
 * @return long long 
 */
long long mstime(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((long long)tv.tv_sec) * 1000 + (tv.tv_usec / 1000);
}

// 去除字符串中空白字符,
void strim(char *s)
{
    char* r =s ;
    char *w = s;
    while (*r)
    {
        if(!isspace(*r)) {
            *w = *r;
            w++;
        }
        r++;
    }
    *w = '\0';
}

/**
 * 字符串安全转10进制
 * @param s
 * @param val
 * @return 是否成功
 */
bool string2long(const char*s, long* out)
{
    char* endptr;
    errno = 0;
    long val = strtol(s, &endptr, 10);
    if (s==endptr || errno == ERANGE || *endptr != '\0')
    {
        return false;
    }
    if (val > LONG_MAX || val < LONG_MIN) return false;
    *out = val;
    return true;
}

