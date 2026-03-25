#pragma once
#include <stdint.h>

/* Print label=value only when the value changes. Suppresses flood output. */
void log_on_change(const char *label, uint32_t value);
void log_reset_frame(void);
