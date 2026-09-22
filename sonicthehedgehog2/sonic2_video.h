#pragma once
#include "sonic2_state_io.h"
void s2_video_state(S2StateIO *io);
unsigned s2_video_main_cpu_divisor(void);
void s2_video_vblank(void);
#include "game_video.h"
extern const GameVideo sonic2_video;
int s2_video_hook(uint32_t pc);
void s2_video_command(int id, const char *json);
void s2_video_actor_origin(const struct GVDP *vdp, int line, int width, int *left, int *top);
int s2_video_actor_pixel_visible(const struct GVDP *vdp, int wx, int wy, int high, const unsigned char *world);
