#pragma once
#include <stddef.h>
#include <stdint.h>

/* This runner's stock S3K .srm is a 16 KiB raw-bus image (including unused
 * even lanes). Preserve that existing representation exactly. Extensions
 * are file data only and never increase the cartridge's mapped SRAM. */
enum { TR_SRAM_BASE=0x4000, TR_SRAM_LIMIT=1024*1024 };
typedef struct TrSram {
    uint8_t *original, *tail;
    size_t original_size, tail_size;
    char path[1024], error[192];
    uint64_t generation;
    int exists, ready, read_only, extensions_valid;
} TrSram;
void tr_sram_dispose(TrSram *s);
int tr_sram_open(TrSram *s,const char *path,uint8_t base[TR_SRAM_BASE]);
int tr_sram_commit(TrSram *s,const uint8_t base[TR_SRAM_BASE]);
/* Version and bytes are returned without interpreting a record. Unknown tags
 * and versions survive writes byte-for-byte. Duplicate tags are invalid. */
const uint8_t *tr_sram_get(const TrSram *s,const char tag[4],unsigned *version,size_t *size);
int tr_sram_set(TrSram *s,const char tag[4],unsigned version,const void *data,size_t size);
typedef struct TrSramRecord {
    char tag[4];
    unsigned version;
    const void *data;
    size_t size;
} TrSramRecord;
/* Stage related records together: an allocation/validation failure leaves
 * every record and the dirty generation unchanged. */
int tr_sram_set_many(TrSram *s,const TrSramRecord *records,size_t count);

/* GameSpec persistence hooks, installed even while the mod is disabled so
 * an ordinary S3K play session cannot truncate expansion progress. */
int tr_sram_load(const char *path,uint8_t *base,size_t size);
int tr_sram_save(const char *path,const uint8_t *base,size_t size);
uint64_t tr_sram_generation(void);
TrSram *tr_sram_current(void);
