#pragma once
/*
 *  gadgetbridge.h – Gadgetbridge BLE interface (BangleJS protocol)
 *
 *  Emulates a BangleJS-compatible device so Gadgetbridge on Android
 *  can pair and exchange data:  time sync, notifications, step count,
 *  battery level, music control.
 *
 *  ESP32-S3:  uses NimBLE with Nordic UART Service (NUS).
 *  Simulator: TCP server on localhost:9001, same JSON protocol.
 */

#ifndef GADGETBRIDGE_H
#define GADGETBRIDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Notification ────────────────────────────────────────────────── */
#define GB_NOTIF_TITLE_MAX  64
#define GB_NOTIF_BODY_MAX   128
#define GB_NOTIF_SRC_MAX    32

typedef struct {
    uint32_t id;
    char     title[GB_NOTIF_TITLE_MAX];
    char     body[GB_NOTIF_BODY_MAX];
    char     src[GB_NOTIF_SRC_MAX];    /* e.g. "WhatsApp", "Gmail" */
} gb_notification_t;

/* ── Music info ──────────────────────────────────────────────────── */
#define GB_MUSIC_MAX  64

typedef struct {
    char artist[GB_MUSIC_MAX];
    char track[GB_MUSIC_MAX];
    char album[GB_MUSIC_MAX];
    bool playing;
} gb_music_info_t;

/* ── Weather info ────────────────────────────────────────────────── */
#define GB_WEATHER_TXT_MAX  32
#define GB_WEATHER_LOC_MAX  48

typedef struct {
    int  temp;       /* temperature °C            */
    int  humidity;   /* 0–100 %                   */
    int  code;       /* OWM weather condition code */
    char txt[GB_WEATHER_TXT_MAX];   /* e.g. "Clear" */
    int  wind;       /* wind speed km/h            */
    int  wind_dir;   /* wind direction degrees     */
    char location[GB_WEATHER_LOC_MAX];
    int  temp_min;   /* daily low  °C  (0 if absent) */
    int  temp_max;   /* daily high °C  (0 if absent) */
    int  sunrise;    /* seconds since midnight (–1 if absent) */
    int  sunset;     /* seconds since midnight (–1 if absent) */
} gb_weather_info_t;

/* ── Navigation info ─────────────────────────────────────────────── */
#define GB_NAV_INSTR_MAX  128
#define GB_NAV_DIST_MAX    32
#define GB_NAV_ACTION_MAX  32
#define GB_NAV_ETA_MAX     32

typedef struct {
    char instr[GB_NAV_INSTR_MAX];    /* e.g. "Turn left onto Main St"  */
    char distance[GB_NAV_DIST_MAX];  /* e.g. "200 m"                   */
    char action[GB_NAV_ACTION_MAX];  /* e.g. "left","right","straight" */
    char eta[GB_NAV_ETA_MAX];        /* e.g. "12:34" or "5 min"        */
    bool active;                     /* true = navigating, false = stopped */
} gb_nav_info_t;

/* ── Callbacks (watch receives from phone) ───────────────────────── */
typedef void (*gb_on_time_set_cb)(int year, int month, int day,
                                   int hour, int min, int sec);
typedef void (*gb_on_notification_cb)(const gb_notification_t *notif);
typedef void (*gb_on_notif_dismiss_cb)(uint32_t id);
typedef void (*gb_on_music_cb)(const gb_music_info_t *music);
typedef void (*gb_on_call_cb)(const char *cmd, const char *name,
                               const char *number);
typedef void (*gb_on_find_cb)(bool start);  /* find-my-watch */
typedef void (*gb_on_weather_cb)(const gb_weather_info_t *weather);
typedef void (*gb_on_nav_cb)(const gb_nav_info_t *nav);

typedef struct {
    gb_on_time_set_cb       on_time_set;
    gb_on_notification_cb   on_notification;
    gb_on_notif_dismiss_cb  on_notif_dismiss;
    gb_on_music_cb          on_music;
    gb_on_call_cb           on_call;
    gb_on_find_cb           on_find;
    gb_on_weather_cb        on_weather;
    gb_on_nav_cb            on_nav;
} gb_callbacks_t;

/* ── Public API ──────────────────────────────────────────────────── */

/* Initialise BLE (or TCP for simulator) and start advertising.
   `device_name` appears in BLE scan, e.g. "Pocket Watch".
   `callbacks` struct with event handlers (NULL-safe).         */
void gb_init(const char *device_name, const gb_callbacks_t *callbacks);

/* Call from loop() – processes incoming data, sends keepalives. */
void gb_loop(void);

/* Stop BLE/TCP and free resources.  Safe to call even if not inited. */
void gb_deinit(void);

/* ── Send data to phone ──────────────────────────────────────────── */
void gb_send_battery(int percent, float voltage);
void gb_send_steps(uint32_t steps);
void gb_send_firmware_info(const char *fw_version, const char *hw_version);

/* Music control (watch → phone) */
void gb_send_music_cmd(const char *cmd);  /* "play","pause","next","prev","vol+" etc */

/* Is Gadgetbridge connected? */
bool gb_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* GADGETBRIDGE_H */
