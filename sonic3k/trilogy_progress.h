#pragma once
#include "trilogy_campaign.h"
#include "trilogy_sram.h"

enum { TR_CHAPTER_S1, TR_CHAPTER_S2, TR_CHAPTER_S3K, TR_CHAPTERS };
enum { TR_RECORD_CAMP=1, TR_RECORD_S1=2, TR_RECORD_S2=4 };
typedef struct TrChapterProgress {
    uint16_t stage;
    uint8_t checkpoint, cleared;
    uint32_t token;
} TrChapterProgress;
typedef struct TrProgress {
    TrCampaign campaign;
    TrChapterProgress chapters[TR_CHAPTERS][TR_SLOTS];
    uint32_t tokens[TR_SLOTS], next_token;
    unsigned present, protected_records, dirty;
    char error[192];
} TrProgress;
typedef struct TrResume {
    uint16_t stage;
    uint8_t checkpoint, unavailable_packs;
} TrResume;

/* No donor is needed to parse progress. Future chapter versions are retained
 * by the container and skipped independently of known chapters. */
int tr_progress_load(TrProgress *p,const TrSram *s);
int tr_progress_store(TrProgress *p,TrSram *s);
int tr_progress_new(TrProgress *p,unsigned slot,unsigned packs,unsigned character);
int tr_progress_delete(TrProgress *p,unsigned slot);
/* Existing native saves retain their current chapter. Adding a donor to an
 * existing campaign is explicit; its presence on disk alone never enrolls it. */
int tr_progress_attach_native(TrProgress *p,unsigned slot,const TrSlot *native);
int tr_progress_add_pack(TrProgress *p,unsigned slot,unsigned pack);
int tr_progress_resolve(const TrProgress *p,unsigned slot,unsigned available,TrResume *out);
int tr_progress_checkpoint(TrProgress *p,unsigned slot,unsigned stage,unsigned checkpoint);
int tr_progress_finish_import(TrProgress *p,unsigned slot,unsigned available,TrResume *next);
int tr_progress_finish_native(TrProgress *p,unsigned slot);
int tr_progress_select_zone(TrProgress *p,unsigned slot,unsigned stage,unsigned available);
int tr_progress_collect(TrProgress *p,unsigned slot,unsigned chaos,unsigned super);
/* Native ten-byte Saved_data slot, as loaded/maintained by stock S3K. These
 * helpers never rewrite its SRAM representation or checksum. */
int tr_native_slot_decode(const uint8_t bytes[10],TrSlot *out);
int tr_progress_sync_native(TrProgress *p,unsigned slot,const uint8_t bytes[10]);
