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

int main() {
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
    return 0;
}
