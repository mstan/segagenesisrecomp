/* Shared typed audio timeline operations. Callers keep their actual clock,
 * tie-break and FM-pairing policy; this does not invent sound-driver semantics.
 * A NULL sort scratch probes an already ordered stream without allocation.
 * If merging is needed, return 0 before changing events. Otherwise return 1.
 * Non-NULL scratch is distinct and has room for count typed events. */
#ifndef GENESIS_AUDIO_TIMELINE_H
#define GENESIS_AUDIO_TIMELINE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
static inline int audio_timeline_scale(uint64_t ticks,uint32_t from_hz,
    uint32_t to_hz,uint64_t *out)
{
    if(!out || !from_hz || !to_hz)return 0;
    uint64_t whole=ticks/from_hz,part=ticks%from_hz;
    uint64_t tail=part*(uint64_t)to_hz/from_hz;
    if(whole>(UINT64_MAX-tail)/to_hz)return 0;
    *out=whole*to_hz+tail;return 1;
}
#define GENESIS_AUDIO_TIMELINE_DEFINE(name,type,le) \
static int name(type *events,type *scratch,size_t count) \
{ \
    if(count<2)return 1; \
    type *input=events,*output=scratch; \
    for(;;){ \
        size_t at=0;unsigned merges=0; \
        while(at<count){ \
            size_t mid=at+1; \
            while(mid<count && le(&input[mid-1],&input[mid]))++mid; \
            if(mid==count){ \
                if(!merges){if(input!=events)memcpy(events,input,count*sizeof *events);return 1;} \
                memcpy(output+at,input+at,(count-at)*sizeof *events);break; \
            } \
            if(!output)return 0; \
            size_t end=mid+1; \
            while(end<count && le(&input[end-1],&input[end]))++end; \
            size_t left=at,right=mid,out=at; \
            while(left<mid && right<end){ \
                if(le(&input[left],&input[right]))output[out++]=input[left++]; \
                else output[out++]=input[right++]; \
            } \
            if(left<mid)memcpy(output+out,input+left,(mid-left)*sizeof *events); \
            else if(right<end)memcpy(output+out,input+right,(end-right)*sizeof *events); \
            ++merges;at=end; \
        } \
        type *previous=input;input=output;output=previous; \
    } \
}
#endif
