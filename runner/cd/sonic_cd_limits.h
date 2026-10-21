#ifndef SEGACD_SONIC_CD_LIMITS_H
#define SEGACD_SONIC_CD_LIMITS_H

/* Native signed-word spawn cursors require 0x300 + 2*C <= 0x7fff,
 * where C rounds each side's margin up to 128 pixels. This is the largest
 * even canvas that satisfies the original instruction operands. */
enum { SEGACD_SONIC_CD_MAX_VIEW_WIDTH = 32064 };

#endif
