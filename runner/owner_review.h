#ifndef GENESIS_OWNER_REVIEW_H
#define GENESIS_OWNER_REVIEW_H

#include <string.h>

/* Explicit offline interactive review, independent of observation rings.
 * A pending step is consumed only after a complete normal frontend frame. */
typedef struct GenesisOwnerReview {
    int enabled, paused;
    unsigned remaining, requested;
} GenesisOwnerReview;
enum { GENESIS_OWNER_REVIEW_MAX_STEP = 8 };
enum { OWNER_REVIEW_OK, OWNER_REVIEW_DISABLED, OWNER_REVIEW_ONLINE,
       OWNER_REVIEW_BUSY, OWNER_REVIEW_INVALID };

/* Only a small JSON integer is accepted; omission means one frame. */
static inline int genesis_owner_review_frames(const char *value)
{
    if (!value) return 1;
    while (*value==' ' || *value=='\t' || *value=='\r' || *value=='\n' || *value==':') ++value;
    if (*value<'1' || *value>'8') return 0;
    int count=*value++-'0';
    while (*value==' ' || *value=='\t' || *value=='\r' || *value=='\n') ++value;
    return !*value || *value==',' || *value=='}' ? count : 0;
}

static inline void genesis_owner_review_init(GenesisOwnerReview *s, const char *value)
{
    memset(s, 0, sizeof *s);
    s->enabled = value && strcmp(value, "1") == 0;
    s->paused = s->enabled;
}

static inline int genesis_owner_review_request(GenesisOwnerReview *s,
                                               const char *action, int frames,
                                               int online)
{
    if (!s->enabled) return OWNER_REVIEW_DISABLED;
    if (online) return OWNER_REVIEW_ONLINE;
    if (s->remaining) return OWNER_REVIEW_BUSY;
    if (!strcmp(action, "pause")) s->paused = 1;
    else if (!strcmp(action, "play")) s->paused = 0;
    else if (!strcmp(action, "step") && frames >= 1 &&
             frames <= GENESIS_OWNER_REVIEW_MAX_STEP) {
        s->paused = 1;
        s->remaining = s->requested = (unsigned)frames;
    } else return OWNER_REVIEW_INVALID;
    return OWNER_REVIEW_OK;
}

static inline int genesis_owner_review_hold(const GenesisOwnerReview *s, int online)
{ return s->enabled && s->paused && !online; }

static inline int genesis_owner_review_can_advance(const GenesisOwnerReview *s, int online)
{ return !genesis_owner_review_hold(s, online) || s->remaining != 0; }

static inline unsigned genesis_owner_review_frame_done(GenesisOwnerReview *s)
{
    if (s->enabled && s->paused && s->remaining && --s->remaining == 0) {
        unsigned completed = s->requested;
        s->requested = 0;
        return completed;
    }
    return 0;
}
#endif
