#ifndef UTIL_H
#define UTIL_H


/* 可视化打印buf */
void printBuf(const char* prefix, const char* buf, int len);

char* fullPath(char* path);
long long mstime(void) ;
void strim(char *s);
#endif