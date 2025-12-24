#include "crypto.h"
#include <stdio.h>
#include <assert.h>
#include "log.h"
/**
 * @brief 
 * 
 * @param [in] data 
 * @param [in] len 
 * @param [in] out 32字节
 */
void compute_sha256(const char* data, size_t len, unsigned char out[])
{
    log_debug("Compute sha256 for data len [%zu]", len);
    // MD : message digest
    EVP_MD_CTX *ctx = EVP_MD_CTX_new(); // md上下文
    assert(ctx);
    // ex: extended
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1) {
        log_error("Error digest init!");
        EVP_MD_CTX_free(ctx);
        return;
    }
    if (EVP_DigestUpdate(ctx, data, len) != 1) {
        log_error("Error updating digest!");
        EVP_MD_CTX_free(ctx);
        return;
    }

    unsigned int hash_len = 0;  // 哈希结果的长度
    if (EVP_DigestFinal_ex(ctx, out, &hash_len) != 1) {
        log_error("Error finalizing digest.");
        EVP_MD_CTX_free(ctx);
        return;
    }
    log_debug("compute_hash len: %zu", hash_len);
    // printhash(out, hash_len);
    // 释放上下文资源
    EVP_MD_CTX_free(ctx);


}
void printhash(unsigned char hash[], size_t hashlen)
{
    for (size_t i = 0; i < hashlen; i++)
    {
        log_debug("%02x", hash[i]);
    }
    log_debug("\n");    
}

/**
 * @brief 校验256
 * 
 * @param [in] data 
 * @param [in] len 
 * @param [in] expected_hash 
 * @return int 
 */
int verify_sha256(const char* data, size_t len, unsigned char expected_hash[]) 
{
    unsigned char computed_hash[SHA256_DIGEST_LENGTH];
    compute_sha256(data, len, computed_hash);
    if (memcmp(computed_hash, expected_hash, SHA256_DIGEST_LENGTH) == 0) {
        log_debug("Verify success!");
        return 1;
    } else {
        log_error("Verify failed ! Expected : ");
        printhash(expected_hash, SHA256_DIGEST_LENGTH);
        log_error("Verify failed ! Computed : ");
        printhash(computed_hash, SHA256_DIGEST_LENGTH);
        
        return 0; // 校验失败 
    }
}