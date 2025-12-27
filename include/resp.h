#ifndef RESP_H
#define RESP_H
// 统一
struct RespShared {
    char *ok;
    char *pong;
    char* err;
    char* keyNotFound;
    char* bye;
    char* invalidCommand;
    char* sync;
    char* dupkey;
    char* ping;
    char* info;
    char* valmissed;
};
extern struct RespShared resp;

int resp_decode(const char *resp, int *argc_out, char** argv_out[]);
char* respEncodeArrayString(int argc, char* argv[]);
char* resp_str(const char *resp);
char* respEncodeBulkString(const char* s);
char* respParse(char* buf, size_t len);

#endif