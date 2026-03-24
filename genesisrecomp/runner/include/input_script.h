#pragma once
#include <stdbool.h>

bool input_script_load(const char *path);
void input_script_tick(void);    /* Call once per frame from the main loop */
bool input_script_done(void);
