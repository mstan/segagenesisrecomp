#pragma once
#include "game_video.h"
extern const GameVideo sonic1_video;
void s1_video_command(int id, const char *json);
int s1_video_hook(uint32_t pc);
