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
 char* get_config(const char* filename, const char* key)
 {
    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        perror("open config file");
        return NULL;
    }
    
    char line[64] = {0};
    char* ret = NULL;
    while(fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* k = strtok(line, "=");
        if (strcasecmp(k, key) == 0) {
            char* v = strtok(NULL, "=");
            ret = malloc(strlen(v) + 1);
            strcpy(ret, v);
            printf("CONF ---- key: %s, value: %s\n", k, v);
            break;
        }
    }
    fclose(f);
    return ret;
 }