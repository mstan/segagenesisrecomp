#pragma once
#include <stddef.h>
#include <stdint.h>

enum { S2_SAVE_SLOTS = 8, S2_CAMPAIGN_STAGES = 20, S2_CAMPAIGN_ZONES = 11,
       S2_SAVE_BYTES = 128 };
enum { S2_SAVE_EMPTY, S2_SAVE_ACTIVE, S2_SAVE_COMPLETE };
typedef struct S2CampaignStage {
    uint16_t native_id;
    uint8_t zone, act;
    const char *name;
} S2CampaignStage;
typedef struct S2CampaignSlot {
    uint8_t state, stage, emeralds, lives, continues;
} S2CampaignSlot;
typedef struct S2CampaignData { S2CampaignSlot slots[S2_SAVE_SLOTS]; } S2CampaignData;

/* Ordered campaign destinations, NOT an index into Sonic 2's sparse zone IDs.
 * Metropolis Act 3 uses native zone 5, act 0. See SONIC2_SAVE_MENU.md. */
extern const S2CampaignStage s2_campaign_stages[S2_CAMPAIGN_STAGES];
int s2_campaign_stage(uint16_t native_id);
int s2_campaign_zone_start(unsigned zone);
unsigned s2_campaign_emerald_count(unsigned mask);
int s2_campaign_valid(const S2CampaignData *data);
int s2_campaign_new(S2CampaignData *data, unsigned slot);
int s2_campaign_delete(S2CampaignData *data, unsigned slot);
int s2_campaign_advance(S2CampaignSlot *slot, uint16_t next_native_id);
int s2_campaign_collect(S2CampaignSlot *slot, unsigned emerald_mask);
int s2_campaign_complete(S2CampaignSlot *slot);
int s2_campaign_select_zone(S2CampaignSlot *slot, unsigned zone);
int s2_campaign_select_stage(S2CampaignSlot *slot, unsigned stage);

/* Fixed endian, CRC-protected, exact Sonic 2 REV01 identity; never raw structs.
 * Decoding is transactional: output and sequence are unchanged on failure. */
int s2_campaign_encode(const S2CampaignData *data, uint32_t sequence,
                       uint8_t bytes[S2_SAVE_BYTES]);
int s2_campaign_decode(const uint8_t *bytes, size_t size, S2CampaignData *data,
                       uint32_t *sequence, char *error, size_t error_size);

typedef struct S2CampaignStore {
    S2CampaignData data;
    char path[1024], error[192];
    uint8_t original[S2_SAVE_BYTES];
    uint32_t sequence;
    int ready, read_only, exists, recovered;
} S2CampaignStore;
/* Missing = empty in memory, with no file created. Invalid/future files are
 * protected. A valid backup can be inspected/played read-only, never silently
 * copied over a damaged primary. No Save does not call commit. */
int s2_campaign_open(S2CampaignStore *store, const char *path);
int s2_campaign_commit(S2CampaignStore *store, const S2CampaignData *candidate);
