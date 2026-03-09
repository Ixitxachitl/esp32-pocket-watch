#pragma once
/*
 *  screen_manager.h – Custom horizontal swipe screen management
 *
 *  Uses manual touch tracking + lv_anim_t for smooth swiping between:
 *    Screen 0: Clock face
 *    Screen 1: Stopwatch
 *    Screen 2: Timer
 *    Screen 3: Alarm
 *
 *  Wraps around seamlessly (Alarm ↔ Clock).
 *  The swipe-down settings menu, notification popup, and sleep overlay
 *  are managed above the screens on the parent.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <lvgl.h>

/* Screen indices */
#define SCR_CLOCK     0
#define SCR_STOPWATCH 1
#define SCR_TIMER     2
#define SCR_ALARM     3
#define SCR_WEATHER   4
#define SCR_RADAR     5
#define SCR_COMPASS   6
#define SCR_MEDIA     7
#define SCR_LEVEL     8
#define SCR_SYSINFO   9
#define SCR_NAV      10
#define SCR_COUNT    11

/* Initialise the screen manager – creates tileview and all screens.
   Call once from setup() after lv_init() and display driver registration. */
void screen_manager_init(lv_obj_t *parent, int diameter);

/* Call every loop iteration – updates the active screen. */
void screen_manager_loop(void);

/* Forward current clock time to sub-screens (for alarm checking). */
void screen_manager_set_time(int hour, int min, int sec);

/* Navigate to a specific screen programmatically. */
void screen_manager_goto(int screen_idx);

/* Get the current visible screen index. */
int screen_manager_current(void);

/* Get the tile container for a given screen (for embedding widgets). */
lv_obj_t *screen_manager_get_tile(int screen_idx);

/* Small dot indicators showing which screen is active. */
void screen_manager_update_indicators(void);

/* Set battery level (0–100 %) across all screens' battery rings. */
void screen_manager_set_battery(int percent, bool charging);

/* ── Screen order / enable API ─────────────────────────────────── */

/* Set which screens are active and in what order.
   phys_ids is an array of physical screen indices (SCR_STOPWATCH etc.)
   Clock is always first and must NOT be included in the array.
   count = number of items in phys_ids. */
void screen_manager_set_screen_order(const int *phys_ids, int count);

/* Get the current active order (including clock at index 0).
   Returns the number written (≤ max). */
int  screen_manager_get_screen_order(int *out_phys_ids, int max);

/* Get the number of currently active (enabled) screens. */
int  screen_manager_get_active_count(void);

/* Get a human-readable name for a physical screen index. */
const char *screen_manager_get_screen_name(int physical_idx);

/* Returns true while the user is actively dragging or animating a swipe. */
bool screen_manager_is_swiping(void);

#ifdef __cplusplus
}
#endif
