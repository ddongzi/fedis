#include "io.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>

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