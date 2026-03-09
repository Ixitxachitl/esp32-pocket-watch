#pragma once
/*
 *  sysinfo_screen.h – System information display
 *
 *  Shows battery level, WiFi signal strength, Bluetooth state,
 *  uptime, free memory, and chip info.  Data is pushed via
 *  sysinfo_screen_update() from the main loop.
 */

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Data structure pushed from main loop. */
typedef struct {
    int   battery_pct;      /* 0-100, or -1 if unknown           */
    float battery_volt;     /* volts, or 0 if unknown             */
    int   wifi_rssi;        /* dBm, or 0 if not connected         */
    int   wifi_connected;   /* 1 = connected                      */
    int   wifi_enabled;     /* 1 = WiFi is on                     */
    int   bt_connected;     /* 1 = connected                      */
    unsigned long uptime_s; /* seconds since boot                 */
    unsigned long free_heap;/* bytes of free heap                 */
    const char *wifi_ssid;  /* SSID string (NULL if disconnected) */
    const char *wifi_ip;    /* IP address string (AP or STA)      */
} sysinfo_data_t;

/* Create system info UI on the given tile parent. */
void sysinfo_screen_create(lv_obj_t *parent, int diameter);

/* Push updated data (call every ~1 second from main loop). */
void sysinfo_screen_update(const sysinfo_data_t *data);

#ifdef __cplusplus
}
#endif
