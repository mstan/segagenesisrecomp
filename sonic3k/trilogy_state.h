#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Private, versioned quickstate payload. No host pointers are serialized.
 * Modes: 0 measure/write, 1 validate/skip, 2 restore. */
typedef struct TrStateIO { uint8_t *data; size_t size,pos; int mode,ok; } TrStateIO;
static void tr_state_bytes(TrStateIO *s,void *value,size_t n)
{
    if(!s->data){s->pos+=n;return;}
    if(s->pos>s->size||n>s->size-s->pos){s->ok=0;return;}
    if(s->mode==0)memcpy(s->data+s->pos,value,n);
    else if(s->mode==2)memcpy(value,s->data+s->pos,n);
    s->pos+=n;
}
#define TR_STATE(s,value) tr_state_bytes(s,&(value),sizeof(value))
