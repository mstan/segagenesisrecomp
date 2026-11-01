/* Real provider failures restart BOTH complete identities from the beginning. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
static unsigned failure_hit,provider_calls;
#if SEGACD_TEST_FAIL_update
static unsigned fail_at=2;
#else
static unsigned fail_at=1;
#endif
static int should_fail(void)
{if(++provider_calls==fail_at){++failure_hit;return 1;}return 0;}
#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#define FAILED_PROVIDER ((NTSTATUS)0xC0000001L)
#if SEGACD_TEST_FAIL_init
static NTSTATUS WINAPI fail_init(BCRYPT_ALG_HANDLE algorithm,BCRYPT_HASH_HANDLE *hash,
                                 PUCHAR object,ULONG object_bytes,PUCHAR secret,
                                 ULONG secret_bytes,ULONG flags)
{
    if(should_fail())return FAILED_PROVIDER;
    return BCryptCreateHash(algorithm,hash,object,object_bytes,secret,secret_bytes,flags);
}
#define BCryptCreateHash fail_init
#elif SEGACD_TEST_FAIL_update
static NTSTATUS WINAPI fail_update(BCRYPT_HASH_HANDLE hash,PUCHAR bytes,ULONG size,ULONG flags)
{if(should_fail())return FAILED_PROVIDER;return BCryptHashData(hash,bytes,size,flags);}
#define BCryptHashData fail_update
#elif SEGACD_TEST_FAIL_final
static NTSTATUS WINAPI fail_final(BCRYPT_HASH_HANDLE hash,PUCHAR digest,ULONG size,ULONG flags)
{
    NTSTATUS result=BCryptFinishHash(hash,digest,size,flags);
    return should_fail()?FAILED_PROVIDER:result;
}
#define BCryptFinishHash fail_final
#else
#error Select a provider failure
#endif
#else
#include <openssl/evp.h>
#if SEGACD_TEST_FAIL_init
static EVP_MD_CTX *fail_init(void)
{if(should_fail())return NULL;return EVP_MD_CTX_new();}
#define EVP_MD_CTX_new fail_init
#elif SEGACD_TEST_FAIL_update
static int fail_update(EVP_MD_CTX *context,const void *bytes,size_t size)
{if(should_fail())return 0;return EVP_DigestUpdate(context,bytes,size);}
#define EVP_DigestUpdate fail_update
#elif SEGACD_TEST_FAIL_final
static int fail_final(EVP_MD_CTX *context,unsigned char *digest,unsigned *size)
{int result=EVP_DigestFinal_ex(context,digest,size);return should_fail()?0:result;}
#define EVP_DigestFinal_ex fail_final
#else
#error Select a provider failure
#endif
#endif
#include "../../runner/cd/disc_image.c"
#define main reader_suite
#include "disc_image_reader_test.c"
#undef main
int main(void)
{
    int result=reader_suite();CHECK(failure_hit==1);
    /* A failure specifically in the whole-image context must restart payload
     * and whole streams together; it cannot publish the successful first hash. */
    const char*path="provider_expected_fixture.sgcd";char error[96];
    make_fixture(path);provider_calls=failure_hit=0;
#if SEGACD_TEST_FAIL_update
    fail_at=3; /* prefix, payload block, WHOLE block */
#else
    fail_at=2; /* payload then WHOLE init/final */
#endif
    SegaCDImage*image=segacd_image_open_expected(path,known_image_digest,error,sizeof error);
    CHECK(image&&failure_hit==1);
    CHECK(!memcmp(segacd_image_sha256(image),known_digest,32));
    CHECK(!memcmp(segacd_image_whole_sha256(image),known_image_digest,32));
    uint8_t sector[RAW];CHECK(segacd_image_read(image,0,sector)&&all_byte(sector,RAW,0x11));
    segacd_image_close(image);CHECK(!remove(path));return result;
}
