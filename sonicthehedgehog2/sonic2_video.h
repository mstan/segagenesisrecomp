#pragma once
unsigned s2_video_main_cpu_divisor(void);
void s2_video_vblank(void);
#include "game_video.h"
extern const GameVideo sonic2_video;
int s2_video_hook(uint32_t pc);
void s2_video_command(int id, const char *json);
