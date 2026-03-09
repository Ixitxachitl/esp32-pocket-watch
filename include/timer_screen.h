#pragma once
/*
 *  timer_screen.h – Countdown timer screen for the tileview
 */

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create timer UI on the given parent (tile). */
void timer_screen_create(lv_obj_t *parent, int diameter);

/* Call every frame – updates countdown display. */
void timer_screen_loop(void);

/* Get/set state for persistence. */
int  timer_screen_get_set_hr(void);
int  timer_screen_get_set_min(void);
int  timer_screen_get_set_sec(void);
void timer_screen_set_preset(int hr, int min, int sec);

/* Get/set paused remaining time (ms) for persistence */
uint32_t timer_screen_get_remaining_ms(void);
void     timer_screen_set_remaining_ms(uint32_t ms);

/* Register callback fired when timer settings change */
void timer_screen_set_changed_cb(void (*cb)(void));

#ifdef __cplusplus
}
#endif
