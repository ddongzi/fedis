#ifndef CONF_H
#define CONF_H

char* get_config(char* filename, const char* key);
void update_config(const char* filename, const char* key, const char* value);

#endif