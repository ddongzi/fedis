#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * 将 RESP 格式转为普通字符串，去掉标识符号，添加空格
 * @param resp RESP 格式的输入
 * @return 转换后的字符串，需手动释放
 */
char* respParse(const char* resp) {
    if (!resp || *resp == '\0') return strdup("");

    char* result = malloc(1024); // 初始缓冲区，动态调整
    if (!result) return NULL;
    size_t pos = 0;
    size_t capacity = 1024;
    *result = '\0';

    const char* p = resp;
    int first_item = 1; // 标记是否是第一个元素，避免多余空格

    while (*p) {
        switch (*p) {
            case '+': // 简单字符串
            case '-': // 错误（这里简单处理，与 + 一致）
                p++; // 跳过标识
                while (*p && !(*p == '\r' && *(p + 1) == '\n')) {
                    if (pos + 1 >= capacity) {
                        capacity *= 2;
                        result = realloc(result, capacity);
                        if (!result) return NULL;
                    }
                    result[pos++] = *p++;
                }
                p += 2; // 跳过 \r\n
                break;

            case '$': // 批量字符串
                p++; // 跳过 $
                int len = atoi(p);
                if (len < 0) { // $-1 表示 null
                    p = strchr(p, '\n') + 1; // 跳到 \r\n 后
                    continue;
                }
                while (*p != '\r') p++; // 跳过长度部分
                p += 2; // 跳过 \r\n
                if (!first_item) {
                    if (pos + 1 >= capacity) {
                        capacity *= 2;
                        result = realloc(result, capacity);
                    }
                    result[pos++] = ' '; // 添加空格
                }
                for (int i = 0; i < len; i++) {
                    if (pos + 1 >= capacity) {
                        capacity *= 2;
                        result = realloc(result, capacity);
                    }
                    result[pos++] = *p++;
                }
                p += 2; // 跳过 \r\n
                first_item = 0;
                break;

            case '*': // 数组
                p++; // 跳过 *
                int num = atoi(p);
                if (num <= 0) { // *0 或 *-1
                    p = strchr(p, '\n') + 1;
                    continue;
                }
                while (*p != '\r') p++; // 跳过数量
                p += 2; // 跳过 \r\n
                break; // 递归处理数组中的元素

            default:
                p++; // 跳过未知字符
                break;
        }
    }

    result[pos] = '\0';
    return result;
}


// getRespLength 函数
static ssize_t getRespLength(const char* buf, size_t len) {
    if (len < 2) return -1;

    char type = buf[0];
    size_t i;

    switch (type) {
        case '+': // 简单字符串
        case '-': // 错误
        case ':': // 整数
            for (i = 1; i < len - 1; i++) {
                if (buf[i] == '\r' && buf[i + 1] == '\n') {
                    return i + 2;
                }
            }
            return -1;

        case '$': // 批量字符串， $10\r\nfoofoofoob\r\n
            if (len < 3) return -1;
            for (i = 1; i < len - 1; i++) {
                // 遍历找到第一个\r\n， 即length，然后根据i+length计算完整长度 
                if (buf[i] == '\r' && buf[i + 1] == '\n') {
                    char len_buf[32];
                    size_t prefix_len = i + 2;
                    strncpy(len_buf, buf + 1, i - 1);
                    len_buf[i - 1] = '\0';
                    int data_len = atoi(len_buf);
                    if (data_len <= 0) return prefix_len;    // $-1\r\n 返回5
                    if (len < prefix_len + data_len + 2) return -1; // 参数len太小了，不完整，返回-1
                    if (buf[prefix_len + data_len] == '\r' && buf[prefix_len + data_len + 1] == '\n') {
                        return prefix_len + data_len + 2;
                    }
                    return -1;
                }
            }
            printf("找不到\r\n ");
            return -1;

        case '*': // 数组 *2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
            if (len < 3) return -1;
            for (i = 1; i < len - 1; i++) {
                // 遍历找到第一个\r\n, 即数组大小，
                if (buf[i] == '\r' && buf[i + 1] == '\n') {
                    char len_buf[32];
                    size_t prefix_len = i + 2;
                    strncpy(len_buf, buf + 1, i - 1);
                    len_buf[i - 1] = '\0';
                    int num_elements = atoi(len_buf);
                    if (num_elements <= 0) return prefix_len; // *-1\r\n 返回5，前缀长度
                    size_t offset = prefix_len;
                    for (int j = 0; j < num_elements; j++) {
                        if (offset >= len) return -1;   // 如果大于buf的len，不完整
                        ssize_t elem_len = getRespLength(buf + offset, len - offset);
                        printf(" elem_len %u\n", elem_len);
                        if (elem_len == -1) return -1;  // 获取数组各元素
                        offset += elem_len;
                    }
                    return offset;
                }
            }
            return -1;

        default:
            printf("unexpected\n");
            return -1;
    }
}

// 测试函数
void testRespLength(const char* test_name, const char* buf, size_t len, ssize_t expected) {
    printf("Test: %s\n", test_name);
    printf("Input: '%.*s' (len=%zu)\n", (int)len, buf, len);
    ssize_t result = getRespLength(buf, len);

    printf("Expected: %zd, Got: %zd\n", expected, result);
    printf("Result: %s\n\n", (result == expected) ? "PASS" : "FAIL");
}

int main_testRespLength() {
    // 测试用例
    testRespLength("Simple String Complete FULLSYNC", "+FULLSYNC\r\n", 11, 11);
    testRespLength("Simple String Complete", "+OK\r\n", 5, 5);
    testRespLength("Simple String Incomplete", "+OK", 3, -1);
    testRespLength("Error Complete", "-ERR\r\n", 6, 6);
    testRespLength("Integer Complete", ":1000\r\n", 7, 7);
    testRespLength("Bulk String Complete", "$3\r\nfoo\r\n", 9, 9);
    testRespLength("Bulk String Empty", "$0\r\n", 5, 4);
    testRespLength("Bulk String Null", "$-1\r\n", 5, 5);
    testRespLength("Bulk String Incomplete Data", "$3\r\nfo", 6, -1);
    testRespLength("Bulk String Incomplete Prefix", "$3", 2, -1);
    testRespLength("Array Complete", "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n", 22, 22);
    testRespLength("Array Empty", "*0\r\n", 4, 4);
    testRespLength("Array Incomplete Element", "*2\r\n$3\r\nfoo\r\n$3\r\nb", 18, -1);
    testRespLength("Invalid Type", "xABC\r\n", 6, -1);
    testRespLength("Too Short", "+", 1, -1);
    testRespLength("RDB Length", "$10\r\n1234567890", 15, -1);
    testRespLength("RESP fullsyc", "+FULLSYNC\r\n", 11, 11);

    return 0;
}

void testRespParse(const char* test_name, const char* input, const char* expected) {
    char* result = respParse(input);
    printf("Test: %s\n", test_name);
    printf("Input:  '%s'\n", input);
    printf("Expect: '%s'\n", expected);
    printf("Result: '%s'\n", result ? result : "(null)");
    int pass = result && strcmp(result, expected) == 0;
    printf("Status: %s\n\n", pass ? "PASS" : "FAIL");
    free(result);
}

int main_testRespParse() {
    // 测试用例
    printf("\n=========TEST test_testRespParse ======\n");
    testRespParse("Simple String", "+OK\r\n", "OK");
    testRespParse("Error", "-ERR invalid\r\n", "ERR invalid");
    testRespParse("Bulk String", "$3\r\nfoo\r\n", "foo");
    testRespParse("Empty Bulk String", "$0\r\n\r\n", "");
    testRespParse("Null Bulk String", "$-1\r\n", "");
    testRespParse("Array Two Elements", "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n", "foo bar");
    testRespParse("Array One Element", "*1\r\n$3\r\nfoo\r\n", "foo");
    testRespParse("Empty Array", "*0\r\n", "");
    testRespParse("Null Array", "*-1\r\n", "");
    testRespParse("Nested Array", "*2\r\n*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n$3\r\nbaz\r\n", "foo bar baz");
    testRespParse("Empty Input", "", "");
    
    testRespParse("$<length>\r\n", "$28\r\n", "28");
    return 0;
}

int main() {
    main_testRespLength();
    main_testRespParse();
    return 0;
}
