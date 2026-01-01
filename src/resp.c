/**
 * RESP格式
 * 简单字符串 +OK\r\n
 * 异常错误 -ERR unknown command\r\n
 * 整数 :100\r\n
 * 批量字符串 $5\r\nhello\r\n
 * 数组类型 *2\r\n$3\r\nGET\r\n$3\r\nsex\r\n
 * 
 * 特殊：
 * null字符串 $-1\r\n 返回客户端get错误
 * 空数组 *0\r\n
 * null数组 *-1\r\n
 * 长度为0的字符串 $0\r\n\r\n
 *
 * 错误：
 * *-2\r\n
 * $-2\r\n
 * :\r\n
 * 
 * ❤️ 客户端请求统一只能以数组类型。
 * ❤️ 服务端会返回所有类型
 */
#include  <assert.h>
#include <stdlib.h>
#include "resp.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <openssl/types.h>
#include <ctype.h>
#include "log.h"


struct RespShared resp = {
    .ok = "+OK\r\n",
    .pong = "+PONG\r\n",
    .err = "-ERR\r\n",
    .keyNotFound = "-ERR key not found\r\n",
    .bye = "-bye\r\n",
    .invalidCommand = "-Invalid command\r\n",
    .fullsync = "+FULLSYNC\r\n",
    .appendsync = "+APPENDSYNC\r\n",
    .dupkey = "-ERR:Duplicate key\r\n",
    .ping = "*1\r\n$4\r\nPING\r\n",
    .info = "*1\r\n$4\r\nINFO\r\n",
    .valmissed = "-ERR: Value missed\r\n"
};

/**
 * @brief 编码批量数组字符串。
 * 
 * @param [in] argc 
 * @param [in] argv 
 * @return char* 返回编码后结果
 */
char* respEncodeArrayString(int argc, char* argv[])
{
    size_t cap = 1024;
    char* buf = malloc(cap);
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
 * 编码为$字符串
 * @param s
 * @return
 */
char* respEncodeBulkString(const char* s)
{
    char buf[1024] = {0};
    sprintf(buf, "$%lu\r\n%s\r\n", strlen((s)), s);
    return strdup(buf);
}

/**
 * @brief 服务端使用：从resp字符串解析: 批量数组字符串
 *
 * @param [in] resp 多条批量字符串格式
 * @param [out] argc_out
 * @param [out] argv_out
 * @return int -1标识失败。 0标识成功
 */
int resp_decode(const char* resp, int* argc_out, char** argv_out[])
{
    if (*resp != '*')
    {
        return -1;
    }
    int argc;
    sscanf(resp + 1, "%d", &argc);
    *argc_out = argc;
    *argv_out = malloc(sizeof(char*) * argc);

    const char* p = strchr(resp, '\n') + 1;
    for (int i = 0; i < argc; ++i)
    {
        if (*p != '$')
        {
            log_error("RESP $.");
            return -1;
        }
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

/**
 * resp字符串 转为用户看的
 * @param resp
 * @return 错误返回NULL
 */
char* resp_str(const char* resp)
{
    if (resp == NULL) return NULL;
    if (strlen(resp) > 1024) return NULL;
    char buf[1024] = {0};
    char* p = strdup(resp);
    char* endp = p + strlen(resp);
    switch (*resp)
    {
    case '$':
        {
            int len, n;
            p += 1;
            // $_\r\n
            sscanf(p, "%d%n", &len, &n);
            p += n;
            if (n == 0) return NULL;
            if (len < -1)
            {
                // $-2\r\n
                return NULL;
            }
            if (len == -1)
            {
                // $-1\r\n
                sprintf(buf, "NULL");
                break;
            }
            if (p + 2 > endp) return NULL;
            if (*p++ != '\r') return NULL;
            if (*p++ != '\n') return NULL;
            // sex\r\n
            if (p + len + 2 > endp) return NULL;
            memcpy(buf, p, len);
            buf[len] = '\0';
            p += len;
            if (*p++ != '\r') return NULL;
            if (*p++ != '\n') return NULL;
            break;
        }
    case '*':
        {
            p += 1;
            int n = 0;
            int arrlen = 0;
            sscanf(p, "%d%n", &arrlen, &n);
            p += n;
            if (n == 0) return NULL;
            if (p + 2 > endp) return NULL;
            if (*p++ != '\r') return NULL;
            if (*p++ != '\n') return NULL;
            if (arrlen == -1)
            {
                sprintf(buf, "NULL");
                break;
            }
            if (arrlen < -1)
            {
                return NULL;
            }
            int len = 0;
            int bufidx = 0;
            buf[bufidx++] = '[';
            for (int i = 0; i < arrlen; ++i)
            {
                // $_\r\n
                if (p + 1 > endp) return NULL;
                if (*p++ != '$') return NULL;
                sscanf(p, "%d%n", &len, &n);
                p += n;
                if (n == 0) return NULL;
                if (p + 2 > endp) return NULL;
                if (*p++ != '\r') return NULL;
                if (*p++ != '\n') return NULL;
                if (len < -1)
                {
                    // $-2\r\n
                    return NULL;
                }
                else if (len == -1)
                {
                    // $-1\r\n
                    int nwrite = sprintf(buf + bufidx, "NULL");
                    bufidx += nwrite;
                    buf[bufidx++] = ',';
                }
                else
                {
                    // sex\r\n
                    if (p + len + 2 > endp) return NULL;
                    memcpy(buf + bufidx, p, len);
                    bufidx += len;
                    buf[bufidx++] = ',';
                    p += len;
                    if (*p++ != '\r') return NULL;
                    if (*p++ != '\n') return NULL;
                }
            }
            buf[bufidx] = ']';
            break;
        }
    case ':':
        {
            p += 1;
            int num = 0;
            int n = 0;
            sscanf(p, "%d%n", &num, &n);
            p += n;
            if (n == 0) return NULL;
            if (p + 2 > endp) return NULL;
            if (*p++ != '\r') return NULL;
            if (*p++ != '\n') return NULL;
            sprintf(buf, "%d", num);
            break;
        }
    case '+':
    case '-':
        {
            p += 1;
            char* crlf = memchr(p, '\r', endp - p);
            if (!crlf || (crlf + 1 > endp) || *(crlf + 1) != '\n')
            {
                // 没有找到完整的\r\n.
                return NULL;
            }
            size_t len = crlf - p;
            if (len == 0) return NULL;
            memcpy(buf, p, len);
            buf[len] = '\0';
            break;
        }
    default:
        return NULL;
        break;
    }
    return strdup(buf);
}

/**辅助get，找到\r\n， 返回\n位置
 *
 * @param buf
 * @param len
 * @return
 */
static char* find_line_end(char* buf, size_t len)
{
    if (len < 2) return NULL;
    char* r = memchr(buf, '\r', len - 1);
    if (r && *(r + 1) == '\n')
        return r + 1;
    return NULL;
}

/**
 * 从buf开头识别一个resp字符串。
 * @param buf
 * @param len
 * @return 指向最终位置\n。 如果识别不出来 就是NULL
 */
char* respParse(char* buf, size_t len)
{
    if (len < 3) return NULL;
    char type = buf[0];
    char* first_crlf = find_line_end(buf, len);
    if (first_crlf == NULL) return NULL;

    switch (type)
    {
    case '+':
    case '-':
        return first_crlf;
    case ':':
        for (char* p  =  buf + 1; p < first_crlf - 1; p++)
        {
            if (!isdigit(*p)) return NULL;
        }
        return first_crlf;
    case '$':
        long slen = strtol(buf + 1, NULL, 10);
        if (slen == -1) return first_crlf; // $-1\r\n
        if (slen < -1) return  NULL;

        char* str = first_crlf + 1;
        size_t remain_len = len - (str - buf);
        if (remain_len < slen + 2) return NULL;

        char* second_crlf = str + slen;
        if (*second_crlf == '\r' && *(second_crlf + 1) == '\n')
            return second_crlf + 1;
        return NULL;
    case '*':
        long count = strtol(buf + 1, NULL, 10);
        if (count == 0) return NULL; //
        char* current = first_crlf + 1;
        for (int i = 0; i < count; ++i)
        {
            size_t remain_len = len - (current - buf);
            char* element_end = respParse(current, remain_len);
            if (element_end == NULL) return NULL;
            current = element_end + 1;
        }
        return current - 1;
    default:
        break;
    }
}
