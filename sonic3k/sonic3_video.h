#pragma once
#include "game_video.h"
#include "trilogy_state.h"
void s3_video_state(TrStateIO *io);
extern const GameVideo sonic3_video;
int s3_video_hook(uint32_t pc);
void s3_video_command(int id, const char *json);
void s3_video_vblank(void);
unsigned s3_video_main_cpu_divisor(void);
