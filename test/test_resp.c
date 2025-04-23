#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/**
 * @brief 编码：argv[] -> RESP 格式字符串
 *
 * @param [in] argc
 * @param [in] argv
 * @return char*
 */
char *resp_encode(int argc, char *argv[])
{
    size_t cap = 1024;
    char *buf = malloc(cap);
    size_t len = 0;

    len += snprintf(buf + len, cap - len, "*%d\r\n", argc);
    for (int i = 0; i < argc; ++i)
    {
        int arglen = strlen(argv[i]);
        len += snprintf(buf + len, cap - len, "$%d\r\n", arglen);
        if (len + arglen + 2 >= cap)
        {
            cap *= 2;
            buf = realloc(buf, cap);
        }
        memcpy(buf + len, argv[i], arglen);
        len += arglen;
        memcpy(buf + len, "\r\n", 2);
        len += 2;
    }
    buf[len] = '\0';
    return buf;
}
/**
 * @brief 从resp字符串解析
 *
 * @param [in] resp
 * @param [out] argc_out
 * @param [out] argv_out
 * @return int
 */
int resp_decode(const char *resp, int *argc_out, char **argv_out[])
{
    if (*resp != '*')
        return -1;
    int argc;
    sscanf(resp + 1, "%d", &argc);
    *argc_out = argc;
    *argv_out = malloc(sizeof(char *) * argc);

    const char *p = strchr(resp, '\n') + 1;
    for (int i = 0; i < argc; ++i)
    {
        if (*p != '$')
            return -1;
        int len;
        sscanf(p + 1, "%d", &len);
        p = strchr(p, '\n') + 1;

        (*argv_out)[i] = malloc(len + 1);
        memcpy((*argv_out)[i], p, len);
        (*argv_out)[i][len] = '\0';
        p += len + 2; // skip "\r\n"
    }
    return 0;
}
void run_tests()
{
    const char *test_cases[][5] = {
        {"SET", "foo", "bar"},                        // 普通三参数
        {"PING"},                                     // 单参数
        {"GET", ""},                                  // 空字符串参数
        {"MSET", "a", "1", "b", "2"},                 // 多参数
        {"ECHO", "hello\r\nworld"},                   // 带 \r\n 的内容
        {"特殊字符", "空格 test", "😀"},              // UTF-8 / 空格 / emoji
        {"大数据", "123456789012345678901234567890"}, // 长字符串
        {"NULL", NULL},                               // 模拟空指针（我们不处理 NULL 参数）
        {NULL}                                        // 结束标记
    };
    for (int t = 0; test_cases[t][0] != NULL; ++t)
    {
        // 构造 argc 和 argv
        int t_argc = 0;
        while (test_cases[t][t_argc] != NULL && t_argc < 5)
            t_argc++;
        char **t_argv = (char **)test_cases[t];

        printf("\n=== Test case %d ===\n", t + 1);
        printf("[Input]:\n");
        for (int i = 0; i < t_argc; ++i)
            printf("  argv[%d] = \"%s\"\n", i, t_argv[i]);

        // 编码
        char *resp = resp_encode(t_argc, t_argv);
        printf("\n[Encoded RESP]:\n%s", resp);

        // 解码
        int new_argc;
        char **new_argv;
        if (resp_decode(resp, &new_argc, &new_argv) != 0)
        {
            printf("  ❌ Decode failed\n");
            free(resp);
            continue;
        }

        // 输出解码结果
        printf("[Decoded argv]:\n");
        for (int i = 0; i < new_argc; ++i)
            printf("  argv[%d] = \"%s\"\n", i, new_argv[i]);

        // 验证一致性
        int match = (t_argc == new_argc);
        for (int i = 0; i < t_argc && match; ++i)
            if (strcmp(t_argv[i], new_argv[i]) != 0)
                match = 0;

        printf("\n[Check]: %s\n", match ? "✅ Match" : "❌ Mismatch");

        // 清理
        for (int i = 0; i < new_argc; ++i)
            free(new_argv[i]);
        free(new_argv);
        free(resp);
    }
}
int main(int argc, char const *argv[])
{
    run_tests();
    return 0;
}
