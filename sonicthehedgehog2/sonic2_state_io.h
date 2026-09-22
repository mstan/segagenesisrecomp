#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Private-to-build POD stream. mode 0 writes/measures, 1 validates without
 * mutation, 2 applies an already validated payload. Never stream pointers. */
typedef struct S2StateIO { uint8_t *data; size_t size,pos; int mode,ok; } S2StateIO;
static void s2_state_bytes(S2StateIO *s,void *data,size_t size)
{
    if (s->data) {
        if (s->pos>s->size || size>s->size-s->pos) { s->ok=0; return; }
        if (!s->mode) memcpy(s->data+s->pos,data,size);
        if (s->mode==2) memcpy(data,s->data+s->pos,size);
    }
    s->pos+=size;
}
#define S2_STATE(s,v) s2_state_bytes(s,&(v),sizeof(v))
