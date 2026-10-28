/* Generic frame audio timeline: pair FM latches, then stably merge natural
 * timestamp runs. Buffers are distinct and each holds at least count events.
 * Input order breaks ties. Retain push indices for the optional applied trace;
 * they do not participate in comparisons. Scratch is reusable for requeue. */
#ifndef GENESIS_AUDIO_EVENT_ORDER_H
#define GENESIS_AUDIO_EVENT_ORDER_H
#include "event_queue.h"
#include <string.h>
typedef struct {AudioEvent e;uint32_t idx;} AudioOrderedEvent;
static void audio_event_order(AudioOrderedEvent *events,AudioOrderedEvent *scratch,size_t count,int pairguard)
{
    if(pairguard) {
        uint32_t next_data[2]={0,0};unsigned have_data[2]={0,0};
        for(size_t i=count;i-- >0;) {
            unsigned port=events[i].e.port;
            if(port<4u) {
                unsigned part=port>>1;
                if(port&1u){next_data[part]=events[i].e.cycle_stamp;have_data[part]=1;}
                else {if(have_data[part])events[i].e.cycle_stamp=next_data[part];have_data[part]=0;}
            }
        }
    }
    if(count<2)return;
    AudioOrderedEvent *input=events,*output=scratch;
    for(;;) {
        size_t at=0;unsigned merges=0;
        while(at<count) {
            size_t mid=at+1;
            while(mid<count&&input[mid-1].e.cycle_stamp<=input[mid].e.cycle_stamp)++mid;
            if(mid==count) {
                if(!merges) {
                    if(input!=events)memcpy(events,input,count*sizeof *events);
                    return;
                }
                memcpy(output+at,input+at,(count-at)*sizeof *events);break;
            }
            size_t end=mid+1;
            while(end<count&&input[end-1].e.cycle_stamp<=input[end].e.cycle_stamp)++end;
            size_t left=at,right=mid,out=at;
            while(left<mid&&right<end) {
                if(input[left].e.cycle_stamp<=input[right].e.cycle_stamp)output[out++]=input[left++];
                else output[out++]=input[right++];
            }
            if(left<mid)memcpy(output+out,input+left,(mid-left)*sizeof *events);
            else if(right<end)memcpy(output+out,input+right,(end-right)*sizeof *events);
            ++merges;at=end;
        }
        AudioOrderedEvent *previous=input;input=output;output=previous;
    }
}
#endif
