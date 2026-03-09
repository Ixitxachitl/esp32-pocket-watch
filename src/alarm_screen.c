/*
 *  alarm_screen.c – Alarm screen (pure LVGL)
 *
 *  Features: Set hour/minute, enable/disable switch.
 *  When enabled and the current time matches, plays a repeated
 *  ding sound until the user dismisses it.
 */

#include "alarm_screen.h"
#include "seg7.h"
#include "clock_face.h"
#include "sound.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdbool.h>

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_BG      lv_color_make(10, 10, 15)
#define COL_TEXT    lv_color_white()
#define COL_DIM     lv_color_make(100, 100, 110)
#define COL_AMBER   lv_color_make(220, 170, 40)
#define COL_RED     lv_color_make(220, 50, 50)
#define COL_GREY    lv_color_make(55, 55, 70)
#define COL_BTN_DK  lv_color_make(40, 40, 55)

/* ── State ─────────────────────────────────────────────────────── */
static seg7_display_t *seg7_disp = NULL;
static lv_obj_t *ampm_label    = NULL;
static lv_obj_t *alarm_sw      = NULL;
static lv_obj_t *dismiss_btn   = NULL;
static lv_obj_t *status_label  = NULL;

static int  alarm_hour = 7;
static int  alarm_min  = 0;
static bool alarm_enabled  = false;
static bool alarm_ringing  = false;
static int  last_trig_min  = -1;   /* avoid re-triggering same minute */
static int  cur_h = 0, cur_m = 0;

/* Settings-changed callback (for persistence) */
typedef void (*alarm_changed_cb_t)(void);
static alarm_changed_cb_t alarm_changed_cb = NULL;

static lv_timer_t *ring_timer = NULL;  /* repeated ding while ringing */
static int ring_count = 0;
#define RING_REPEATS  30   /* max dings (30 seconds @ 1000ms interval) */

/* ── Helpers ───────────────────────────────────────────────────── */
static void update_display(void) {
    bool is24h = clock_face_get_24h();
    char buf[12];

    if (is24h) {
        snprintf(buf, sizeof(buf), "%02d:%02d", alarm_hour, alarm_min);
        if (ampm_label) lv_obj_add_flag(ampm_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* Convert 24-hour to 12-hour */
        int h12 = alarm_hour % 12;
        if (h12 == 0) h12 = 12;
        const char *ap = (alarm_hour < 12) ? "AM" : "PM";
        snprintf(buf, sizeof(buf), "%d:%02d", h12, alarm_min);
        if (ampm_label) {
            lv_label_set_text(ampm_label, ap);
            lv_obj_clear_flag(ampm_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* Set colour before text so segment lines pick it up immediately */
    if (alarm_ringing) {
        seg7_set_color(seg7_disp, COL_RED);
        if (ampm_label) lv_obj_set_style_text_color(ampm_label, COL_RED, 0);
        if (status_label) lv_label_set_text(status_label, "ALARM!");
        if (dismiss_btn)  lv_obj_clear_flag(dismiss_btn, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_color_t col = alarm_enabled ? COL_AMBER : COL_DIM;
        seg7_set_color(seg7_disp, col);
        if (ampm_label) lv_obj_set_style_text_color(ampm_label, col, 0);
        if (status_label) {
            lv_label_set_text(status_label,
                alarm_enabled ? "Alarm ON" : "Alarm OFF");
            lv_obj_set_style_text_color(status_label, col, 0);
        }
        if (dismiss_btn) lv_obj_add_flag(dismiss_btn, LV_OBJ_FLAG_HIDDEN);
    }

    seg7_set_text(seg7_disp, buf);
    /* Re-center after text change */
    if (is24h) {
        lv_obj_align(seg7_get_obj(seg7_disp), LV_ALIGN_CENTER, 0, -60);
    } else {
        lv_obj_align(seg7_get_obj(seg7_disp), LV_ALIGN_CENTER, -20, -60);
        if (ampm_label) {
            lv_obj_align_to(ampm_label, seg7_get_obj(seg7_disp),
                            LV_ALIGN_OUT_RIGHT_MID, 6, 0);
        }
    }
}

static void stop_ringing(void) {
    alarm_ringing = false;
    ring_count = 0;
    if (ring_timer) {
        lv_timer_del(ring_timer);
        ring_timer = NULL;
    }
    update_display();
}

static void _ring_cb(lv_timer_t *t) {
    (void)t;
    ring_count++;
    if (ring_count >= RING_REPEATS) {
        stop_ringing();
        return;
    }
    sound_play_ding();
}

static void start_ringing(void) {
    alarm_ringing = true;
    ring_count = 0;
    sound_play_ding();
    if (ring_timer) lv_timer_del(ring_timer);
    ring_timer = lv_timer_create(_ring_cb, 1000, NULL);
    update_display();
}

/* ── Callbacks ─────────────────────────────────────────────────── */
static void _hr_plus_cb(lv_event_t *e) {
    (void)e;
    alarm_hour = (alarm_hour + 1) % 24;
    last_trig_min = -1;
    update_display();
    if (alarm_changed_cb) alarm_changed_cb();
}
static void _hr_minus_cb(lv_event_t *e) {
    (void)e;
    alarm_hour = (alarm_hour + 23) % 24;
    last_trig_min = -1;
    update_display();
    if (alarm_changed_cb) alarm_changed_cb();
}
static void _min_plus_cb(lv_event_t *e) {
    (void)e;
    alarm_min = (alarm_min + 1) % 60;
    last_trig_min = -1;
    update_display();
    if (alarm_changed_cb) alarm_changed_cb();
}
static void _min_minus_cb(lv_event_t *e) {
    (void)e;
    alarm_min = (alarm_min + 59) % 60;
    last_trig_min = -1;
    update_display();
    if (alarm_changed_cb) alarm_changed_cb();
}

static void _sw_cb(lv_event_t *e) {
    (void)e;
    alarm_enabled = lv_obj_has_state(alarm_sw, LV_STATE_CHECKED);
    last_trig_min = -1;
    if (!alarm_enabled && alarm_ringing) stop_ringing();
    update_display();
    if (alarm_changed_cb) alarm_changed_cb();
}

static void _ampm_cb(lv_event_t *e) {
    (void)e;
    /* Toggle AM ↔ PM by shifting 12 hours */
    alarm_hour = (alarm_hour + 12) % 24;
    last_trig_min = -1;
    update_display();
    if (alarm_changed_cb) alarm_changed_cb();
}

static void _dismiss_cb(lv_event_t *e) {
    (void)e;
    stop_ringing();
}

/* ── Helper: create a small round button ───────────────────────── */
static lv_obj_t *make_adj_btn(lv_obj_t *parent, const char *sym,
                               lv_align_t align, int x_ofs, int y_ofs,
                               lv_event_cb_t cb) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 48, 44);
    lv_obj_align(btn, align, x_ofs, y_ofs);
    lv_obj_set_style_bg_color(btn, COL_BTN_DK, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 6, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, sym);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* ── Create ────────────────────────────────────────────────────── */
void alarm_screen_create(lv_obj_t *parent, int diameter) {
    (void)diameter;
    lv_obj_set_style_bg_color(parent, COL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "ALARM");
    lv_obj_set_style_text_color(title, COL_DIM, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* Time display — seg7 widget */
    seg7_disp = seg7_create(parent, 5, 36, COL_AMBER, 4);
    lv_obj_align(seg7_get_obj(seg7_disp), LV_ALIGN_CENTER, -20, -60);

    /* AM / PM label (tap to toggle, hidden in 24h mode) */
    ampm_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ampm_label, &lv_font_montserrat_20, 0);
    lv_obj_align_to(ampm_label, seg7_get_obj(seg7_disp),
                    LV_ALIGN_OUT_RIGHT_MID, 6, 0);
    lv_obj_add_flag(ampm_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ampm_label, _ampm_cb, LV_EVENT_CLICKED, NULL);

    /* HOUR / MIN labels */
    lv_obj_t *hr_lbl = lv_label_create(parent);
    lv_label_set_text(hr_lbl, "HOUR");
    lv_obj_set_style_text_color(hr_lbl, COL_DIM, 0);
    lv_obj_set_style_text_font(hr_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(hr_lbl, LV_ALIGN_CENTER, -70, -18);

    lv_obj_t *mn_lbl = lv_label_create(parent);
    lv_label_set_text(mn_lbl, "MIN");
    lv_obj_set_style_text_color(mn_lbl, COL_DIM, 0);
    lv_obj_set_style_text_font(mn_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(mn_lbl, LV_ALIGN_CENTER, 70, -18);

    /* Adjustment buttons */
    make_adj_btn(parent, LV_SYMBOL_MINUS, LV_ALIGN_CENTER, -95, 18, _hr_minus_cb);
    make_adj_btn(parent, LV_SYMBOL_PLUS,  LV_ALIGN_CENTER, -45, 18, _hr_plus_cb);
    make_adj_btn(parent, LV_SYMBOL_MINUS, LV_ALIGN_CENTER,  45, 18, _min_minus_cb);
    make_adj_btn(parent, LV_SYMBOL_PLUS,  LV_ALIGN_CENTER,  95, 18, _min_plus_cb);

    /* ON / OFF switch */
    alarm_sw = lv_switch_create(parent);
    lv_obj_set_size(alarm_sw, 60, 30);
    lv_obj_align(alarm_sw, LV_ALIGN_CENTER, 0, 85);
    lv_obj_set_style_bg_color(alarm_sw, lv_color_make(60, 60, 70),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(alarm_sw, COL_AMBER,
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(alarm_sw, _sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Status text */
    status_label = lv_label_create(parent);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_20, 0);
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 125);

    /* Dismiss button on lv_layer_top() — overlays any screen */
    dismiss_btn = lv_btn_create(lv_layer_top());
    lv_obj_set_size(dismiss_btn, 300, 300);
    lv_obj_align(dismiss_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dismiss_btn, COL_RED, 0);
    lv_obj_set_style_bg_opa(dismiss_btn, LV_OPA_90, 0);
    lv_obj_set_style_radius(dismiss_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(dismiss_btn, 0, 0);
    lv_obj_set_style_border_width(dismiss_btn, 3, 0);
    lv_obj_set_style_border_color(dismiss_btn, lv_color_white(), 0);
    lv_obj_set_style_border_opa(dismiss_btn, LV_OPA_40, 0);
    lv_obj_t *dis_lbl = lv_label_create(dismiss_btn);
    lv_label_set_text(dis_lbl, "DISMISS");
    lv_obj_set_style_text_font(dis_lbl, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(dis_lbl, lv_color_white(), 0);
    lv_obj_align(dis_lbl, LV_ALIGN_CENTER, 0, -14);
    lv_obj_t *src_lbl = lv_label_create(dismiss_btn);
    lv_label_set_text(src_lbl, "ALARM");
    lv_obj_set_style_text_font(src_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(src_lbl, lv_color_make(255, 200, 200), 0);
    lv_obj_align(src_lbl, LV_ALIGN_CENTER, 0, 22);
    lv_obj_add_event_cb(dismiss_btn, _dismiss_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(dismiss_btn, LV_OBJ_FLAG_HIDDEN);

    update_display();
}

/* ── Public API ────────────────────────────────────────────────── */
void alarm_screen_set_current_time(int hour, int min, int sec) {
    (void)sec;
    cur_h = hour;
    cur_m = min;
}

void alarm_screen_loop(void) {
    /* Refresh display if 12/24h format changed */
    static bool last_24h = true;
    bool cur_24h = clock_face_get_24h();
    if (cur_24h != last_24h) {
        last_24h = cur_24h;
        update_display();
    }

    /* Check alarm trigger */
    if (alarm_enabled && !alarm_ringing
        && cur_h == alarm_hour && cur_m == alarm_min
        && cur_m != last_trig_min) {
        last_trig_min = cur_m;
        start_ringing();
    }
    /* Reset trigger guard when minute changes */
    if (cur_m != alarm_min) {
        /* Allow re-triggering next time the minute matches */
    }
}

int  alarm_screen_get_hour(void)    { return alarm_hour; }
int  alarm_screen_get_min(void)     { return alarm_min; }
bool alarm_screen_get_enabled(void) { return alarm_enabled; }

void alarm_screen_set_alarm(int hour, int min, bool enabled) {
    alarm_hour = hour % 24;
    alarm_min  = min % 60;
    alarm_enabled = enabled;
    if (alarm_sw) {
        if (enabled) lv_obj_add_state(alarm_sw, LV_STATE_CHECKED);
        else         lv_obj_clear_state(alarm_sw, LV_STATE_CHECKED);
    }
    last_trig_min = -1;
    update_display();
}

void alarm_screen_set_changed_cb(void (*cb)(void)) {
    alarm_changed_cb = cb;
}
