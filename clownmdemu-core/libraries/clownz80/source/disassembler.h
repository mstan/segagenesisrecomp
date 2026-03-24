#ifndef CLOWNZ80_DISASSEMBLER_H
#define CLOWNZ80_DISASSEMBLER_H

#include <stddef.h>

#include "../libraries/clowncommon/clowncommon.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char (*ClownZ80_ReadCallback)(void *user_data);
typedef void (*ClownZ80_PrintCallback)(void *user_data, const char *format, ...);

void ClownZ80_Disassemble(unsigned long address, unsigned int maximum_instructions, ClownZ80_ReadCallback read_callback, ClownZ80_PrintCallback print_callback, const void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* CLOWNZ80_DISASSEMBLER_H */
