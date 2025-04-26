#include "sds.h"
/**
 * @brief 
 * 
 * @param [in] init 
 * @param [in] len 字符串长度
 * @return sds* 
 */
static sds* _sdsnewWithLen(const char* init, int len)
{
    sds* ss;
    int bufLen = 0;
    bufLen = len < 1024 ? len * 2 + 1 : len + 1024 + 1;
    ss = calloc(1, sizeof(sds));
    ss->buf = calloc(bufLen, sizeof(char));
    ss->len = len;
    ss->free = bufLen - len - 1;
    memcpy(ss->buf, init, len);
    return ss;
}

/**
 * @brief 
 * 
 * @param [in] s 
 * @return sdshdr* 
 */
sds* sdsnew(const char* s)
{
    if (s == NULL)  return NULL;
    return _sdsnewWithLen(s, strlen(s));
}
sds* sdsempty()
{
    return _sdsnewWithLen("", 0);
}
void sdsfree(sds* ss)
{
    if (ss == NULL) return;
    if (ss->buf == NULL) return;
    free(ss->buf);
    free(ss);
}
int sdslen(const sds* ss)
{
    if (ss == NULL) return 0;
    return ss->len;
}
int sdsavail(const sds* ss)
{
    if (ss == NULL) return 0;
    return ss->free;
}
sds* sdsdump(const sds* ss)
{
    if (ss == NULL) return NULL;
    sds* res = malloc(sizeof(ss));
    res->buf = calloc(ss->len + ss->free + 1, sizeof(char));
    // 显示复制
    res->len = ss->len;
    res->free = ss->free;
    memcpy(res->buf,  ss->buf, ss->len + ss->free + 1);
    return res; 
}
void sdsclear(sds* ss)
{
    if (ss == NULL) return;
    ss->free = ss->len + ss->free ;
    memset(ss->buf, 0, ss->len);
    ss->len = 0;
}

/**
 * @brief 添加一些buf, 二进制数组
 * 
 * @param [in] dest 
 * @param [in] buf 
 * @param [in] n 
 */
void sdscatlen(sds* dest, const char* buf, int n)
{
    if (dest == NULL) return;
    if (buf == NULL) return;
    int len = sdslen(dest);
    int newlen = len + n;
    int newbuflen;
    if (n > sdsavail(dest)) {
        // 需要扩展
        newbuflen = newlen < 1024 ? newlen * 2 + 1 : newlen + 1024 + 1;
        dest->buf = realloc(dest->buf, newbuflen);
        dest->free = newbuflen - newlen - 1;
        dest->len = newlen;
        // 分配后填充0
        memset(dest->buf + len, 0, newbuflen - len);
        memcpy(dest->buf + len, buf, n);
    } else {
        memcpy(dest->buf + len, buf, n);
        dest->len = newlen;
    }
}


/**
 * @brief 
 * 
 * @param [in] dest 
 * @param [in] s 字符串
 */
void sdscat(sds* dest, const char* s)
{
    if (s == NULL) return;
    int len = sdslen(dest);
    int newlen = len + strlen(s);
    int newbuflen;
    if (strlen(s) > sdsavail(dest)) {
        // 需要扩展
        newbuflen = newlen < 1024 ? newlen * 2 + 1 : newlen + 1024 + 1;
        dest->buf = realloc(dest->buf, newbuflen);
        dest->free = newbuflen - newlen - 1;
        dest->len = newlen;
        // 分配后填充0
        memset(dest->buf + len, 0, newbuflen - len);
        memcpy(dest->buf + len, s, strlen(s));

    } else {
        memcpy(dest->buf + len, s, strlen(s));
        dest->len = newlen;
    }

}
void sdscatsds(sds* dest, sds* src)
{
    if (src == NULL) return;
    sdscat(dest, src->buf);
    src->free = src->len + src->free + 1;
    src->len = 0;
    src->buf[0] = '\0';
}
/**
 * @brief char*字符串  到dest 
 * @param [in] dest 
 * @param [in] s 
 */
void sdscpy(sds* dest, const char* s)
{
    sdsclear(dest);
    sdscat(dest, s);
}
void sdsgrowzero(sds* ss, int newlen)
{
    if (ss == NULL) return;
    int len = sdslen(ss);
    if (newlen <= len) {
        return;
    }
    int free = sdsavail(ss);
    int n = newlen - len;
    // 如果有空间扩展, 默认都是'\0'
    if (free >= n) {
        // 直接返回
        memset(ss->buf + len, 0, n);
        return;
    }
    int newbuflen = newlen < 1024 ? newlen * 2 + 1 : newlen + 1024 + 1;
    ss = realloc(ss, sizeof(sds) + newbuflen);
    ss->free = newbuflen - newlen - 1;
    ss->len = newlen;
    memset(ss->buf + len, 0, n);
    return;
}

/**
 * @brief 裁剪保留[start, end] 
 *  "helloworld"
 *  [2,10]
 * @param [in] ss 
 * @param [in] start 下标索引 
 * @param [in] end 下标索引
 * @return void
 * 
 * @note 如果 start>end, 成为空字符串
 */
void sdsrange(sds* ss, int start, int end)
{
    if (ss == NULL) return;
    int len = sdslen(ss);
    int newLen;
    if (start < 0) start = len + start;
    if (end < 0) end = len + end;
    if (start < 0 || end >= len)
        return;
    newLen = end - start + 1;
    if (newLen > 0) {
        memmove(ss->buf, ss->buf + start, newLen);
    }
    ss->buf[newLen] = '\0';
    ss->len = newLen;
    ss->free = ss->free + len - newLen ;
    return;
}
void sdstrim(sds* sds, const char* s)
{
    int start = 0;
    int end = sdslen(sds) - 1;
    while (start < end && strchr(s, sds->buf[start]))
        start++;
    while (end >= start && strchr(s, sds->buf[end]))
        end--;
    if (start > 0 || end < sdslen(sds) - 1)
        sdsrange(sds, start, end);
    return;
}
int sdscmp(const sds* sds1, const sds* sds2)
{
    if (sds1 == NULL || sds2 == NULL) return -1;
    int len1 = sdslen(sds1);
    int len2 = sdslen(sds2);
    if (len1!= len2) {
        return len1 - len2;
    }
    return strcmp(sds1->buf, sds2->buf);
}
