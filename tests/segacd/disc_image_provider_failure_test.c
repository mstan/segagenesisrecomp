/* Inject one real provider-boundary error. The complete existing reader suite
 * then verifies portable restart, resident bytes and corruption rejection. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include <openssl/evp.h>
static unsigned failure_hit;
#if SEGACD_TEST_FAIL_init
static EVP_MD_CTX *fail_init(void)
{
    if(!failure_hit){++failure_hit;return NULL;}
    return EVP_MD_CTX_new();
}
#define EVP_MD_CTX_new fail_init
#elif SEGACD_TEST_FAIL_update
static unsigned update_calls;
static int fail_update(EVP_MD_CTX *context,const void *bytes,size_t size)
{
    if(++update_calls==2){++failure_hit;return 0;}
    return EVP_DigestUpdate(context,bytes,size);
}
#define EVP_DigestUpdate fail_update
#elif SEGACD_TEST_FAIL_final
static int fail_final(EVP_MD_CTX *context,unsigned char *digest,unsigned *size)
{
    int result=EVP_DigestFinal_ex(context,digest,size);
    if(!failure_hit){++failure_hit;return 0;}
    return result;
}
#define EVP_DigestFinal_ex fail_final
#else
#error Select a provider failure
#endif
#include "../../runner/cd/disc_image.c"
#define main reader_suite
#include "disc_image_reader_test.c"
#undef main
int main(void)
{
    int result=reader_suite();
    CHECK(failure_hit==1);
    return result;
}
