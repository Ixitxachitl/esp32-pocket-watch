#pragma once
/*
 *  nav_screen.h – Turn-by-turn navigation display
 *
 *  Shows navigation instructions received from Gadgetbridge
 *  (phone maps app).  Displays: direction arrow, instruction
 *  text, distance to next turn, and ETA.
 */

#include <lvgl.h>
#include "gadgetbridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create navigation UI on the given tile parent. */
void nav_screen_create(lv_obj_t *parent, int diameter);

/* Update with navigation data from Gadgetbridge. */
void nav_screen_update(const gb_nav_info_t *nav);

/* Re-render with cached data (call after units change). */
void nav_screen_refresh(void);

#ifdef __cplusplus
}
#endif
