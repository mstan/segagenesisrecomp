#ifndef GENESIS_NATIVE_CALL_FRAME_H
#define GENESIS_NATIVE_CALL_FRAME_H
#include <stdint.h>
extern uint64_t g_native_direct_calls,g_native_direct_tails;
#ifndef GENESIS_DISABLE_NATIVE_CALL_FRAME

/* Generated dispatchers opt into this shared representation. Older generated
 * dispatchers keep their private frames and do not reference these symbols.
 * All fields live on the game fiber stack; snapshots retain the actual frame,
 * while the address-free walk continues to expose only pending/guest address.
 * function is a derived native continuation valid for this loaded build. */
#define GENESIS_HAS_NATIVE_CALL_FRAME 1
typedef struct GenesisNativeCallFrame {
    int pending;
    uint32_t addr;
    struct GenesisNativeCallFrame *prev;
    void (*function)(void);
} GenesisNativeCallFrame;
extern GenesisNativeCallFrame *g_recomp_native_frame;
void recomp_native_drain(GenesisNativeCallFrame *);
void recomp_tail_call(uint32_t);

/* Only generated, proven function symbols use this entry. Keeping the frame
 * in the caller exposes the direct C edge to the host compiler without hiding
 * pending tails, nested IRQs, stack skips or a fiber suspension. */
#define RECOMP_CALL_NATIVE(fn) do { \
    GenesisNativeCallFrame _native_frame={0,0,g_recomp_native_frame,0}; \
    g_recomp_native_frame=&_native_frame; \
    ++g_native_direct_calls; \
    (fn)(); \
    if(_native_frame.pending)recomp_native_drain(&_native_frame); \
    g_recomp_native_frame=_native_frame.prev; \
} while(0)

static inline void recomp_tail_native(uint32_t addr,void (*fn)(void))
{
    if(!g_recomp_native_frame){recomp_tail_call(addr);return;}
    g_recomp_native_frame->addr=addr&0xffffffu;
    g_recomp_native_frame->function=fn;
    g_recomp_native_frame->pending=1;
    ++g_native_direct_tails;
}
#define RECOMP_TAIL_NATIVE(addr,fn) recomp_tail_native((addr),(fn))
#endif
#endif
