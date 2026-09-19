#pragma once
#include "sonic2_campaign.h"

/* Settings live beside the executable, as with native S3&K SRAM. Local paths
 * stay relative to that directory; paths elsewhere retain their full location. */
void s2_campaign_file_load(const char *settings_path);
const S2CampaignStore *s2_campaign_file_store(void);
const char *s2_campaign_file_error(void);
int s2_campaign_file_select(const char *path);
int s2_campaign_file_commit(const S2CampaignData *candidate);
