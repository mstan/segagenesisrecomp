#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "disc_image.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#include <share.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

#ifndef SEGACD_IMAGE_PLATFORM_SHA
#define SEGACD_IMAGE_PLATFORM_SHA 1
#endif
#ifndef SEGACD_IMAGE_MAP_RESIDENT
#define SEGACD_IMAGE_MAP_RESIDENT 1
#endif
#if defined(_WIN32) && SEGACD_IMAGE_PLATFORM_SHA
#include <windows.h>
#include <bcrypt.h>
#elif SEGACD_IMAGE_PLATFORM_SHA && defined(SEGACD_IMAGE_OPENSSL_SHA) && SEGACD_IMAGE_OPENSSL_SHA
#include <openssl/evp.h>
#endif
#if SEGACD_IMAGE_MAP_RESIDENT && defined(_WIN32)
#include <windows.h>
#include <io.h>
#elif SEGACD_IMAGE_MAP_RESIDENT && (defined(__unix__) || defined(__APPLE__))
#include <sys/mman.h>
#include <unistd.h>
#endif

enum { SECTOR_BYTES = 2352, HEADER_BYTES = 96, TRACK_BYTES = 32,
       MAX_TRACKS = 99, MAX_LEADOUT = 449849 };
enum { PREFETCH_SECTORS=512, PREFETCH_BYTES=PREFETCH_SECTORS*SECTOR_BYTES };

typedef struct {
    SegaCDImageAsyncHost host;
    void *worker;
    uint8_t *ready,*loading;
    int32_t last_lba,ready_first,job_first;
    unsigned ready_count,job_count;
    int attempted,pending,discard,job_ok,io_ready;
    intptr_t file;
    void *event;
} ImagePrefetch;

struct SegaCDImage {
    FILE *file;
    SegaCDTOC toc;
    uint64_t payload_offset;
    uint8_t digest[32];
    uint8_t whole_digest[32];
    size_t private_snapshot_bytes;
    int whole_identity_checked;
    uint8_t *resident;
    int32_t resident_first;
    size_t resident_size;
    void *mapping_base,*mapping_handle;
    size_t mapping_size;
    uint64_t read_cursor,raw_reads,resident_reads,host_seeks;
    ImagePrefetch prefetch;
    uint64_t prefetch_jobs,prefetch_hits,prefetch_failures,prefetch_cancels;
    uint64_t sync_read_ns,max_sync_read_ns;
    int measure_reads;
};

static uint64_t image_clock_ns(void)
{
#if defined(_WIN32)
    LARGE_INTEGER now,freq;
    if(!QueryPerformanceCounter(&now)||!QueryPerformanceFrequency(&freq)||freq.QuadPart<=0)return 0;
    uint64_t t=(uint64_t)now.QuadPart,f=(uint64_t)freq.QuadPart;
    return t/f*UINT64_C(1000000000)+(t%f)*UINT64_C(1000000000)/f;
#elif defined(CLOCK_MONOTONIC)
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
#else
    return 0;
#endif
}

static void prefetch_job(void *user)
{
    SegaCDImage *image=(SegaCDImage *)user;ImagePrefetch *p=&image->prefetch;
    uint64_t offset=image->payload_offset+(uint64_t)(p->job_first+150)*SECTOR_BYTES;
    size_t bytes=(size_t)p->job_count*SECTOR_BYTES;
    p->job_ok=0;
#if defined(_WIN32)
    /* ReOpenFile(OVERLAPPED) has no shared file cursor. The stack OVERLAPPED
     * and buffer remain alive through completion/cancellation acknowledgement.
     * https://learn.microsoft.com/windows/win32/api/winbase/nf-winbase-reopenfile */
    OVERLAPPED ov;memset(&ov,0,sizeof ov);
    ov.Offset=(DWORD)offset;ov.OffsetHigh=(DWORD)(offset>>32);ov.hEvent=(HANDLE)p->event;
    DWORD got=0;ResetEvent(ov.hEvent);
    BOOL ok=ReadFile((HANDLE)p->file,p->loading,(DWORD)bytes,&got,&ov);
    if(!ok&&GetLastError()==ERROR_IO_PENDING)
        ok=GetOverlappedResult((HANDLE)p->file,&ov,&got,TRUE);
    p->job_ok=ok&&got==bytes;
#elif defined(__unix__) || defined(__APPLE__)
    size_t done=0;
    while(done<bytes) {
        uint64_t at=offset+done;if((uint64_t)(off_t)at!=at)return;
        ssize_t got=pread((int)p->file,p->loading+done,bytes-done,(off_t)at);
        if(got<0&&errno==EINTR)continue;
        if(got<=0)return;
        done+=(size_t)got;
    }
    p->job_ok=1;
#else
    (void)offset;(void)bytes;
#endif
}

void segacd_image_prefetch_cancel(SegaCDImage *image)
{
    if(!image)return;
    ImagePrefetch *p=&image->prefetch;
    if(p->ready_count||p->pending)++image->prefetch_cancels;
    p->ready_count=0;p->last_lba=INT32_MIN;
    if(p->pending)p->discard=1;
#if defined(_WIN32)
    if(p->io_ready)(void)CancelIoEx((HANDLE)p->file,NULL);
#endif
}

static void prefetch_close(SegaCDImage *image)
{
    ImagePrefetch *p=&image->prefetch;segacd_image_prefetch_cancel(image);
    if(p->worker)p->host.destroy(p->worker); /* joins before freeing job inputs */
    free(p->ready);free(p->loading);
    if(p->io_ready) {
#if defined(_WIN32)
        CloseHandle((HANDLE)p->file);CloseHandle((HANDLE)p->event);
#elif defined(__unix__) || defined(__APPLE__)
        close((int)p->file);
#endif
    }
    memset(p,0,sizeof *p);p->last_lba=INT32_MIN;
}

void segacd_image_set_async_host(SegaCDImage *image,const SegaCDImageAsyncHost *host)
{
    if(!image)return;
    prefetch_close(image);
    if(host&&host->create&&host->try_start&&host->busy&&host->destroy)
        image->prefetch.host=*host;
}

static int prefetch_init(SegaCDImage *image)
{
    ImagePrefetch *p=&image->prefetch;
    if(p->attempted)return p->worker!=NULL;
    p->attempted=1;
    if(!p->host.create)return 0;
#if defined(_WIN32)
    HANDLE original=(HANDLE)_get_osfhandle(_fileno(image->file));
    HANDLE file=ReOpenFile(original,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                           FILE_FLAG_OVERLAPPED|FILE_FLAG_SEQUENTIAL_SCAN);
    if(file==INVALID_HANDLE_VALUE)return 0;
    HANDLE event=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!event){CloseHandle(file);return 0;}
    p->file=(intptr_t)file;p->event=event;p->io_ready=1;
#elif defined(__unix__) || defined(__APPLE__)
    int fd=dup(fileno(image->file));if(fd<0)return 0;
    p->file=fd;p->io_ready=1;
#else
    return 0;
#endif
    p->ready=(uint8_t *)malloc(PREFETCH_BYTES);p->loading=(uint8_t *)malloc(PREFETCH_BYTES);
    if(p->ready&&p->loading)p->worker=p->host.create();
    if(!p->worker) {
        SegaCDImageAsyncHost host=p->host;
        prefetch_close(image);p->host=host;p->attempted=1;return 0;
    }
    return 1;
}

static int prefetch_contains(int32_t first,unsigned count,int32_t lba)
{return lba>=first&&(uint64_t)((int64_t)lba-first)<count;}

static void prefetch_poll(SegaCDImage *image,int32_t lba)
{
    ImagePrefetch *p=&image->prefetch;
    if(!p->pending||p->host.busy(p->worker))return;
    if(!p->discard&&p->job_ok&&prefetch_contains(p->ready_first,p->ready_count,lba))
        return; /* retain old ready block until demand reaches the next block */
    p->pending=0;
    if(!p->discard&&p->job_ok) {
        uint8_t *old=p->ready;p->ready=p->loading;p->loading=old;
        p->ready_first=p->job_first;p->ready_count=p->job_count;
    } else if(!p->discard)++image->prefetch_failures;
    p->discard=0;
}

static void prefetch_schedule(SegaCDImage *image,int32_t next,int sequential)
{
    ImagePrefetch *p=&image->prefetch;
    if(!sequential||p->pending||next>=image->toc.leadout)return;
    if(prefetch_contains(p->ready_first,p->ready_count,next)) {
        int32_t end=p->ready_first+(int32_t)p->ready_count;
        if(end-next>PREFETCH_SECTORS/4)return;
        next=end;
    }
    if(next>=image->toc.leadout||segacd_image_resident_sector(image,next))return;
    if(!prefetch_init(image))return;
    unsigned count=(unsigned)(image->toc.leadout-next);
    if(count>PREFETCH_SECTORS)count=PREFETCH_SECTORS;
    p->job_first=next;p->job_count=count;p->job_ok=0;p->discard=0;
    if(p->host.try_start(p->worker,prefetch_job,image)) {
        p->pending=1;++image->prefetch_jobs;
    }
}

static int map_resident(SegaCDImage *image,uint64_t offset,size_t bytes)
{
#if SEGACD_IMAGE_MAP_RESIDENT && defined(_WIN32)
    SYSTEM_INFO info;GetSystemInfo(&info);
    uint64_t aligned=offset-offset%info.dwAllocationGranularity;
    size_t prefix=(size_t)(offset-aligned),length=prefix+bytes;
    HANDLE file=(HANDLE)_get_osfhandle(_fileno(image->file));
    if(file==INVALID_HANDLE_VALUE)return 0;
    HANDLE mapping=CreateFileMappingW(file,NULL,PAGE_READONLY,0,0,NULL);
    if(!mapping)return 0;
    void *view=MapViewOfFile(mapping,FILE_MAP_READ,(DWORD)(aligned>>32),(DWORD)aligned,length);
    if(!view){CloseHandle(mapping);return 0;}
    image->mapping_handle=mapping;
#elif SEGACD_IMAGE_MAP_RESIDENT && (defined(__unix__) || defined(__APPLE__))
    long page=sysconf(_SC_PAGESIZE);if(page<=0)return 0;
    uint64_t aligned=offset-offset%(uint64_t)page;
    size_t prefix=(size_t)(offset-aligned),length=prefix+bytes;
    if((uint64_t)(off_t)aligned!=aligned)return 0;
    void *view=mmap(NULL,length,PROT_READ,MAP_PRIVATE,fileno(image->file),(off_t)aligned);
    if(view==MAP_FAILED)return 0;
    if(!view){(void)munmap(view,length);return 0;}
#else
    (void)image;(void)offset;(void)bytes;return 0;
#endif
#if SEGACD_IMAGE_MAP_RESIDENT && (defined(_WIN32) || defined(__unix__) || defined(__APPLE__))
    image->mapping_base=view;image->mapping_size=length;
    image->resident=(uint8_t *)view+prefix;return 1;
#endif
}

typedef struct {
    uint32_t state[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t used;
} Sha256;

static const uint32_t sha_k[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,
    0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,
    0x76f988dau,0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,
    0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,
    0x92722c85u,0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,
    0x106aa070u,0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,
    0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,
    0xc67178f2u
};

static uint32_t rotr32(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void sha_transform(Sha256 *sha, const uint8_t block[64])
{
    uint32_t w[64], a,b,c,d,e,f,g,h;
    unsigned i;
    for (i = 0; i < 16; ++i) {
        const size_t p = (size_t)i * 4u;
        w[i] = ((uint32_t)block[p] << 24) | ((uint32_t)block[p+1] << 16) |
               ((uint32_t)block[p+2] << 8) | (uint32_t)block[p+3];
    }
    for (; i < 64; ++i) {
        const uint32_t s0 = rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15] >> 3);
        const uint32_t s1 = rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    a=sha->state[0]; b=sha->state[1]; c=sha->state[2]; d=sha->state[3];
    e=sha->state[4]; f=sha->state[5]; g=sha->state[6]; h=sha->state[7];
    for (i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25);
        const uint32_t ch = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + ch + sha_k[i] + w[i];
        const uint32_t s0 = rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    sha->state[0]+=a; sha->state[1]+=b; sha->state[2]+=c; sha->state[3]+=d;
    sha->state[4]+=e; sha->state[5]+=f; sha->state[6]+=g; sha->state[7]+=h;
}

static void sha_init(Sha256 *sha)
{
    static const uint32_t initial[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    memcpy(sha->state, initial, sizeof(initial));
    sha->bytes = 0;
    sha->used = 0;
}

static void sha_update(Sha256 *sha, const uint8_t *data, size_t size)
{
    sha->bytes += (uint64_t)size;
    while (size != 0) {
        size_t take = sizeof(sha->block) - sha->used;
        if (take > size) take = size;
        memcpy(sha->block + sha->used, data, take);
        sha->used += take;
        data += take;
        size -= take;
        if (sha->used == sizeof(sha->block)) {
            sha_transform(sha, sha->block);
            sha->used = 0;
        }
    }
}

static void sha_final(Sha256 *sha, uint8_t out[32])
{
    const uint64_t bit_count = sha->bytes * 8u;
    unsigned i;
    sha->block[sha->used++] = 0x80;
    if (sha->used > 56) {
        memset(sha->block + sha->used, 0, sizeof(sha->block) - sha->used);
        sha_transform(sha, sha->block);
        sha->used = 0;
    }
    memset(sha->block + sha->used, 0, 56 - sha->used);
    for (i = 0; i < 8; ++i)
        sha->block[63u-i] = (uint8_t)(bit_count >> (i*8u));
    sha_transform(sha, sha->block);
    for (i = 0; i < 8; ++i) {
        out[i*4u] = (uint8_t)(sha->state[i] >> 24);
        out[i*4u+1u] = (uint8_t)(sha->state[i] >> 16);
        out[i*4u+2u] = (uint8_t)(sha->state[i] >> 8);
        out[i*4u+3u] = (uint8_t)sha->state[i];
    }
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t lei32(const uint8_t *p)
{
    const uint32_t value = le32(p);
    if (value <= INT32_MAX) return (int32_t)value;
    return -1 - (int32_t)(UINT32_MAX - value);
}

static uint64_t le64(const uint8_t *p)
{
    return (uint64_t)le32(p) | ((uint64_t)le32(p+4) << 32);
}

static void set_error(char *error, size_t capacity, const char *message)
{
    if (capacity == 0 || error == NULL) return;
    (void)snprintf(error, capacity, "%s", message);
}

static int read_exact(FILE *file, void *data, size_t size)
{
    return fread(data, 1, size, file) == size;
}

static int all_zero(const uint8_t *data, size_t size)
{
    size_t i;
    for (i = 0; i < size; ++i) if (data[i] != 0) return 0;
    return 1;
}

/* Provider selection is host work. Every provider sees the complete stream;
 * no partial result or residency is published after a provider failure. */
typedef struct {
    Sha256 portable;
#if defined(_WIN32) && SEGACD_IMAGE_PLATFORM_SHA
    BCRYPT_ALG_HANDLE algorithm;
    BCRYPT_HASH_HANDLE hash;
#elif SEGACD_IMAGE_PLATFORM_SHA && defined(SEGACD_IMAGE_OPENSSL_SHA) && SEGACD_IMAGE_OPENSSL_SHA
    EVP_MD_CTX *context;
#endif
} PayloadHash;
static int payload_hash_init(PayloadHash *hash,int platform)
{
    memset(hash,0,sizeof *hash);sha_init(&hash->portable);
#if defined(_WIN32) && SEGACD_IMAGE_PLATFORM_SHA
    if(platform && (BCryptOpenAlgorithmProvider(&hash->algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0 ||
        BCryptCreateHash(hash->algorithm,&hash->hash,NULL,0,NULL,0,0)<0))return 0;
#elif SEGACD_IMAGE_PLATFORM_SHA && defined(SEGACD_IMAGE_OPENSSL_SHA) && SEGACD_IMAGE_OPENSSL_SHA
    if(platform) {
        hash->context=EVP_MD_CTX_new();
        if(!hash->context || EVP_DigestInit_ex(hash->context,EVP_sha256(),NULL)!=1)return 0;
    }
#else
    (void)platform;
#endif
    return 1;
}
static int payload_hash_update(PayloadHash *hash,const uint8_t *bytes,size_t size)
{
#if defined(_WIN32) && SEGACD_IMAGE_PLATFORM_SHA
    if(hash->hash)return BCryptHashData(hash->hash,(PUCHAR)bytes,(ULONG)size,0)>=0;
#elif SEGACD_IMAGE_PLATFORM_SHA && defined(SEGACD_IMAGE_OPENSSL_SHA) && SEGACD_IMAGE_OPENSSL_SHA
    if(hash->context)return EVP_DigestUpdate(hash->context,bytes,size)==1;
#endif
    sha_update(&hash->portable,bytes,size);return 1;
}
static int payload_hash_final(PayloadHash *hash,uint8_t digest[32])
{
#if defined(_WIN32) && SEGACD_IMAGE_PLATFORM_SHA
    if(hash->hash)return BCryptFinishHash(hash->hash,digest,32,0)>=0;
#elif SEGACD_IMAGE_PLATFORM_SHA && defined(SEGACD_IMAGE_OPENSSL_SHA) && SEGACD_IMAGE_OPENSSL_SHA
    if(hash->context) {
        unsigned bytes=0;
        return EVP_DigestFinal_ex(hash->context,digest,&bytes)==1&&bytes==32;
    }
#endif
    sha_final(&hash->portable,digest);return 1;
}
static void payload_hash_close(PayloadHash *hash)
{
#if defined(_WIN32) && SEGACD_IMAGE_PLATFORM_SHA
    if(hash->hash)BCryptDestroyHash(hash->hash);
    if(hash->algorithm)BCryptCloseAlgorithmProvider(hash->algorithm,0);
#elif SEGACD_IMAGE_PLATFORM_SHA && defined(SEGACD_IMAGE_OPENSSL_SHA) && SEGACD_IMAGE_OPENSSL_SHA
    EVP_MD_CTX_free(hash->context);
#else
    (void)hash;
#endif
}
static int hash_payload_pass(SegaCDImage *image,uint64_t offset,uint64_t size,
                             uint8_t digest[32],const uint8_t *prefix,
                             uint8_t whole_digest[32],int platform)
{
    FILE *file=image->file;
    PayloadHash hash,whole;
    uint8_t buffer[32768];
    uint64_t remaining=size;
    int result = 0;
    memset(&whole,0,sizeof whole);
    image->read_cursor=UINT64_MAX;
    if(!payload_hash_init(&hash,platform)){result=-1;goto done;}
    if(prefix&&(!payload_hash_init(&whole,platform)||
                !payload_hash_update(&whole,prefix,(size_t)offset))){result=-1;goto done;}
    if (offset > (uint64_t)LONG_MAX || fseek(file, (long)offset, SEEK_SET) != 0) goto done;
    while (remaining != 0) {
        const size_t amount = remaining < sizeof(buffer) ? (size_t)remaining : sizeof(buffer);
        if (!read_exact(file, buffer, amount)) goto done;
        if (image->resident != NULL) {
            uint64_t cache_start = (uint64_t)(image->resident_first + 150) * SECTOR_BYTES;
            uint64_t position = size - remaining;
            uint64_t first = position > cache_start ? position : cache_start;
            uint64_t end = position + amount;
            uint64_t cache_end = cache_start + image->resident_size;
            if (end > cache_end) end = cache_end;
            if (first < end) {
                uint8_t *resident=image->resident+(size_t)(first-cache_start);
                const uint8_t *source=buffer+(size_t)(first-position);
                size_t bytes=(size_t)(end-first);
                if(image->mapping_base) {
                    /* Compare/prefault EVERY published mapped byte during
                     * validation. The mounted file must remain immutable. */
                    if(memcmp(resident,source,bytes))goto done;
                }else memcpy(resident,source,bytes);
            }
        }
        if(!payload_hash_update(&hash,buffer,amount)){result=-1;goto done;}
        if(prefix&&!payload_hash_update(&whole,buffer,amount)){result=-1;goto done;}
        remaining -= amount;
    }
    result=payload_hash_final(&hash,digest)&&
        (!prefix||payload_hash_final(&whole,whole_digest))?1:-1;
done:
    payload_hash_close(&hash);
    payload_hash_close(&whole);
    return result;
}

static int hash_payload(SegaCDImage *image,uint64_t offset,uint64_t size,uint8_t digest[32],
                        const uint8_t *prefix,uint8_t whole_digest[32])
{
    int result = hash_payload_pass(image,offset,size,digest,prefix,whole_digest,1);
    /* Provider failure restarts a complete portable validation. No failed or
     * partial digest can qualify the image or expose its resident sectors. */
    if (result < 0) result = hash_payload_pass(image,offset,size,digest,prefix,whole_digest,0);
    return result == 1;
}

/* Portable ownership fallback. Only the private completed copy is parsed,
 * hashed, mapped and served. The caller never supplies a snapshot pathname.
 * This bounded fallback can cost one full-image temporary copy; Windows uses
 * enforced read sharing instead. Provider failure restarts on this same copy. */
#if !defined(_WIN32) || (defined(SEGACD_IMAGE_FORCE_SNAPSHOT) && SEGACD_IMAGE_FORCE_SNAPSHOT)
static FILE *private_snapshot(FILE *source,size_t *bytes)
{
    uint8_t buffer[32768];
    uint64_t limit=HEADER_BYTES+MAX_TRACKS*TRACK_BYTES+
        (uint64_t)(MAX_LEADOUT+150)*SECTOR_BYTES;
    if(fseek(source,0,SEEK_END))return NULL;
    long length=ftell(source);
    if(length<HEADER_BYTES||(uint64_t)length>limit||fseek(source,0,SEEK_SET))return NULL;
    FILE *owned=tmpfile();if(!owned)return NULL;
    (void)setvbuf(owned,NULL,_IOFBF,1024u*1024u);
    size_t remaining=(size_t)length;
    while(remaining) {
        size_t amount=remaining<sizeof buffer?remaining:sizeof buffer;
        if(!read_exact(source,buffer,amount)||fwrite(buffer,1,amount,owned)!=amount)goto fail;
        remaining-=amount;
    }
    if(fgetc(source)!=EOF||ferror(source)||fflush(owned)||fseek(owned,0,SEEK_SET))goto fail;
    *bytes=(size_t)length;return owned;
fail:
    fclose(owned);return NULL;
}
#endif

SegaCDImage *segacd_image_open_expected(const char *path,const uint8_t expected[32],
                                       char *error,size_t capacity)
{
    static const uint8_t magic[8] = {'S','G','C','D','I','M','G',0};
    uint8_t header[HEADER_BYTES], entry[TRACK_BYTES], digest[32];
    uint8_t prefix[HEADER_BYTES+MAX_TRACKS*TRACK_BYTES];
    size_t snapshot_bytes=0;
    SegaCDImage *image = NULL;
    FILE *file = NULL;
    long file_length;
    uint32_t count, i;
    uint64_t payload_offset, payload_size, expected_size;
    int32_t leadout;
    set_error(error, capacity, "invalid SGCDIMG1 image");
    if (path == NULL || !*path) { set_error(error, capacity, "missing image path"); return NULL; }
#ifdef _WIN32
    /* The complete payload is read sequentially for identity validation.
     * This CRT hint preserves later random sector access. */
    /* Keep the verified object immutable through close. A pre-existing writer
     * or a later write/replace attempt is rejected by Windows sharing rules.
     * https://learn.microsoft.com/cpp/c-runtime-library/reference/fsopen-wfsopen */
    file = _fsopen(path,"rbS",_SH_DENYWR);
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL) { set_error(error, capacity, "cannot open image"); return NULL; }
    /* Batch physical reads during hashing instead of issuing small CRT-sized
     * reads on a busy disk. A failed optional buffer allocation leaves the
     * default buffering usable; validation still hashes every payload byte. */
    (void)setvbuf(file, NULL, _IOFBF, 1024u * 1024u);
#if !defined(_WIN32) || (defined(SEGACD_IMAGE_FORCE_SNAPSHOT) && SEGACD_IMAGE_FORCE_SNAPSHOT)
    if(expected) {
        FILE *owned=private_snapshot(file,&snapshot_bytes);
        if(!owned){set_error(error,capacity,"cannot own immutable image snapshot");goto fail;}
        fclose(file);file=owned;
    }
#endif
    if (fseek(file, 0, SEEK_END) != 0 || (file_length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        set_error(error, capacity, "cannot size image"); goto fail;
    }
    if (!read_exact(file, header, sizeof(header))) { set_error(error, capacity, "truncated image header"); goto fail; }
    if(expected)memcpy(prefix,header,sizeof header);
    if (memcmp(header, magic, sizeof(magic)) != 0 || le32(header+8) != 1u ||
        le32(header+16) != SECTOR_BYTES || le32(header+20) < 1u || le32(header+20) > MAX_TRACKS ||
        lei32(header+24) != -150) {
        set_error(error, capacity, "unsupported or invalid image header"); goto fail;
    }
    count = le32(header+20);
    leadout = lei32(header+28);
    payload_offset = le64(header+32);
    payload_size = le64(header+40);
    expected_size = (uint64_t)HEADER_BYTES + (uint64_t)count * TRACK_BYTES;
    if (leadout < 1 || leadout > MAX_LEADOUT || le32(header+12) != expected_size ||
        payload_offset != expected_size || payload_size != ((uint64_t)(uint32_t)leadout + 150u) * SECTOR_BYTES ||
        !all_zero(header+80, 16) || expected_size > (uint64_t)LONG_MAX ||
        payload_offset > (uint64_t)LONG_MAX || payload_size > (uint64_t)LONG_MAX - payload_offset ||
        (uint64_t)file_length != payload_offset + payload_size) {
        set_error(error, capacity, "invalid image bounds or file length"); goto fail;
    }
    image = (SegaCDImage *)calloc(1, sizeof(*image));
    if (image == NULL) { set_error(error, capacity, "out of memory"); goto fail; }
    image->file = file;
    image->private_snapshot_bytes=snapshot_bytes;
    image->prefetch.last_lba=INT32_MIN;
    const char *trace=getenv("GENESIS_CD_MEDIA_TRACE");
    image->measure_reads=trace&&*trace&&strcmp(trace,"0");
    file = NULL;
    image->toc.count = count;
    image->toc.leadout = leadout;
    image->payload_offset = payload_offset;
    memcpy(image->digest, header+48, sizeof(image->digest));
    for (i = 0; i < count; ++i) {
        SegaCDTrack *track = &image->toc.track[i];
        uint32_t number, kind;
        if (!read_exact(image->file, entry, sizeof(entry))) { set_error(error, capacity, "truncated track table"); goto fail; }
        if(expected)memcpy(prefix+HEADER_BYTES+(size_t)i*TRACK_BYTES,entry,sizeof entry);
        number = le32(entry);
        kind = le32(entry+4);
        track->index00 = lei32(entry+8);
        track->start = lei32(entry+12);
        track->end = lei32(entry+16);
        if (number != i+1u || kind > 1u || !all_zero(entry+20, 12) ||
            track->index00 < -150 || track->start < 0 || track->end <= track->start || track->end > leadout ||
            (i == 0 ? (track->index00 != -150 || track->start != 0) :
             (track->index00 != image->toc.track[i-1u].end || track->start < track->index00))) {
            set_error(error, capacity, "invalid track table"); goto fail;
        }
        track->data = (uint8_t)kind;
    }
    if (image->toc.track[count-1u].end != leadout) { set_error(error, capacity, "track table does not reach leadout"); goto fail; }
    /* Keep only the first data track, not the much larger CDDA soundtrack.
     * Reuse the mandatory validation pass: no second media read/preload wait.
     * This bounded optional allocation is never required to mount an image. */
    for (i = 0; i < count; ++i) {
        const SegaCDTrack *track = &image->toc.track[i];
        uint64_t bytes = (uint64_t)(track->end - track->index00) * SECTOR_BYTES;
        if (!track->data) continue;
        if (bytes <= 256u * 1024u * 1024u) {
            uint64_t offset=payload_offset+(uint64_t)(track->index00+150)*SECTOR_BYTES;
            if(!map_resident(image,offset,(size_t)bytes))
                image->resident=(uint8_t *)malloc((size_t)bytes);
            if (image->resident != NULL) {
                image->resident_first = track->index00;
                image->resident_size = (size_t)bytes;
            }
        }
        break;
    }
    if (!hash_payload(image,payload_offset,payload_size,digest,
                       expected?prefix:NULL,image->whole_digest)) {
        set_error(error, capacity, "cannot read image payload"); goto fail;
    }
    if (memcmp(digest, image->digest, sizeof(digest)) != 0) { set_error(error, capacity, "payload SHA-256 mismatch"); goto fail; }
    if(expected&&memcmp(image->whole_digest,expected,32)) {
        set_error(error,capacity,"complete image SHA-256 mismatch");goto fail;
    }
    image->whole_identity_checked=expected!=NULL;
    set_error(error, capacity, "");
    return image;
fail:
    if (image != NULL) segacd_image_close(image);
    if (file != NULL) fclose(file);
    return NULL;
}

SegaCDImage *segacd_image_open(const char *path,char *error,size_t capacity)
{return segacd_image_open_expected(path,NULL,error,capacity);}

void segacd_image_close(SegaCDImage *image)
{
    if (image == NULL) return;
    prefetch_close(image);
    if(image->mapping_base) {
#if SEGACD_IMAGE_MAP_RESIDENT && defined(_WIN32)
        UnmapViewOfFile(image->mapping_base);CloseHandle((HANDLE)image->mapping_handle);
#elif SEGACD_IMAGE_MAP_RESIDENT && (defined(__unix__) || defined(__APPLE__))
        (void)munmap(image->mapping_base,image->mapping_size);
#endif
    }else free(image->resident);
    if (image->file != NULL) fclose(image->file);
    free(image);
}

const SegaCDTOC *segacd_image_toc(const SegaCDImage *image)
{
    return image == NULL ? NULL : &image->toc;
}

int segacd_image_read(SegaCDImage *image, int32_t lba, uint8_t raw[SECTOR_BYTES])
{
    uint8_t temporary[SECTOR_BYTES];
    uint64_t offset;
    if (image == NULL || raw == NULL || lba < -150 || lba >= image->toc.leadout) return 0;
    ImagePrefetch *p=&image->prefetch;
    int sequential=p->last_lba!=INT32_MIN&&lba==p->last_lba+1;
    if(!sequential)segacd_image_prefetch_cancel(image);
    prefetch_poll(image,lba);
    const uint8_t *resident = segacd_image_resident_sector(image, lba);
    if (resident != NULL) {
        memcpy(raw,resident,SECTOR_BYTES);++image->resident_reads;
        p->last_lba=lba;prefetch_schedule(image,lba+1,sequential);return 1;
    }
    if(prefetch_contains(p->ready_first,p->ready_count,lba)) {
        memcpy(raw,p->ready+(size_t)(lba-p->ready_first)*SECTOR_BYTES,SECTOR_BYTES);
        ++image->prefetch_hits;p->last_lba=lba;
        prefetch_schedule(image,lba+1,sequential);return 1;
    }
    offset = image->payload_offset + (uint64_t)(lba + 150) * SECTOR_BYTES;
    ++image->raw_reads;
    if(offset>(uint64_t)LONG_MAX)return 0;
    uint64_t started=image->measure_reads?image_clock_ns():0;
    int ok=0;
    if(offset!=image->read_cursor) {
        ++image->host_seeks;
        if(fseek(image->file,(long)offset,SEEK_SET)!=0)goto finished;
    }
    if(!read_exact(image->file,temporary,sizeof temporary))goto finished;
    memcpy(raw, temporary, sizeof(temporary));
    ok=1;
finished:
    if(image->measure_reads) {
        uint64_t elapsed=image_clock_ns()-started;
        image->sync_read_ns+=elapsed;
        if(elapsed>image->max_sync_read_ns)image->max_sync_read_ns=elapsed;
    }
    image->read_cursor=ok?offset+SECTOR_BYTES:UINT64_MAX;
    if(ok){p->last_lba=lba;prefetch_schedule(image,lba+1,sequential);}
    else segacd_image_prefetch_cancel(image);
    return ok;
}

const uint8_t *segacd_image_sha256(const SegaCDImage *image)
{
    return image == NULL ? NULL : image->digest;
}
const uint8_t *segacd_image_whole_sha256(const SegaCDImage *image)
{return image&&image->whole_identity_checked?image->whole_digest:NULL;}

const uint8_t *segacd_image_resident_sector(const SegaCDImage *image, int32_t lba)
{
    if (image == NULL || image->resident == NULL || lba < image->resident_first) return NULL;
    size_t index = (size_t)((int64_t)lba - image->resident_first);
    if (index >= image->resident_size / SECTOR_BYTES) return NULL;
    return image->resident + index * SECTOR_BYTES;
}

size_t segacd_image_resident_bytes(const SegaCDImage *image)
{
    return image == NULL ? 0 : image->resident_size;
}

int segacd_image_host_stats(const SegaCDImage *image,SegaCDImageHostStats *stats)
{
    if(!image||!stats)return 0;
    stats->raw_reads=image->raw_reads;stats->resident_reads=image->resident_reads;
    stats->host_seeks=image->host_seeks;stats->mapped_view_bytes=image->mapping_size;
    stats->private_resident_bytes=image->mapping_base?0:image->resident_size;
    stats->prefetch_jobs=image->prefetch_jobs;stats->prefetch_hits=image->prefetch_hits;
    stats->prefetch_failures=image->prefetch_failures;stats->prefetch_cancels=image->prefetch_cancels;
    stats->sync_read_ns=image->sync_read_ns;stats->max_sync_read_ns=image->max_sync_read_ns;
    stats->prefetch_bytes=image->prefetch.worker?2u*PREFETCH_BYTES:0;
    stats->private_snapshot_bytes=image->private_snapshot_bytes;
    stats->whole_identity_checked=image->whole_identity_checked;return 1;
}
