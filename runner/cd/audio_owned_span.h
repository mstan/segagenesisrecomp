#ifndef SEGACD_AUDIO_OWNED_SPAN_H
#define SEGACD_AUDIO_OWNED_SPAN_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t subclock;
    int16_t left,right;
} SegaCDAudioFrame;

/* Timestamp i = first + floor((phase + i*step_numerator)/denominator).
 * A constant run preserves every logical frame; it does not drop endpoints. */
typedef struct {
    uint64_t first;
    uint32_t phase,step_numerator,denominator;
    int16_t left,right;
} SegaCDAudioConstant;

/* A synchronous, single-owner reservation. Begin is read-only. No queue or
 * device observer may run until commit. The producer supplies every promised
 * frame in strictly increasing time, with no gap above one subclock second.
 * These host fields are never part of deterministic device state. */
typedef struct {
    SegaCDAudioFrame *frame;
    size_t count;
    unsigned first,mask,old_head,old_count,source;
    uint64_t first_subclock,last_subclock;
    void *owner;
    SegaCDAudioConstant constant;
    uint8_t constant_run;
} SegaCDAudioWriteGrant;

typedef void (*SegaCDAudioTimedSample)(void *,uint64_t,int16_t,int16_t);
typedef struct {
    /* Guard before calling begin: replacing either legacy binding revokes the
     * capability without dereferencing its possibly retired user context. */
    SegaCDAudioTimedSample legacy;
    void *legacy_user;
    void *user;
    int (*begin)(void *,uint64_t first,uint64_t last,size_t count,SegaCDAudioWriteGrant *);
    void (*commit)(void *,const SegaCDAudioWriteGrant *);
    /* Read-only reservation under the same no-observer contract. The source
     * advances ALL authoritative device state before the existing commit. */
    int (*begin_constant)(void *,const SegaCDAudioConstant *,size_t,SegaCDAudioWriteGrant *);
} SegaCDAudioOwnedSink;

static inline SegaCDAudioFrame *segacd_audio_grant_frame(const SegaCDAudioWriteGrant *g,size_t i)
{return &g->frame[(g->first+(unsigned)i)&g->mask];}
#endif
