#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char* respParse(const char* resp) {
    if (!resp) return NULL;
    
    char type = resp[0];
    const char* data = resp + 1;
    char* result = NULL;
    
    switch (type) {
        case '+':  // Simple Strings
        case '-':  // Errors
            result = strdup(data);
            result[strcspn(result, "\r\n")] = 0; // 去掉结尾的 \r\n
            break;
        case ':':  // Integers
            asprintf(&result, "%ld", strtol(data, NULL, 10));
            break;
        case '$': { // Bulk Strings
            int len = strtol(data, NULL, 10);
            if (len == -1) {
                result = strdup("(nil)");
            } else {
                const char* str = strchr(data, '\n');
                if (str) {
                    result = strndup(str + 1, len);
                }
            }
            break;
        }
        case '*': { // Arrays
            int count = strtol(data, NULL, 10);
            if (count == -1) {
                result = strdup("(empty array)");
            } else {
                result = malloc(1024);  // 假设最大长度不会超
                result[0] = '\0';
                const char* ptr = strchr(data, '\n') + 1;
                for (int i = 0; i < count; i++) {
                    char* elem = respParse(ptr);
                    strcat(result, elem);
                    strcat(result, " ");
                    free(elem);
                    
                    // 移动 ptr 指向下一个 RESP 片段
                    if (*ptr == '+' || *ptr == '-' || *ptr == ':') {
                        ptr = strchr(ptr, '\n') + 1;
                    } else if (*ptr == '$') {
                        int blen = strtol(ptr + 1, NULL, 10);
                        if (blen != -1) {
                            ptr = strchr(ptr, '\n') + 1 + blen + 2;
                        } else {
                            ptr = strchr(ptr, '\n') + 1;
                        }
                    }
                }
            }
            break;
        }
        default:
            result = strdup("(unknown)");
            break;
    }
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

    return 0;
}
void testRespParse()
{
    const char* examples[] = {
        "+OK\r\n",
        "-Error message\r\n",
        ":1000\r\n",
        "$6\r\nfoobar\r\n",
        "*-1\r\n",
        "*2\r\n$3\r\nfoo\r\n$3\r\nbar\r\n"
    };
    
    for (int i = 0; i < 6; i++) {
        char* parsed = respParse(examples[i]);
        printf("Parsed: %s\n", parsed);
        free(parsed);
    }
}

int main() {
    main_testRespLength();
    return 0;
}
