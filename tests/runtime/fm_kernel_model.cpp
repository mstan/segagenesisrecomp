/* Reuse the complete public wrapper; the reference compiles its unchanged
 * frozen body AND vendor in a distinct namespace. This adapter is test-only. */
#if GENESIS_FM_KERNEL_REFERENCE
#include "fm_reference/ym2612_ymfm.cpp"
#else
#include "../../runner/audio/ym2612_ymfm.cpp"
#endif
extern "C" uint8_t ym2612_kernel_test_status(void)
{
    if(!s_inited)ym2612_init();
    return s_chip->read_status();
}
