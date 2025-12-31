/**
 * @file conf.c 
 * @author dong (you@domain.com)
 * @brief 解析.conf文件
 * @version 0.1
 * @date 2025-04-15
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include "log.h"
#include "redis.h"
#include "util.h"

#define CONFIG_MAX_LINE 512

char* get_config(char* filename, const char* key)
{
    FILE* f = fopen(filename, "r");
    if (f == NULL)
    {
        log_error("Open config file failed. %s, %s", filename, strerror(errno));
        return NULL;
    }

    char line[64] = {0};
    char* ret = NULL;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        strim(line);
        if (line[0] == '#' || line[0] == '\n') continue;
        char* k = strtok(line, "=");

        if (strcasecmp(k, key) == 0)
        {
            char* v = strtok(NULL, "=");
            ret = malloc(strlen(v) + 1);
            strcpy(ret, v);
            break;
        }
    }
    // log_debug("CONF ---- key: %s, value: %s", key, ret);
    fclose(f);
    return ret;
}

/**
 * 写配置
 * @param key
 * @return
 */
void update_config(const char* filename, const char* key, const char* value)
{
    FILE* fin = fopen(filename, "r+");
    char* tmppath = fullPath("conf/conf.tmp");
    FILE* tmp = fopen(tmppath, "w+");
    if (fin == NULL)
    {
        log_error("Open config file failed. %s, %s", filename, strerror(errno));
        return;
    }
    if (tmp == NULL)
    {
        log_error("Open temp file failed. %s, %s", tmppath, strerror(errno));
        return;
    }
    int found = 0;
    char line[CONFIG_MAX_LINE];
    while (fgets(line, CONFIG_MAX_LINE, fin) != NULL)
    {
        if (strncasecmp(line, key, strlen(key)) == 0)
        {
            fprintf(tmp, "%s=%s\n", key, value);
            found = 1;
        } else
        {
            fprintf(tmp, "%s", line);
        }
    }
    if (!found)
    {
        fprintf(tmp, "%s=%s\n", key, value);
    }
    fclose(fin);
    fclose(tmp);

    if (rename(tmppath, filename) != 0)
    {
        log_error("Update config failed");
    }
}
