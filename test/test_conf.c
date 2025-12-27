#include "conf.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char const *argv[])
{
    char* v =  get_config("appendfsync");
    
    if (strcmp(v, "everysec") != 0) return 1;
    else return 0;
}
