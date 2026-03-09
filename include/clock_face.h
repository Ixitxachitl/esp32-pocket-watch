#pragma once
/*
 *  clock_face.h – LVGL analogue clock widget
 *
 *  Creates an lv_meter-based analogue clock on the given parent.
 *  Call clock_face_update() every second with h/m/s.
 *  Pure LVGL – no hardware dependencies, works on SDL too.
 */

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Callbacks fired from the swipe-down settings menu */
typedef void (*cf_wifi_toggle_cb_t)(bool enable);
typedef void (*cf_bt_toggle_cb_t)(bool enable);
typedef void (*cf_reset_steps_cb_t)(void);
typedef void (*cf_screen_power_cb_t)(bool on);  /* true = display on */
typedef void (*cf_settings_changed_cb_t)(void);  /* called when any setting changes */

/* Create the clock face on `parent` (or lv_scr_act() if NULL).
   `diameter` is the widget size in pixels.
   Only creates the clock dial / hands / orrery – no overlays.  */
void clock_face_create(lv_obj_t *parent, int diameter);

/* Create the overlay elements (settings menu, notification popup,
   sleep overlay, gesture handler) on `overlay_parent`.
   Must be called after clock_face_create().
   Usually overlay_parent is lv_scr_act() so overlays float above
   all tileview tiles.                                           */
void clock_face_create_overlays(lv_obj_t *overlay_parent);

/* Update the clock hands.  h = 0–23, m = 0–59, s = 0–59 */
void clock_face_update(int h, int m, int s);

/* Set battery level (0–100 %).  Shown as a blue arc on the outer ring.
   If charging is true, arc and icon turn green with a lightning bolt. */
void clock_face_set_battery(int percent, bool charging);

/* Set the date shown in the mechanical date window.  day = 1–31. */
void clock_face_set_date(int day, int month);

/* Set full date/time – updates orrery planet positions + date window.
   year = e.g. 2026, month = 1–12, day = 1–31, hour = 0–23, min = 0–59. */
void clock_face_set_datetime(int year, int month, int day, int hour, int min);

/* Set step counter value displayed on the clock face */
void clock_face_set_steps(uint32_t steps);

/* Register callbacks for swipe-down menu actions */
void clock_face_set_menu_callbacks(cf_wifi_toggle_cb_t wifi_cb,
                                   cf_bt_toggle_cb_t bt_cb,
                                   cf_reset_steps_cb_t reset_cb);

/* Set the Bluetooth switch state shown in the menu */
void clock_face_set_bt_switch_state(bool on);

/* Register callback for screen on/off control */
void clock_face_set_screen_power_cb(cf_screen_power_cb_t cb);

/* Set the WiFi switch state shown in the menu (call after WiFi changes) */
void clock_face_set_wifi_state(bool connected);

/* Call every loop iteration – manages screen timeout & wake-on-long-press.
   Returns true if screen is on (caller should skip LVGL updates if off). */
bool clock_face_process_activity(void);

/* Get/set the screen timeout index (0=5s, 1=10s, 2=15s, 3=always on) */
int  clock_face_get_timeout_idx(void);
void clock_face_set_timeout_idx(int idx);

/* Register callback fired when a persistent setting changes */
void clock_face_set_settings_changed_cb(cf_settings_changed_cb_t cb);

/* Show a notification popup (auto-dismisses after ~5 seconds) */
void clock_face_show_notification(const char *title, const char *body,
                                  const char *source);

/* Dismiss the current notification popup (if any) */
void clock_face_dismiss_notification(void);

/* Wake the screen immediately (e.g. from a hardware button press) */
void clock_face_wake(void);

/* Set the Bluetooth connection indicator */
void clock_face_set_bt_state(bool connected);

/* Returns true if the swipe-down settings menu is currently open */
bool clock_face_is_menu_open(void);

/* Get/set 24-hour time format (true = 24h, false = 12h) */
bool clock_face_get_24h(void);
void clock_face_set_24h(bool is24h);

/* Get/set orrery (solar system) display on/off */
bool clock_face_get_orrery(void);
void clock_face_set_orrery(bool on);

/* Get/set metric units (true = °C/km·h, false = °F/mph) */
bool clock_face_get_metric(void);
void clock_face_set_metric(bool metric);

#ifdef __cplusplus
}
#endif
