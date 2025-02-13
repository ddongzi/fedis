#include "redis.h"


void notifyKeyspaceEvent(int type, char* event, robj* key, int dbid);