#ifndef GENESIS_ANDROID_HOST_H
#define GENESIS_ANDROID_HOST_H
/* Android's touch pad belongs to gameplay; native launcher/menu touch stays free. */
void genesis_android_set_gameplay(int available);
/* Map only SDL's requested fullscreen mode. Android uses the Activity surface;
 * desktop callers retain the exact requested window/borderless/exclusive mode. */
unsigned genesis_android_fullscreen_flags(unsigned requested);
#endif
