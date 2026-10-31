/* Generic frame audio timeline: pair FM latches, then stably merge natural
 * timestamp runs. Buffers are distinct and each holds at least count events.
 * Input order breaks ties. Retain push indices for the optional applied trace;
 * they do not participate in comparisons. Scratch is reusable for requeue. */
#ifndef GENESIS_AUDIO_EVENT_ORDER_H
#define GENESIS_AUDIO_EVENT_ORDER_H
#include "event_queue.h"
#include "timeline.h"
#include <string.h>
static int audio_event_before_equal(const AudioOrderedEvent *a,const AudioOrderedEvent *b)
{return a->e.cycle_stamp<=b->e.cycle_stamp;}
GENESIS_AUDIO_TIMELINE_DEFINE(audio_event_merge,AudioOrderedEvent,audio_event_before_equal)
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
    (void)audio_event_merge(events,scratch,count);
}
#endif
