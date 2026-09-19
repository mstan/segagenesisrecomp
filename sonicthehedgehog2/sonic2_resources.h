#pragma once
#include "sonic2_donor_assets.h"
enum { S2_RESOURCE_AMY, S2_RESOURCE_SK, S2_RESOURCE_COUNT };
/* Banks contain only normalized gameplay art, never a mapped guest ROM. */
void s2_resources_load(const char *settings_path);
int s2_resource_set(unsigned resource, const char *path);
int s2_resource_verified(unsigned resource);
const S2DonorBank *s2_resource_bank(unsigned resource);
const char *s2_resource_error(unsigned resource);
void s2_resources_shutdown(void);
