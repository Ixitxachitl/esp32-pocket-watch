#pragma once
/*
 *  alarm_screen.h – Alarm screen for the tileview
 */

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create alarm UI on the given parent (tile). */
void alarm_screen_create(lv_obj_t *parent, int diameter);

/* Call every frame – checks alarm trigger. */
void alarm_screen_loop(void);

/* Feed current time so alarm can detect match. */
void alarm_screen_set_current_time(int hour, int min, int sec);

/* Get/set alarm state for persistence. */
int  alarm_screen_get_hour(void);
int  alarm_screen_get_min(void);
bool alarm_screen_get_enabled(void);
void alarm_screen_set_alarm(int hour, int min, bool enabled);

/* Register callback fired when alarm settings change */
void alarm_screen_set_changed_cb(void (*cb)(void));

#ifdef __cplusplus
}
#endif
