#pragma once
#include <stddef.h>
#include <stdint.h>

/* Stable campaign IDs never index native Sonic 3 tables. */
enum { TR_PACK_S1=1, TR_PACK_S2=2, TR_SLOTS=8, TR_SAVE_BYTES=256 };
enum { TR_EMPTY, TR_ACTIVE, TR_COMPLETE };
enum { TR_SONIC_TAILS, TR_SONIC, TR_TAILS, TR_KNUCKLES };
typedef struct TrStage {
    uint16_t id, native_id;
    uint8_t pack, act;
    const char *name;
} TrStage;
typedef struct TrSlot {
    uint16_t stage;
    uint8_t state, packs, character, chaos, super, entered_sk;
} TrSlot;
typedef struct TrCampaign { TrSlot slots[TR_SLOTS]; } TrCampaign;
extern const TrStage tr_stages[];
extern const unsigned tr_stage_count;
const TrStage *tr_stage(unsigned id);
unsigned tr_first_stage(unsigned packs);
unsigned tr_next_import(unsigned id, unsigned packs);
int tr_slot_valid(const TrSlot *slot);
int tr_campaign_valid(const TrCampaign *data);
int tr_campaign_new(TrCampaign *data,unsigned slot,unsigned packs,unsigned character);
int tr_campaign_delete(TrCampaign *data,unsigned slot);
int tr_campaign_advance(TrSlot *slot,unsigned id);
int tr_campaign_collect(TrSlot *slot,unsigned chaos,unsigned super);
int tr_campaign_complete(TrSlot *slot);
int tr_campaign_select_zone(TrSlot *slot,unsigned id);
int tr_campaign_encode(const TrCampaign *data,uint32_t sequence,uint8_t bytes[TR_SAVE_BYTES]);
int tr_campaign_decode(const uint8_t *bytes,size_t size,TrCampaign *data,
                       uint32_t *sequence,char *error,size_t error_size);
