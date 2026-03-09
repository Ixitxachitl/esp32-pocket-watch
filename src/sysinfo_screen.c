/*
 *  sysinfo_screen.c – System information display
 *
 *  Displays battery level/voltage, WiFi signal strength,
 *  Bluetooth state, uptime, and free heap memory in a
 *  clean instrument-panel style using native LVGL widgets.
 */

#include "sysinfo_screen.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_TITLE    lv_color_make(100, 100, 110)
#define COL_LABEL    lv_color_make(130, 130, 140)
#define COL_VALUE    lv_color_make(220, 220, 210)
#define COL_GOOD     lv_color_make(80,  200, 100)
#define COL_WARN     lv_color_make(255, 180,  50)
#define COL_BAD      lv_color_make(255,  80,  60)
#define COL_OFF      lv_color_make(100, 100, 100)
#define COL_ICON     lv_color_make(80,  160, 255)
#define COL_DIVIDER  lv_color_make(40,   40,  50)

/* ── Layout ────────────────────────────────────────────────────── */
#define ROW_X_ICON   60
#define ROW_X_LABEL  90
#define ROW_X_VALUE  380    /* right-aligned */
#define ROW_START_Y  130
#define ROW_HEIGHT   42
#define NUM_ROWS      5

/* ── LVGL objects ──────────────────────────────────────────────── */
static lv_obj_t *icon_lbls[NUM_ROWS]    = {NULL};
static lv_obj_t *label_lbls[NUM_ROWS]   = {NULL};
static lv_obj_t *value_lbls[NUM_ROWS]   = {NULL};
static lv_obj_t *dividers[NUM_ROWS - 1] = {NULL};

/* Battery bar */
static lv_obj_t *batt_bar_bg  = NULL;
static lv_obj_t *batt_bar_fg  = NULL;

/* WiFi signal bars */
static lv_obj_t *wifi_bars[4] = {NULL};

/* ── Row definitions ───────────────────────────────────────────── */
static const char *row_icons[NUM_ROWS] = {
    "\xF0\x9F\x94\x8B",   /* 🔋 Battery     */
    "\xF0\x9F\x93\xB6",   /* 📶 WiFi        */
    "\xF0\x9F\x94\xB5",   /* 🔵 Bluetooth   */
    "\xE2\x8F\xB1",       /* ⏱  Uptime      */
    "\xF0\x9F\x92\xBE"    /* 💾 Memory      */
};

static const char *row_labels[NUM_ROWS] = {
    "Battery",
    "WiFi",
    "Bluetooth",
    "Uptime",
    "Free Memory"
};

/* ── Create ────────────────────────────────────────────────────── */
void sysinfo_screen_create(lv_obj_t *parent, int diameter)
{
    (void)diameter;

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "SYSTEM");
    lv_obj_set_style_text_color(title, COL_TITLE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* ── Rows ── */
    for (int i = 0; i < NUM_ROWS; i++) {
        int y = ROW_START_Y + i * ROW_HEIGHT;

        /* Icon – but since LVGL built-in fonts don't have emoji,
           use simple ASCII symbols instead */
        icon_lbls[i] = lv_label_create(parent);
        lv_obj_set_style_text_font(icon_lbls[i],
                                   &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(icon_lbls[i], COL_ICON, 0);
        lv_obj_set_pos(icon_lbls[i], ROW_X_ICON, y + 4);

        /* Label */
        label_lbls[i] = lv_label_create(parent);
        lv_label_set_text(label_lbls[i], row_labels[i]);
        lv_obj_set_style_text_font(label_lbls[i],
                                   &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label_lbls[i], COL_LABEL, 0);
        lv_obj_set_pos(label_lbls[i], ROW_X_LABEL, y + 4);

        /* Value */
        value_lbls[i] = lv_label_create(parent);
        lv_label_set_text(value_lbls[i], "--");
        lv_obj_set_style_text_font(value_lbls[i],
                                   &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(value_lbls[i], COL_VALUE, 0);
        lv_obj_set_style_text_align(value_lbls[i],
                                    LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(value_lbls[i], 160);
        lv_obj_set_pos(value_lbls[i], ROW_X_VALUE - 160, y + 4);

        /* Divider line between rows */
        if (i < NUM_ROWS - 1) {
            dividers[i] = lv_obj_create(parent);
            lv_obj_set_size(dividers[i], 310, 1);
            lv_obj_set_style_bg_color(dividers[i], COL_DIVIDER, 0);
            lv_obj_set_style_bg_opa(dividers[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(dividers[i], 0, 0);
            lv_obj_set_style_pad_all(dividers[i], 0, 0);
            lv_obj_set_style_radius(dividers[i], 0, 0);
            lv_obj_clear_flag(dividers[i],
                LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_pos(dividers[i], 78, y + ROW_HEIGHT - 4);
        }
    }

    /* Set ASCII icons (since LVGL montserrat doesn't have emoji) */
    lv_label_set_text(icon_lbls[0], LV_SYMBOL_BATTERY_FULL);
    lv_label_set_text(icon_lbls[1], LV_SYMBOL_WIFI);
    lv_label_set_text(icon_lbls[2], LV_SYMBOL_BLUETOOTH);
    lv_label_set_text(icon_lbls[3], LV_SYMBOL_REFRESH);
    lv_label_set_text(icon_lbls[4], LV_SYMBOL_DRIVE);

    /* Set initial values */
    lv_label_set_text(value_lbls[0], "-- %");
    lv_label_set_text(value_lbls[1], "Not connected");
    lv_label_set_text(value_lbls[2], "Disconnected");
    lv_label_set_text(value_lbls[3], "0:00:00");
    lv_label_set_text(value_lbls[4], "-- KB");
}

/* ── Helper: RSSI → quality colour ─────────────────────────────── */
static lv_color_t _rssi_color(int rssi)
{
    if (rssi > -50) return COL_GOOD;
    if (rssi > -70) return COL_WARN;
    return COL_BAD;
}

/* ── Helper: battery % → colour ────────────────────────────────── */
static lv_color_t _batt_color(int pct)
{
    if (pct > 50) return COL_GOOD;
    if (pct > 20) return COL_WARN;
    return COL_BAD;
}

/* ── Update ────────────────────────────────────────────────────── */
void sysinfo_screen_update(const sysinfo_data_t *d)
{
    if (!d) return;
    char buf[48];

    /* Battery */
    if (d->battery_pct >= 0) {
        if (d->battery_volt > 0.1f)
            snprintf(buf, sizeof(buf), "%d%%  %.2fV", d->battery_pct,
                     (double)d->battery_volt);
        else
            snprintf(buf, sizeof(buf), "%d%%", d->battery_pct);
        lv_label_set_text(value_lbls[0], buf);
        lv_obj_set_style_text_color(value_lbls[0],
                                    _batt_color(d->battery_pct), 0);
    } else {
        lv_label_set_text(value_lbls[0], "Unknown");
        lv_obj_set_style_text_color(value_lbls[0], COL_OFF, 0);
    }

    /* WiFi */
    if (d->wifi_connected) {
        if (d->wifi_ip)
            snprintf(buf, sizeof(buf), "%s  %d dBm\n%s",
                     d->wifi_ssid ? d->wifi_ssid : "?",
                     d->wifi_rssi, d->wifi_ip);
        else
            snprintf(buf, sizeof(buf), "%s  %d dBm",
                     d->wifi_ssid ? d->wifi_ssid : "?",
                     d->wifi_rssi);
        lv_label_set_text(value_lbls[1], buf);
        lv_obj_set_style_text_color(value_lbls[1],
                                    _rssi_color(d->wifi_rssi), 0);
    } else if (d->wifi_ip) {
        snprintf(buf, sizeof(buf), "AP  %s", d->wifi_ip);
        lv_label_set_text(value_lbls[1], buf);
        lv_obj_set_style_text_color(value_lbls[1], COL_WARN, 0);
    } else if (d->wifi_enabled) {
        lv_label_set_text(value_lbls[1], "AP  starting...");
        lv_obj_set_style_text_color(value_lbls[1], COL_WARN, 0);
    } else {
        lv_label_set_text(value_lbls[1], "Off");
        lv_obj_set_style_text_color(value_lbls[1], COL_OFF, 0);
    }

    /* Bluetooth */
    if (d->bt_connected) {
        lv_label_set_text(value_lbls[2], "Connected");
        lv_obj_set_style_text_color(value_lbls[2], COL_GOOD, 0);
    } else {
        lv_label_set_text(value_lbls[2], "Disconnected");
        lv_obj_set_style_text_color(value_lbls[2], COL_OFF, 0);
    }

    /* Uptime */
    {
        unsigned long s = d->uptime_s;
        unsigned long days = s / 86400;
        unsigned long hrs  = (s % 86400) / 3600;
        unsigned long mins = (s % 3600) / 60;
        unsigned long secs = s % 60;
        if (days > 0)
            snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu",
                     days, hrs, mins, secs);
        else
            snprintf(buf, sizeof(buf), "%lu:%02lu:%02lu",
                     hrs, mins, secs);
        lv_label_set_text(value_lbls[3], buf);
        lv_obj_set_style_text_color(value_lbls[3], COL_VALUE, 0);
    }

    /* Free memory */
    {
        if (d->free_heap > 1024 * 1024)
            snprintf(buf, sizeof(buf), "%.1f MB",
                     (double)d->free_heap / (1024.0 * 1024.0));
        else
            snprintf(buf, sizeof(buf), "%.1f KB",
                     (double)d->free_heap / 1024.0);
        lv_label_set_text(value_lbls[4], buf);
        lv_obj_set_style_text_color(value_lbls[4], COL_VALUE, 0);
    }
}
