#ifndef CRYPTO_H
#define CRYPTO_H

#include <string.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
// 32字节
void compute_sha256(const char* data, size_t len, unsigned char out[]);

void printhash(unsigned char out[], size_t hashlen);

int verify_sha256(const char* data, size_t len, unsigned char expected_hash[]);

#endif