/*
 *  clock_face.c – LVGL analogue clock widget (lv_meter-based)
 *
 *  Fancier styling: decorative rings, luminous-style markers,
 *  date window with mechanical-scroll appearance.
 *  Entirely LVGL — no hardware or Arduino dependency.
 */

#include "clock_face.h"
#include "seg7.h"
#include "weather_screen.h"
#include "nav_screen.h"
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_BG          lv_color_make(10, 10, 15)
#define COL_RING_OUTER  lv_color_make(60, 60, 65)
#define COL_RING_INNER  lv_color_make(35, 35, 40)
#define COL_TICK_MINOR  lv_color_make(140, 140, 140)
#define COL_TICK_MAJOR  lv_color_white()
#define COL_HOUR_NUM    lv_color_make(220, 220, 210)
#define COL_HAND_HR     lv_color_make(230, 230, 220)
#define COL_HAND_MIN    lv_color_make(230, 230, 220)
#define COL_HAND_SEC    lv_color_make(220, 40, 40)
#define COL_BATT        lv_color_make(0, 140, 255)
#define COL_DATE_BG     lv_color_make(25, 25, 30)
#define COL_DATE_FG     lv_color_white()
#define COL_DATE_BORDER lv_color_make(80, 80, 85)
#define COL_DATE_SHADOW lv_color_make(45, 45, 50)

/* ── Internal state ────────────────────────────────────────────── */
static lv_obj_t              *meter     = NULL;
static lv_meter_scale_t      *main_scale = NULL;
/* Battery arc (lightweight lv_arc instead of meter indicator) */
static lv_obj_t      *batt_arc    = NULL;
/* Snapshot of the meter (static background image in PSRAM) */
static lv_obj_t      *meter_img   = NULL;
static lv_img_dsc_t   meter_snap_dsc;
static void          *meter_snap_buf = NULL;

static lv_obj_t *hour_labels[12];

/* Clock hands as lv_line children (for proper Z-order over date window) */
static lv_obj_t  *line_hour = NULL;
static lv_obj_t  *line_min  = NULL;
static lv_obj_t  *line_sec  = NULL;
static lv_point_t pts_hour[2];
static lv_point_t pts_min[2];
static lv_point_t pts_sec[2];

/* Date window widgets */
static lv_obj_t *date_window  = NULL;
static lv_obj_t *date_label   = NULL;
static lv_obj_t *date_stripe_top = NULL;
static lv_obj_t *date_stripe_bot = NULL;

/* Battery text */
static lv_obj_t *batt_icon_label = NULL;
static lv_obj_t *batt_pct_label  = NULL;

/* Step counter */
static lv_obj_t *step_icon  = NULL;   /* canvas with shoe icon */
static lv_obj_t *step_label = NULL;
#define SHOE_W 16
#define SHOE_H 16
static uint8_t shoe_cbuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(SHOE_W, SHOE_H)];

/* Steps bitmap (1 = filled, 0 = transparent) — two footprints */
static const uint8_t shoe_px[SHOE_H][SHOE_W] = {
    {0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0, 0,0,0,1,1,1,0,0},
    {0,0,0,0,0,0,0,0, 0,0,1,1,1,1,1,0},
    {0,0,1,1,1,0,0,0, 0,0,1,1,1,1,1,0},
    {0,1,1,1,1,1,0,0, 0,0,1,1,1,1,1,0},
    {0,1,1,1,1,1,0,0, 0,0,1,1,1,1,1,0},
    {0,1,1,1,1,1,0,0, 0,0,1,1,1,1,1,0},
    {0,1,1,1,1,1,0,0, 0,0,1,1,1,1,1,0},
    {0,1,1,1,1,1,0,0, 0,0,0,0,0,0,0,0},
    {0,1,1,1,1,1,0,0, 0,0,1,1,1,1,1,0},
    {0,0,0,0,0,0,0,0, 0,0,1,1,1,1,1,0},
    {0,1,1,1,1,1,0,0, 0,0,1,1,1,1,1,0},
    {0,1,1,1,1,1,0,0, 0,0,0,1,1,1,0,0},
    {0,1,1,1,1,1,0,0, 0,0,0,0,0,0,0,0},
    {0,0,1,1,1,0,0,0, 0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0},
};

/* ── Swipe-down settings menu ──────────────────────────────────── */
static lv_obj_t *menu_panel    = NULL;
static lv_obj_t *menu_scrim    = NULL;   /* dim overlay behind panel */
static lv_obj_t *wifi_sw       = NULL;
static lv_obj_t *bt_sw         = NULL;
static bool menu_open = false;
static cf_wifi_toggle_cb_t  cb_wifi_toggle  = NULL;
static cf_bt_toggle_cb_t    cb_bt_toggle    = NULL;
static cf_reset_steps_cb_t  cb_reset_steps  = NULL;
static lv_obj_t *parent_scr    = NULL;   /* remember parent for menu */

/* Screen timeout */
static cf_screen_power_cb_t cb_screen_power = NULL;
static cf_settings_changed_cb_t cb_settings_changed = NULL;
static lv_obj_t *timeout_val_lbl = NULL;  /* shows "5s / 10s / 15s / ON" in menu */
static const int timeout_options[] = { 5, 10, 15, 0 };  /* 0 = always on */
static int timeout_idx = 3;        /* default: always on */

/* 12/24-hour format */
static bool time_24h = true;  /* default: 24-hour */
static lv_obj_t *fmt_val_lbl = NULL;  /* shows "24h" / "12h" in menu */

/* Orrery toggle */
static bool orrery_on = true;  /* default: on */
static lv_obj_t *orrery_val_lbl = NULL;

/* Metric / Imperial units */
static bool use_metric = true;  /* default: metric (°C, km/h) */
static lv_obj_t *unit_val_lbl = NULL;

static bool screen_on  = true;
static uint32_t last_activity_tick = 0;
static lv_obj_t *sleep_overlay = NULL;  /* absorbs touches while screen off */

/* Bluetooth indicator */
static lv_obj_t *bt_icon_label = NULL;

/* Digital time display (seg7) on clock face */
static seg7_display_t *clock_time_seg7 = NULL;
static lv_obj_t       *clock_ampm_lbl  = NULL;

/* Notification popup */
static lv_obj_t *notif_panel     = NULL;
static lv_obj_t *notif_src_lbl   = NULL;
static lv_obj_t *notif_title_lbl = NULL;
static lv_obj_t *notif_body_lbl  = NULL;
static lv_timer_t *notif_timer   = NULL;

static int saved_diameter = 0;
static lv_obj_t *saved_parent = NULL;
static int center_xy = 0;   /* centre of the dial = diameter / 2 */

/* ── Orrery (miniature heliocentric solar-system) ──────────────── */
#define NUM_PLANETS 8
static lv_obj_t *planet_dots[NUM_PLANETS];
static lv_obj_t *moon_dot = NULL;
static int pl_orbit_r[NUM_PLANETS];   /* pixel radii after scaling */

/* Planet-to-sun lines */
static lv_obj_t  *planet_lines[NUM_PLANETS];
static lv_point_t pl_line_pts[NUM_PLANETS][2];

/* Orbit ring circles (lv_obj children for Z-order over date) */
static lv_obj_t *orbit_circles[NUM_PLANETS];

/* Centre cap (doubles as sun when orrery is on) */
static lv_obj_t *cap_outer = NULL;
static lv_obj_t *cap_inner = NULL;

/* Moon canvas buffer for phase-shape rendering */
#define MOON_SZ 12
static uint8_t moon_cbuf[LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(MOON_SZ, MOON_SZ)];

static const struct {
    float L0;           /* mean longitude at J2000 (deg)      */
    float n;            /* mean daily motion (deg/day)        */
    int   orbit_r_base; /* base orbit radius (px @ diam 440)  */
    int   dot_sz;       /* dot diameter (px)                  */
    uint8_t cr, cg, cb; /* RGB colour                         */
} pl_def[NUM_PLANETS] = {
    /* Mercury */ { 252.251f,  4.09233445f,  36,18, 170,170,170 },
    /* Venus   */ { 181.980f,  1.60213049f,  54,22, 255,230,180 },
    /* Earth   */ { 100.464f,  0.98560028f,  72,22,  80,140,220 },
    /* Mars    */ { 355.433f,  0.52402068f,  90,18, 210, 90, 40 },
    /* Jupiter */ {  34.351f,  0.08308529f, 112,30, 200,160,100 },
    /* Saturn  */ {  50.077f,  0.03344414f, 134,25, 210,190,120 },
    /* Uranus  */ { 314.055f,  0.01172834f, 156,20, 130,210,220 },
    /* Neptune */ { 304.349f,  0.00598103f, 178,20,  60,100,220 },
};

/* Julian Day Number (noon UTC) from calendar date */
static double orrery_jdn(int y, int m, int d) {
    int a = (14 - m) / 12;
    int Y = y + 4800 - a;
    int M = m + 12 * a - 3;
    return (double)(d + (153 * M + 2) / 5
                  + 365 * Y + Y / 4 - Y / 100 + Y / 400 - 32045);
}

/* ── Helpers ───────────────────────────────────────────────────── */
#ifndef M_PI
#define M_PI 3.14159265358979f
#endif

/*  Draw moon phase on the canvas.
 *  phase_deg: 0 = new moon, 180 = full moon, 360 = new again.
 *  The lit/dark boundary (terminator) is rendered as a spherical
 *  projection, giving a proper crescent→gibbous→full shape.       */
static void draw_moon_phase(float phase_deg) {
    if (!moon_dot) return;
    const int R = MOON_SZ / 2;
    float phase_rad = phase_deg * (float)M_PI / 180.0f;
    float k = cosf(phase_rad);
    bool waning = (phase_deg >= 180.0f);

    lv_color_t lit_col  = lv_color_make(220, 220, 210);
    lv_color_t dark_col = lv_color_make(30, 30, 35);

    /* Clear to transparent */
    lv_canvas_fill_bg(moon_dot, lv_color_black(), LV_OPA_TRANSP);

    for (int py = 0; py < MOON_SZ; py++) {
        float dy = (float)(py - R) + 0.5f;
        for (int px = 0; px < MOON_SZ; px++) {
            float dx = (float)(px - R) + 0.5f;
            if (dx * dx + dy * dy > (float)(R * R)) continue;

            /* Terminator: spherical projection → ellipse at each row */
            float edge = sqrtf((float)(R * R) - dy * dy);
            float tx = k * edge;
            bool lit;
            if (!waning)
                lit = (dx >= tx);     /* waxing: right side lit first */
            else
                lit = (dx <= -tx);    /* waning: left side stays lit  */

            lv_canvas_set_px_color(moon_dot, px, py, lit ? lit_col : dark_col);
            lv_canvas_set_px_opa(moon_dot, px, py, LV_OPA_COVER);
        }
    }
}

/* Compute line endpoint for a hand on a 60-unit clock face.
   value = 0–59 (or 0–60), length = pixels from centre.
   Returns point relative to (0,0) as centre.               */
static void hand_endpoint(float value, int length, lv_point_t *out) {
    float angle = value * (2.0f * M_PI / 60.0f) - (M_PI / 2.0f);
    out->x = (int)(cosf(angle) * length);
    out->y = (int)(sinf(angle) * length);
}

#define MENU_PANEL_W 360

/* ── Menu open / close helpers ─────────────────────────────────── */
static void _menu_hide_ready_cb(lv_anim_t *a) {
    lv_obj_add_flag(menu_panel, LV_OBJ_FLAG_HIDDEN);
}

static void _menu_show(void) {
    if (menu_open) return;
    menu_open = true;
    lv_obj_clear_flag(menu_scrim, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(menu_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(menu_panel);  /* resolve LV_SIZE_CONTENT */
    int ph = lv_obj_get_height(menu_panel);
    /* Animate panel sliding down from top (centred) */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, menu_panel);
    lv_anim_set_values(&a, -ph, 40);  /* 40px from top to clear bezel */
    lv_anim_set_time(&a, 250);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void _menu_hide(void) {
    if (!menu_open) return;
    menu_open = false;
    lv_obj_add_flag(menu_scrim, LV_OBJ_FLAG_HIDDEN);
    int ph = lv_obj_get_height(menu_panel);
    /* Animate panel sliding up, then hide it */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, menu_panel);
    lv_anim_set_values(&a, 40, -ph);
    lv_anim_set_time(&a, 200);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_ready_cb(&a, _menu_hide_ready_cb);
    lv_anim_start(&a);
}

/* ── Menu event callbacks ──────────────────────────────────────── */
static void _menu_wifi_cb(lv_event_t *e) {
    bool on = lv_obj_has_state(wifi_sw, LV_STATE_CHECKED);
    if (cb_wifi_toggle) cb_wifi_toggle(on);
    if (cb_settings_changed) cb_settings_changed();
}

static void _menu_bt_cb(lv_event_t *e) {
    bool on = lv_obj_has_state(bt_sw, LV_STATE_CHECKED);
    if (cb_bt_toggle) cb_bt_toggle(on);
    if (cb_settings_changed) cb_settings_changed();
}

static void _reset_confirm_cb(lv_event_t *e) {
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *btn_txt = lv_msgbox_get_active_btn_text(mbox);
    if (btn_txt && strcmp(btn_txt, "Yes") == 0) {
        if (cb_reset_steps) cb_reset_steps();
        if (step_label) lv_label_set_text(step_label, "0");
    }
    lv_msgbox_close(mbox);
}

static void _menu_reset_cb(lv_event_t *e) {
    static const char *btns[] = {"Yes", "No", ""};
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Reset Steps",
        "Reset step counter\nto zero?", btns, false);
    lv_obj_set_style_bg_color(mbox, lv_color_make(30, 30, 40), 0);
    lv_obj_set_style_text_color(mbox, lv_color_white(), 0);
    lv_obj_set_style_border_color(mbox, lv_color_make(80, 80, 100), 0);
    lv_obj_set_style_border_width(mbox, 2, 0);
    lv_obj_set_style_radius(mbox, 20, 0);
    lv_obj_set_style_shadow_width(mbox, 30, 0);
    lv_obj_set_style_shadow_color(mbox, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(mbox, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(mbox, 20, 0);
    lv_obj_set_width(mbox, 260);
    lv_obj_center(mbox);

    /* Style the buttons */
    lv_obj_t *btnm = lv_msgbox_get_btns(mbox);
    lv_obj_set_style_bg_color(btnm, lv_color_make(50, 50, 65), 0);
    lv_obj_set_style_radius(btnm, 10, 0);
    lv_obj_set_style_border_width(btnm, 0, 0);
    lv_obj_set_style_text_color(btnm, lv_color_white(), 0);
    lv_obj_set_style_bg_color(btnm, lv_color_make(70, 70, 90), LV_PART_ITEMS);
    lv_obj_set_style_radius(btnm, 8, LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnm, 0, LV_PART_ITEMS);

    lv_obj_add_event_cb(mbox, _reset_confirm_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void _update_timeout_label(void) {
    if (!timeout_val_lbl) return;
    if (timeout_options[timeout_idx] == 0)
        lv_label_set_text(timeout_val_lbl, "Always");
    else {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ds", timeout_options[timeout_idx]);
        lv_label_set_text(timeout_val_lbl, buf);
    }
}

static void _menu_timeout_cb(lv_event_t *e) {
    timeout_idx = (timeout_idx + 1) % 4;
    _update_timeout_label();
    last_activity_tick = lv_tick_get();   /* reset timer on change */
    if (cb_settings_changed) cb_settings_changed();
}

static void _update_fmt_label(void) {
    if (!fmt_val_lbl) return;
    lv_label_set_text(fmt_val_lbl, time_24h ? "24h" : "12h");
}

static void _menu_fmt_cb(lv_event_t *e) {
    (void)e;
    time_24h = !time_24h;
    _update_fmt_label();
    if (cb_settings_changed) cb_settings_changed();
}

/* ── Orrery show/hide helper ─────────────────────────────────── */
static void _orrery_set_visible(bool visible) {
    for (int i = 0; i < NUM_PLANETS; i++) {
        if (orbit_circles[i]) {
            if (visible) lv_obj_clear_flag(orbit_circles[i], LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(orbit_circles[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (planet_dots[i]) {
            if (visible) lv_obj_clear_flag(planet_dots[i], LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(planet_dots[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (planet_lines[i]) {
            if (visible) lv_obj_clear_flag(planet_lines[i], LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(planet_lines[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (moon_dot) {
        if (visible) lv_obj_clear_flag(moon_dot, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(moon_dot, LV_OBJ_FLAG_HIDDEN);
    }
    /* Switch centre cap between sun (gold) and plain pivot (silver) */
    if (cap_outer) {
        if (visible) {
            lv_obj_set_style_bg_color(cap_outer, lv_color_make(180, 140, 30), 0);
            lv_obj_set_style_shadow_color(cap_outer, lv_color_make(255, 180, 30), 0);
            lv_obj_set_style_shadow_opa(cap_outer, LV_OPA_60, 0);
        } else {
            lv_obj_set_style_bg_color(cap_outer, lv_color_make(140, 140, 145), 0);
            lv_obj_set_style_shadow_color(cap_outer, lv_color_make(180, 180, 185), 0);
            lv_obj_set_style_shadow_opa(cap_outer, LV_OPA_30, 0);
        }
    }
    if (cap_inner) {
        if (visible)
            lv_obj_set_style_bg_color(cap_inner, lv_color_make(255, 210, 60), 0);
        else
            lv_obj_set_style_bg_color(cap_inner, lv_color_make(200, 200, 205), 0);
    }
}

static void _update_orrery_label(void) {
    if (!orrery_val_lbl) return;
    lv_label_set_text(orrery_val_lbl, orrery_on ? "ON" : "OFF");
}

static void _update_unit_label(void) {
    if (!unit_val_lbl) return;
    lv_label_set_text(unit_val_lbl, use_metric ? "\xC2\xB0" "C" : "\xC2\xB0" "F");
}

static void _menu_unit_cb(lv_event_t *e) {
    (void)e;
    use_metric = !use_metric;
    _update_unit_label();
    weather_screen_refresh();
    nav_screen_refresh();
    if (cb_settings_changed) cb_settings_changed();
}

static void _menu_orrery_cb(lv_event_t *e) {
    (void)e;
    orrery_on = !orrery_on;
    _update_orrery_label();
    _orrery_set_visible(orrery_on);
    if (cb_settings_changed) cb_settings_changed();
}

static void _menu_scrim_cb(lv_event_t *e) {
    _menu_hide();
}

static void _gesture_cb(lv_event_t *e) {
    if (!screen_on) return;  /* no gestures while screen is off */
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_BOTTOM && !menu_open) {
        _menu_show();
    } else if (dir == LV_DIR_TOP && menu_open) {
        _menu_hide();
    }
}

/* ────────────────────────────────────────────────────────────────── */

void clock_face_create(lv_obj_t *parent, int diameter) {
    if (!parent) parent = lv_scr_act();
    saved_diameter = diameter;
    saved_parent = parent;

    /* ── Dark background ─────────────────────────────────────── */
    lv_obj_set_style_bg_color(parent, COL_BG, 0);

    /* ── Build a temporary meter to snapshot static arcs/ticks ── */
    meter = lv_meter_create(parent);
    lv_obj_set_size(meter, diameter, diameter);
    lv_obj_center(meter);

    lv_obj_set_style_pad_all(meter, 0, 0);
    lv_obj_set_style_bg_color(meter, COL_BG, 0);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(meter, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(meter, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(meter, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(meter, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(meter, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(meter, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_bg_color(meter, COL_BG, LV_PART_TICKS);
    lv_obj_set_style_width(meter, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(meter, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(meter, LV_OPA_TRANSP, LV_PART_INDICATOR);

    lv_meter_scale_t *scale = lv_meter_add_scale(meter);
    main_scale = scale;
    lv_meter_set_scale_ticks(meter, scale,
        61, 1, (int)(diameter * 0.03), COL_TICK_MINOR);
    lv_meter_set_scale_major_ticks(meter, scale,
        5, 3, (int)(diameter * 0.06), COL_TICK_MAJOR,
        (int)(diameter * 0.06));
    lv_meter_set_scale_range(meter, scale, 0, 60, 360, 270);
    lv_obj_set_style_text_opa(meter, LV_OPA_TRANSP, LV_PART_TICKS);

    /* Outer ring arcs */
    {
        lv_meter_indicator_t *ring1 = lv_meter_add_arc(meter, scale,
            4, COL_RING_OUTER, 0);
        lv_meter_set_indicator_start_value(meter, ring1, 0);
        lv_meter_set_indicator_end_value(meter, ring1, 60);
        lv_meter_indicator_t *ring2 = lv_meter_add_arc(meter, scale,
            2, COL_RING_INNER, -4);
        lv_meter_set_indicator_start_value(meter, ring2, 0);
        lv_meter_set_indicator_end_value(meter, ring2, 60);
    }

    /* Circular gradient arcs */
    {
        typedef struct { int r_mod; uint8_t r, g, b; } grad_t;
        static const grad_t bands[] = {
            { 0,   12, 12, 20 },
            { -30, 14, 14, 25 },
            { -60, 16, 18, 32 },
            { -90, 18, 22, 40 },
            { -120, 20, 26, 48 },
            { -150, 22, 28, 52 },
            { -170, 24, 30, 55 },
            { -185, 22, 28, 50 },
            { -200, 18, 22, 40 },
        };
        int n = (int)(sizeof(bands) / sizeof(bands[0]));
        for (int i = 0; i < n; i++) {
            int width = (i == 0) ? (int)(diameter * 0.08)
                                 : (int)(diameter * 0.10);
            lv_meter_indicator_t *g = lv_meter_add_arc(meter, scale,
                width,
                lv_color_make(bands[i].r, bands[i].g, bands[i].b),
                bands[i].r_mod);
            lv_meter_set_indicator_start_value(meter, g, 0);
            lv_meter_set_indicator_end_value(meter, g, 60);
        }
    }

    /* Decorative inner ring */
    lv_meter_indicator_t *ring_inner = lv_meter_add_arc(meter, scale,
        2, COL_RING_INNER, -(int)(diameter * 0.12));
    lv_meter_set_indicator_start_value(meter, ring_inner, 0);
    lv_meter_set_indicator_end_value(meter, ring_inner, 60);

    /* ── Snapshot meter to PSRAM image ───────────────────────── */
    lv_obj_update_layout(meter);
    {
        uint32_t snap_size = lv_snapshot_buf_size_needed(meter, LV_IMG_CF_TRUE_COLOR);
#ifdef ESP_PLATFORM
        meter_snap_buf = heap_caps_malloc(snap_size, MALLOC_CAP_SPIRAM);
#else
        meter_snap_buf = malloc(snap_size);
#endif
        if (meter_snap_buf) {
            lv_snapshot_take_to_buf(meter, LV_IMG_CF_TRUE_COLOR,
                                   &meter_snap_dsc, meter_snap_buf, snap_size);
        }
    }

    /* Delete the expensive meter widget — we have the image now */
    lv_obj_del(meter);
    meter = NULL;
    main_scale = NULL;

    /* Display the snapshot as a static image */
    if (meter_snap_buf) {
        meter_img = lv_img_create(parent);
        lv_img_set_src(meter_img, &meter_snap_dsc);
        lv_obj_center(meter_img);
        lv_obj_clear_flag(meter_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    /* centre of the parent tile (children use parent coords now) */
    {
        int pw = lv_obj_get_width(parent);
        int ph = lv_obj_get_height(parent);
        if (pw == 0) pw = 466;
        if (ph == 0) ph = 466;
        center_xy = pw / 2;
    }

    /* ── Orrery orbit radii (compute only, drawing deferred) ─── */
    {
        float scl = (float)(diameter / 2) / 220.0f;
        for (int i = 0; i < NUM_PLANETS; i++) {
            pl_orbit_r[i] = (int)(pl_def[i].orbit_r_base * scl);
        }
    }

    /* ── Hour-number labels ──────────────────────────────────── */
    int dial_r = diameter / 2;
    int label_radius = dial_r - (int)(diameter * 0.14);
    for (int i = 0; i < 12; i++) {
        int num = (i == 0) ? 12 : i;
        float a = (float)i * 30.0f * 3.14159265f / 180.0f;
        int cx = center_xy + (int)(sinf(a) * label_radius);
        int cy = center_xy - (int)(cosf(a) * label_radius);

        lv_obj_t *lbl = lv_label_create(parent);
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", num);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, COL_HOUR_NUM, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_pos(lbl, cx, cy);
        lv_obj_refr_size(lbl);
        lv_obj_set_pos(lbl,
            cx - lv_obj_get_width(lbl) / 2,
            cy - lv_obj_get_height(lbl) / 2);
        hour_labels[i] = lbl;
    }

    /* ── Date window (below centre, drawn before planets so behind) ── */
    {
        int win_w = (int)(diameter * 0.14);
        int win_h = (int)(diameter * 0.09);
        int win_y_offset = (int)(diameter * 0.15);

        date_window = lv_obj_create(parent);
        lv_obj_set_size(date_window, win_w + 4, win_h + 4);
        lv_obj_align(date_window, LV_ALIGN_CENTER, 0, win_y_offset);
        lv_obj_set_style_bg_color(date_window, COL_DATE_BORDER, 0);
        lv_obj_set_style_bg_opa(date_window, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(date_window, 4, 0);
        lv_obj_set_style_border_width(date_window, 1, 0);
        lv_obj_set_style_border_color(date_window, COL_RING_OUTER, 0);
        lv_obj_set_style_pad_all(date_window, 0, 0);
        lv_obj_set_style_shadow_width(date_window, 8, 0);
        lv_obj_set_style_shadow_color(date_window, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(date_window, LV_OPA_40, 0);
        lv_obj_clear_flag(date_window, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *inner = lv_obj_create(date_window);
        lv_obj_set_size(inner, win_w, win_h);
        lv_obj_center(inner);
        lv_obj_set_style_bg_color(inner, COL_DATE_BG, 0);
        lv_obj_set_style_bg_opa(inner, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(inner, 3, 0);
        lv_obj_set_style_border_width(inner, 0, 0);
        lv_obj_set_style_pad_all(inner, 0, 0);
        lv_obj_clear_flag(inner, LV_OBJ_FLAG_SCROLLABLE);

        date_stripe_top = lv_obj_create(inner);
        lv_obj_set_size(date_stripe_top, win_w, 2);
        lv_obj_align(date_stripe_top, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(date_stripe_top, COL_DATE_SHADOW, 0);
        lv_obj_set_style_bg_opa(date_stripe_top, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(date_stripe_top, 0, 0);
        lv_obj_set_style_radius(date_stripe_top, 0, 0);

        date_stripe_bot = lv_obj_create(inner);
        lv_obj_set_size(date_stripe_bot, win_w, 2);
        lv_obj_align(date_stripe_bot, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(date_stripe_bot, COL_DATE_SHADOW, 0);
        lv_obj_set_style_bg_opa(date_stripe_bot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(date_stripe_bot, 0, 0);
        lv_obj_set_style_radius(date_stripe_bot, 0, 0);

        date_label = lv_label_create(inner);
        lv_label_set_text(date_label, "28");
        lv_obj_set_style_text_color(date_label, COL_DATE_FG, 0);
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_28, 0);
        lv_obj_center(date_label);
    }

    /* ── Digital time display (above step counter) ── */
    {
        int time_y_offset = -(int)(diameter * 0.22);
        lv_color_t hdr_col = lv_color_make(200, 200, 195);
        clock_time_seg7 = seg7_create(parent, 5, 22, hdr_col, 3);
        lv_obj_align(seg7_get_obj(clock_time_seg7),
                     LV_ALIGN_CENTER, 0, time_y_offset);

        clock_ampm_lbl = lv_label_create(parent);
        lv_label_set_text(clock_ampm_lbl, "");
        lv_obj_set_style_text_color(clock_ampm_lbl, hdr_col, 0);
        lv_obj_set_style_text_font(clock_ampm_lbl, &lv_font_montserrat_20, 0);
        lv_obj_add_flag(clock_ampm_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* ── Step counter (above centre, same Z as date window) ── */
    {
        int step_y_offset = -(int)(diameter * 0.15);

        /* Shoe icon drawn as a small canvas */
        step_icon = lv_canvas_create(parent);
        lv_canvas_set_buffer(step_icon, shoe_cbuf, SHOE_W, SHOE_H,
                             LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_canvas_fill_bg(step_icon, lv_color_black(), LV_OPA_TRANSP);
        lv_color_t shoe_col = lv_color_make(180, 220, 180);
        for (int py = 0; py < SHOE_H; py++) {
            for (int px = 0; px < SHOE_W; px++) {
                if (shoe_px[py][px]) {
                    lv_canvas_set_px_color(step_icon, px, py, shoe_col);
                    lv_canvas_set_px_opa(step_icon, px, py, LV_OPA_COVER);
                }
            }
        }
        lv_obj_align(step_icon, LV_ALIGN_CENTER, -28, step_y_offset);
        lv_obj_clear_flag(step_icon,
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        /* Step count label */
        step_label = lv_label_create(parent);
        lv_label_set_text(step_label, "0");
        lv_obj_set_style_text_color(step_label, lv_color_make(180, 220, 180), 0);
        lv_obj_set_style_text_font(step_label, &lv_font_montserrat_20, 0);
        lv_obj_align(step_label, LV_ALIGN_CENTER, 4, step_y_offset);

        /* Bluetooth icon (below step counter) */
        bt_icon_label = lv_label_create(parent);
        lv_label_set_text(bt_icon_label, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(bt_icon_label, lv_color_make(80, 80, 90), 0);
        lv_obj_set_style_text_font(bt_icon_label, &lv_font_montserrat_20, 0);
        lv_obj_align(bt_icon_label, LV_ALIGN_CENTER, 0, step_y_offset + 24);
    }

    /* ── Battery icon + percentage (below date, drawn before planets) ── */
    {
        int batt_y_offset = (int)(diameter * 0.28);

        batt_icon_label = lv_label_create(parent);
        lv_label_set_text(batt_icon_label, LV_SYMBOL_BATTERY_FULL);
        lv_obj_set_style_text_color(batt_icon_label, COL_BATT, 0);
        lv_obj_set_style_text_font(batt_icon_label, &lv_font_montserrat_20, 0);
        lv_obj_align(batt_icon_label, LV_ALIGN_CENTER, -26, batt_y_offset);

        batt_pct_label = lv_label_create(parent);
        lv_label_set_text(batt_pct_label, "--%%");
        lv_obj_set_style_text_color(batt_pct_label, lv_color_make(160, 160, 160), 0);
        lv_obj_set_style_text_font(batt_pct_label, &lv_font_montserrat_20, 0);
        lv_obj_align(batt_pct_label, LV_ALIGN_CENTER, 18, batt_y_offset);
    }

    /* ── Orrery orbit circles (created after date for Z-order) ── */
    for (int i = 0; i < NUM_PLANETS; i++) {
        int d2 = pl_orbit_r[i] * 2;
        orbit_circles[i] = lv_obj_create(parent);
        lv_obj_set_size(orbit_circles[i], d2, d2);
        lv_obj_set_pos(orbit_circles[i],
            center_xy - pl_orbit_r[i], center_xy - pl_orbit_r[i]);
        lv_obj_set_style_radius(orbit_circles[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(orbit_circles[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(orbit_circles[i], 3, 0);
        lv_obj_set_style_border_color(orbit_circles[i],
            lv_color_make(40, 40, 60), 0);
        lv_obj_set_style_border_opa(orbit_circles[i], LV_OPA_70, 0);
        lv_obj_set_style_pad_all(orbit_circles[i], 0, 0);
        lv_obj_clear_flag(orbit_circles[i],
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    /* ── Planet-to-sun lines ────────────────────────────────── */
    for (int i = 0; i < NUM_PLANETS; i++) {
        pl_line_pts[i][0] = (lv_point_t){center_xy, center_xy};
        pl_line_pts[i][1] = (lv_point_t){center_xy, center_xy - pl_orbit_r[i]};
        planet_lines[i] = lv_line_create(parent);
        lv_line_set_points(planet_lines[i], pl_line_pts[i], 2);
        lv_obj_set_style_line_width(planet_lines[i], 2, 0);
        lv_obj_set_style_line_color(planet_lines[i],
            lv_color_make(pl_def[i].cr / 3, pl_def[i].cg / 3, pl_def[i].cb / 3), 0);
        lv_obj_set_style_line_opa(planet_lines[i], LV_OPA_50, 0);
        lv_obj_clear_flag(planet_lines[i], LV_OBJ_FLAG_CLICKABLE);
    }

    /* ── Orrery planet dots (created after date so they render on top) ── */
    for (int i = 0; i < NUM_PLANETS; i++) {
        int sz = pl_def[i].dot_sz;
        planet_dots[i] = lv_obj_create(parent);
        lv_obj_set_size(planet_dots[i], sz, sz);
        lv_obj_set_pos(planet_dots[i],
            center_xy - sz / 2,
            center_xy - pl_orbit_r[i] - sz / 2);
        lv_obj_set_style_radius(planet_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(planet_dots[i],
            lv_color_make(pl_def[i].cr, pl_def[i].cg, pl_def[i].cb), 0);
        lv_obj_set_style_bg_opa(planet_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(planet_dots[i], 1, 0);
        lv_obj_set_style_border_color(planet_dots[i],
            lv_color_make(
                (uint8_t)((pl_def[i].cr > 60) ? pl_def[i].cr - 40 : pl_def[i].cr + 60),
                (uint8_t)((pl_def[i].cg > 60) ? pl_def[i].cg - 40 : pl_def[i].cg + 60),
                (uint8_t)((pl_def[i].cb > 60) ? pl_def[i].cb - 40 : pl_def[i].cb + 60)), 0);
        lv_obj_set_style_pad_all(planet_dots[i], 0, 0);
        lv_obj_clear_flag(planet_dots[i],
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    /* Moon (canvas for phase-shape rendering) */
    moon_dot = lv_canvas_create(parent);
    lv_canvas_set_buffer(moon_dot, moon_cbuf, MOON_SZ, MOON_SZ,
                         LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(moon_dot, lv_color_black(), LV_OPA_TRANSP);
    lv_obj_set_pos(moon_dot, center_xy - MOON_SZ / 2, center_xy - MOON_SZ / 2);
    lv_obj_clear_flag(moon_dot,
        LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    /* ── Battery arc (lightweight lv_arc widget) ───────────────── */
    {
        int arc_w = (int)(diameter * 0.025);
        int arc_r_offset = (int)(diameter * 0.005);
        int arc_diam = diameter - arc_r_offset * 2;
        batt_arc = lv_arc_create(parent);
        lv_obj_set_size(batt_arc, arc_diam, arc_diam);
        lv_obj_center(batt_arc);
        lv_arc_set_rotation(batt_arc, 270);
        lv_arc_set_range(batt_arc, 0, 100);
        lv_arc_set_value(batt_arc, 0);
        lv_arc_set_bg_angles(batt_arc, 0, 360);
        lv_obj_set_style_arc_width(batt_arc, arc_w, LV_PART_MAIN);
        lv_obj_set_style_arc_color(batt_arc, COL_BG, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(batt_arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(batt_arc, arc_w, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(batt_arc, COL_BATT, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(batt_arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(batt_arc, 0, LV_PART_KNOB);
        lv_obj_clear_flag(batt_arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    /* ── Clock hands (lv_line on parent – NOT meter children) ──── */
    /* Points use center_xy as the centre (no border offset) */
    int half = center_xy;
    int len_hour = (int)(diameter * 0.28);
    int len_min  = (int)(diameter * 0.40);
    int len_sec  = (int)(diameter * 0.43);

    /* Hour hand */
    pts_hour[0] = (lv_point_t){half, half};
    pts_hour[1] = (lv_point_t){half, half - len_hour};
    line_hour = lv_line_create(parent);
    lv_line_set_points(line_hour, pts_hour, 2);
    lv_obj_set_style_line_width(line_hour, 6, 0);
    lv_obj_set_style_line_color(line_hour, COL_HAND_HR, 0);
    lv_obj_set_style_line_rounded(line_hour, true, 0);
    lv_obj_clear_flag(line_hour, LV_OBJ_FLAG_CLICKABLE);

    /* Minute hand */
    pts_min[0] = (lv_point_t){half, half};
    pts_min[1] = (lv_point_t){half, half - len_min};
    line_min = lv_line_create(parent);
    lv_line_set_points(line_min, pts_min, 2);
    lv_obj_set_style_line_width(line_min, 4, 0);
    lv_obj_set_style_line_color(line_min, COL_HAND_MIN, 0);
    lv_obj_set_style_line_rounded(line_min, true, 0);
    lv_obj_clear_flag(line_min, LV_OBJ_FLAG_CLICKABLE);

    /* Second hand */
    pts_sec[0] = (lv_point_t){half, half};
    pts_sec[1] = (lv_point_t){half, half - len_sec};
    line_sec = lv_line_create(parent);
    lv_line_set_points(line_sec, pts_sec, 2);
    lv_obj_set_style_line_width(line_sec, 2, 0);
    lv_obj_set_style_line_color(line_sec, COL_HAND_SEC, 0);
    lv_obj_set_style_line_rounded(line_sec, true, 0);
    lv_obj_clear_flag(line_sec, LV_OBJ_FLAG_CLICKABLE);

    /* ── Centre cap (layered for depth) ──────────────────────── */
    cap_outer = lv_obj_create(parent);
    lv_obj_set_size(cap_outer, 36, 36);
    lv_obj_set_pos(cap_outer, center_xy - 18, center_xy - 18);
    lv_obj_set_style_radius(cap_outer, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap_outer, lv_color_make(180, 140, 30), 0);
    lv_obj_set_style_bg_opa(cap_outer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cap_outer, 0, 0);
    lv_obj_set_style_pad_all(cap_outer, 0, 0);
    lv_obj_set_style_shadow_width(cap_outer, 16, 0);
    lv_obj_set_style_shadow_color(cap_outer, lv_color_make(255, 180, 30), 0);
    lv_obj_set_style_shadow_opa(cap_outer, LV_OPA_60, 0);

    cap_inner = lv_obj_create(parent);
    lv_obj_set_size(cap_inner, 20, 20);
    lv_obj_set_pos(cap_inner, center_xy - 10, center_xy - 10);
    lv_obj_set_style_radius(cap_inner, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap_inner, lv_color_make(255, 210, 60), 0);
    lv_obj_set_style_bg_opa(cap_inner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cap_inner, 0, 0);
    lv_obj_set_style_pad_all(cap_inner, 0, 0);

    /* Initial position */
    clock_face_update(12, 0, 0);
    last_activity_tick = lv_tick_get();
}

/* ══════════════════════════════════════════════════════════════════
 *  clock_face_create_overlays – settings menu, notification popup,
 *  sleep overlay, and gesture handler.  Call AFTER clock_face_create().
 *  overlay_parent is typically lv_scr_act() so these float above
 *  all tileview tiles.
 * ══════════════════════════════════════════════════════════════════ */
void clock_face_create_overlays(lv_obj_t *overlay_parent) {
    lv_obj_t *parent = overlay_parent;   /* reuse existing references */
    parent_scr = parent;
    {
        int scr_w = lv_obj_get_width(parent);
        int scr_h = lv_obj_get_height(parent);
        if (scr_w == 0) scr_w = 466;
        if (scr_h == 0) scr_h = 466;

        /* Dim scrim (click to dismiss) */
        menu_scrim = lv_obj_create(parent);
        lv_obj_set_size(menu_scrim, scr_w, scr_h);
        lv_obj_set_pos(menu_scrim, 0, 0);
        lv_obj_set_style_bg_color(menu_scrim, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(menu_scrim, LV_OPA_60, 0);
        lv_obj_set_style_border_width(menu_scrim, 0, 0);
        lv_obj_set_style_radius(menu_scrim, 0, 0);
        lv_obj_set_style_pad_all(menu_scrim, 0, 0);
        lv_obj_clear_flag(menu_scrim, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(menu_scrim, LV_OBJ_FLAG_HIDDEN);

        /* Menu panel – rounded card centred horizontally */
        int panel_x = (scr_w - MENU_PANEL_W) / 2;
        menu_panel = lv_obj_create(parent);
        lv_obj_set_size(menu_panel, MENU_PANEL_W, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(menu_panel, 380, 0);
        lv_obj_set_pos(menu_panel, panel_x, -400);  /* start off-screen */
        lv_obj_set_style_bg_color(menu_panel, lv_color_make(20, 20, 30), 0);
        lv_obj_set_style_bg_opa(menu_panel, LV_OPA_90, 0);
        lv_obj_set_style_radius(menu_panel, 24, 0);
        lv_obj_set_style_border_width(menu_panel, 1, 0);
        lv_obj_set_style_border_color(menu_panel, lv_color_make(60, 60, 80), 0);
        lv_obj_set_style_pad_left(menu_panel, 20, 0);
        lv_obj_set_style_pad_right(menu_panel, 20, 0);
        lv_obj_set_style_pad_top(menu_panel, 20, 0);
        lv_obj_set_style_pad_bottom(menu_panel, 16, 0);
        lv_obj_set_flex_flow(menu_panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(menu_panel, 10, 0);
        lv_obj_set_style_shadow_width(menu_panel, 30, 0);
        lv_obj_set_style_shadow_color(menu_panel, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(menu_panel, LV_OPA_70, 0);
        lv_obj_set_style_shadow_ofs_y(menu_panel, 8, 0);
        lv_obj_set_style_shadow_spread(menu_panel, 4, 0);
        lv_obj_add_flag(menu_panel, LV_OBJ_FLAG_HIDDEN);
        /* Scrollbar styling */
        lv_obj_set_style_bg_color(menu_panel, lv_color_make(80, 80, 100), LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(menu_panel, LV_OPA_50, LV_PART_SCROLLBAR);
        lv_obj_set_style_width(menu_panel, 4, LV_PART_SCROLLBAR);

        /* Title */
        lv_obj_t *title = lv_label_create(menu_panel);
        lv_label_set_text(title, "Settings");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_set_style_pad_bottom(title, 4, 0);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

        /* ── WiFi row ── */
        lv_obj_t *wifi_row = lv_obj_create(menu_panel);
        lv_obj_set_size(wifi_row, lv_pct(100), 46);
        lv_obj_set_style_bg_color(wifi_row, lv_color_make(30, 30, 45), 0);
        lv_obj_set_style_bg_opa(wifi_row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(wifi_row, 12, 0);
        lv_obj_set_style_border_width(wifi_row, 0, 0);
        lv_obj_set_style_pad_all(wifi_row, 0, 0);
        lv_obj_clear_flag(wifi_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *wifi_icon = lv_label_create(wifi_row);
        lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifi_icon, lv_color_make(100, 180, 255), 0);
        lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_24, 0);
        lv_obj_align(wifi_icon, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *wifi_lbl = lv_label_create(wifi_row);
        lv_label_set_text(wifi_lbl, "WiFi");
        lv_obj_set_style_text_color(wifi_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(wifi_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(wifi_lbl, LV_ALIGN_LEFT_MID, 44, 0);

        wifi_sw = lv_switch_create(wifi_row);
        lv_obj_set_size(wifi_sw, 50, 26);
        lv_obj_align(wifi_sw, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_bg_color(wifi_sw, lv_color_make(60, 60, 70), LV_PART_MAIN);
        lv_obj_set_style_bg_color(wifi_sw, lv_color_make(40, 120, 220), LV_PART_INDICATOR | LV_STATE_CHECKED);

        /* ── Bluetooth row ── */
        lv_obj_t *bt_row = lv_obj_create(menu_panel);
        lv_obj_set_size(bt_row, lv_pct(100), 46);
        lv_obj_set_style_bg_color(bt_row, lv_color_make(30, 30, 45), 0);
        lv_obj_set_style_bg_opa(bt_row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bt_row, 12, 0);
        lv_obj_set_style_border_width(bt_row, 0, 0);
        lv_obj_set_style_pad_all(bt_row, 0, 0);
        lv_obj_clear_flag(bt_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *bt_icon = lv_label_create(bt_row);
        lv_label_set_text(bt_icon, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(bt_icon, lv_color_make(80, 130, 255), 0);
        lv_obj_set_style_text_font(bt_icon, &lv_font_montserrat_24, 0);
        lv_obj_align(bt_icon, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *bt_lbl = lv_label_create(bt_row);
        lv_label_set_text(bt_lbl, "Bluetooth");
        lv_obj_set_style_text_color(bt_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(bt_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(bt_lbl, LV_ALIGN_LEFT_MID, 44, 0);

        bt_sw = lv_switch_create(bt_row);
        lv_obj_set_size(bt_sw, 50, 26);
        lv_obj_align(bt_sw, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_bg_color(bt_sw, lv_color_make(60, 60, 70), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bt_sw, lv_color_make(40, 120, 220), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_state(bt_sw, LV_STATE_CHECKED);  /* default on */

        /* ── Reset Steps row ── */
        lv_obj_t *step_row = lv_obj_create(menu_panel);
        lv_obj_set_size(step_row, lv_pct(100), 46);
        lv_obj_set_style_bg_color(step_row, lv_color_make(30, 30, 45), 0);
        lv_obj_set_style_bg_opa(step_row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(step_row, 12, 0);
        lv_obj_set_style_border_width(step_row, 0, 0);
        lv_obj_set_style_pad_all(step_row, 0, 0);
        lv_obj_clear_flag(step_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *step_icon_menu = lv_label_create(step_row);
        lv_label_set_text(step_icon_menu, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(step_icon_menu, lv_color_make(180, 220, 180), 0);
        lv_obj_set_style_text_font(step_icon_menu, &lv_font_montserrat_24, 0);
        lv_obj_align(step_icon_menu, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *step_lbl_menu = lv_label_create(step_row);
        lv_label_set_text(step_lbl_menu, "Reset Steps");
        lv_obj_set_style_text_color(step_lbl_menu, lv_color_white(), 0);
        lv_obj_set_style_text_font(step_lbl_menu, &lv_font_montserrat_20, 0);
        lv_obj_align(step_lbl_menu, LV_ALIGN_LEFT_MID, 44, 0);

        lv_obj_t *reset_btn = lv_btn_create(step_row);
        lv_obj_set_size(reset_btn, 70, 32);
        lv_obj_align(reset_btn, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_bg_color(reset_btn, lv_color_make(180, 60, 60), 0);
        lv_obj_set_style_radius(reset_btn, 6, 0);
        lv_obj_t *btn_lbl = lv_label_create(reset_btn);
        lv_label_set_text(btn_lbl, "Reset");
        lv_obj_set_style_text_color(btn_lbl, lv_color_white(), 0);
        lv_obj_center(btn_lbl);

        /* ── Screen Timeout row ── */
        lv_obj_t *to_row = lv_obj_create(menu_panel);
        lv_obj_set_size(to_row, lv_pct(100), 46);
        lv_obj_set_style_bg_color(to_row, lv_color_make(30, 30, 45), 0);
        lv_obj_set_style_bg_opa(to_row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(to_row, 12, 0);
        lv_obj_set_style_border_width(to_row, 0, 0);
        lv_obj_set_style_pad_all(to_row, 0, 0);
        lv_obj_clear_flag(to_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *to_icon = lv_label_create(to_row);
        lv_label_set_text(to_icon, LV_SYMBOL_EYE_OPEN);
        lv_obj_set_style_text_color(to_icon, lv_color_make(220, 180, 80), 0);
        lv_obj_set_style_text_font(to_icon, &lv_font_montserrat_24, 0);
        lv_obj_align(to_icon, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *to_lbl = lv_label_create(to_row);
        lv_label_set_text(to_lbl, "Screen");
        lv_obj_set_style_text_color(to_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(to_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(to_lbl, LV_ALIGN_LEFT_MID, 44, 0);

        lv_obj_t *to_btn = lv_btn_create(to_row);
        lv_obj_set_size(to_btn, 80, 32);
        lv_obj_align(to_btn, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_bg_color(to_btn, lv_color_make(60, 60, 80), 0);
        lv_obj_set_style_radius(to_btn, 6, 0);
        timeout_val_lbl = lv_label_create(to_btn);
        lv_obj_set_style_text_color(timeout_val_lbl, lv_color_white(), 0);
        lv_obj_center(timeout_val_lbl);
        _update_timeout_label();

        /* ── 12/24h Format row ── */
        lv_obj_t *fmt_row = lv_obj_create(menu_panel);
        lv_obj_set_size(fmt_row, lv_pct(100), 46);
        lv_obj_set_style_bg_color(fmt_row, lv_color_make(30, 30, 45), 0);
        lv_obj_set_style_bg_opa(fmt_row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(fmt_row, 12, 0);
        lv_obj_set_style_border_width(fmt_row, 0, 0);
        lv_obj_set_style_pad_all(fmt_row, 0, 0);
        lv_obj_clear_flag(fmt_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *fmt_icon = lv_label_create(fmt_row);
        lv_label_set_text(fmt_icon, LV_SYMBOL_LOOP);
        lv_obj_set_style_text_color(fmt_icon, lv_color_make(180, 160, 220), 0);
        lv_obj_set_style_text_font(fmt_icon, &lv_font_montserrat_24, 0);
        lv_obj_align(fmt_icon, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *fmt_lbl = lv_label_create(fmt_row);
        lv_label_set_text(fmt_lbl, "Clock");
        lv_obj_set_style_text_color(fmt_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(fmt_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(fmt_lbl, LV_ALIGN_LEFT_MID, 44, 0);

        lv_obj_t *fmt_btn = lv_btn_create(fmt_row);
        lv_obj_set_size(fmt_btn, 80, 32);
        lv_obj_align(fmt_btn, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_bg_color(fmt_btn, lv_color_make(60, 60, 80), 0);
        lv_obj_set_style_radius(fmt_btn, 6, 0);
        fmt_val_lbl = lv_label_create(fmt_btn);
        lv_obj_set_style_text_color(fmt_val_lbl, lv_color_white(), 0);
        lv_obj_center(fmt_val_lbl);
        _update_fmt_label();

        /* Event: WiFi switch toggled */
        lv_obj_add_event_cb(wifi_sw, _menu_wifi_cb, LV_EVENT_VALUE_CHANGED, NULL);
        /* Event: Bluetooth switch toggled */
        lv_obj_add_event_cb(bt_sw, _menu_bt_cb, LV_EVENT_VALUE_CHANGED, NULL);
        /* Event: Reset button pressed */
        lv_obj_add_event_cb(reset_btn, _menu_reset_cb, LV_EVENT_CLICKED, NULL);
        /* Event: Timeout button tapped – cycles 5s→10s→15s→Always */
        lv_obj_add_event_cb(to_btn, _menu_timeout_cb, LV_EVENT_CLICKED, NULL);
        /* Event: Format button tapped – cycles 12h↔24h */
        lv_obj_add_event_cb(fmt_btn, _menu_fmt_cb, LV_EVENT_CLICKED, NULL);

        /* ── Orrery row ── */
        lv_obj_t *orr_row = lv_obj_create(menu_panel);
        lv_obj_set_size(orr_row, lv_pct(100), 46);
        lv_obj_set_style_bg_color(orr_row, lv_color_make(30, 30, 45), 0);
        lv_obj_set_style_bg_opa(orr_row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(orr_row, 12, 0);
        lv_obj_set_style_border_width(orr_row, 0, 0);
        lv_obj_set_style_pad_all(orr_row, 0, 0);
        lv_obj_clear_flag(orr_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *orr_icon = lv_label_create(orr_row);
        lv_label_set_text(orr_icon, LV_SYMBOL_GPS);
        lv_obj_set_style_text_color(orr_icon, lv_color_make(200, 160, 80), 0);
        lv_obj_set_style_text_font(orr_icon, &lv_font_montserrat_24, 0);
        lv_obj_align(orr_icon, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *orr_lbl = lv_label_create(orr_row);
        lv_label_set_text(orr_lbl, "Orrery");
        lv_obj_set_style_text_color(orr_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(orr_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(orr_lbl, LV_ALIGN_LEFT_MID, 44, 0);

        lv_obj_t *orr_btn = lv_btn_create(orr_row);
        lv_obj_set_size(orr_btn, 80, 32);
        lv_obj_align(orr_btn, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_bg_color(orr_btn, lv_color_make(60, 60, 80), 0);
        lv_obj_set_style_radius(orr_btn, 6, 0);
        orrery_val_lbl = lv_label_create(orr_btn);
        lv_obj_set_style_text_color(orrery_val_lbl, lv_color_white(), 0);
        lv_obj_center(orrery_val_lbl);
        _update_orrery_label();

        lv_obj_add_event_cb(orr_btn, _menu_orrery_cb, LV_EVENT_CLICKED, NULL);

        /* ── Units row (Metric / Imperial) ── */
        lv_obj_t *unit_row = lv_obj_create(menu_panel);
        lv_obj_set_size(unit_row, lv_pct(100), 46);
        lv_obj_set_style_bg_color(unit_row, lv_color_make(30, 30, 45), 0);
        lv_obj_set_style_bg_opa(unit_row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(unit_row, 12, 0);
        lv_obj_set_style_border_width(unit_row, 0, 0);
        lv_obj_set_style_pad_all(unit_row, 0, 0);
        lv_obj_clear_flag(unit_row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *unit_icon = lv_label_create(unit_row);
        lv_label_set_text(unit_icon, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_color(unit_icon, lv_color_make(180, 200, 160), 0);
        lv_obj_set_style_text_font(unit_icon, &lv_font_montserrat_24, 0);
        lv_obj_align(unit_icon, LV_ALIGN_LEFT_MID, 8, 0);

        lv_obj_t *unit_lbl = lv_label_create(unit_row);
        lv_label_set_text(unit_lbl, "Units");
        lv_obj_set_style_text_color(unit_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(unit_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(unit_lbl, LV_ALIGN_LEFT_MID, 44, 0);

        lv_obj_t *unit_btn = lv_btn_create(unit_row);
        lv_obj_set_size(unit_btn, 80, 32);
        lv_obj_align(unit_btn, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_bg_color(unit_btn, lv_color_make(60, 60, 80), 0);
        lv_obj_set_style_radius(unit_btn, 6, 0);
        unit_val_lbl = lv_label_create(unit_btn);
        lv_obj_set_style_text_color(unit_val_lbl, lv_color_white(), 0);
        lv_obj_center(unit_val_lbl);
        _update_unit_label();

        lv_obj_add_event_cb(unit_btn, _menu_unit_cb, LV_EVENT_CLICKED, NULL);

        /* Event: Scrim clicked → close menu */
        lv_obj_add_event_cb(menu_scrim, _menu_scrim_cb, LV_EVENT_CLICKED, NULL);

        /* Gesture on parent to detect swipe-down */
        lv_obj_add_event_cb(parent, _gesture_cb, LV_EVENT_GESTURE, NULL);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_GESTURE_BUBBLE);
    }

    /* ── Sleep overlay (absorbs all touches when screen is off) ── */
    {
        int scr_w = lv_obj_get_width(parent);
        int scr_h = lv_obj_get_height(parent);
        if (scr_w == 0) scr_w = 466;
        if (scr_h == 0) scr_h = 466;
        sleep_overlay = lv_obj_create(parent);
        lv_obj_set_size(sleep_overlay, scr_w, scr_h);
        lv_obj_set_pos(sleep_overlay, 0, 0);
        lv_obj_set_style_bg_opa(sleep_overlay, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(sleep_overlay, 0, 0);
        lv_obj_set_style_radius(sleep_overlay, 0, 0);
        lv_obj_set_style_pad_all(sleep_overlay, 0, 0);
        lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(sleep_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);  /* starts hidden */
    }

    /* ── Notification popup panel (hidden until needed) ── */
    {
        int scr_w = lv_obj_get_width(parent);
        if (scr_w == 0) scr_w = 466;

        notif_panel = lv_obj_create(parent);
        lv_obj_set_size(notif_panel, 320, LV_SIZE_CONTENT);
        lv_obj_align(notif_panel, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_color(notif_panel, lv_color_make(25, 25, 40), 0);
        lv_obj_set_style_bg_opa(notif_panel, LV_OPA_90, 0);
        lv_obj_set_style_radius(notif_panel, 20, 0);
        lv_obj_set_style_border_width(notif_panel, 1, 0);
        lv_obj_set_style_border_color(notif_panel, lv_color_make(60, 60, 100), 0);
        lv_obj_set_style_shadow_width(notif_panel, 30, 0);
        lv_obj_set_style_shadow_color(notif_panel, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(notif_panel, LV_OPA_70, 0);
        lv_obj_set_style_pad_all(notif_panel, 16, 0);
        lv_obj_set_style_pad_top(notif_panel, 12, 0);
        lv_obj_set_style_pad_bottom(notif_panel, 14, 0);
        lv_obj_clear_flag(notif_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(notif_panel, LV_OBJ_FLAG_HIDDEN);

        /* Source app label (e.g. "WhatsApp") */
        notif_src_lbl = lv_label_create(notif_panel);
        lv_label_set_text(notif_src_lbl, "");
        lv_obj_set_style_text_color(notif_src_lbl, lv_color_make(120, 160, 255), 0);
        lv_obj_set_style_text_font(notif_src_lbl, &lv_font_montserrat_20, 0);
        lv_obj_align(notif_src_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

        /* Title label */
        notif_title_lbl = lv_label_create(notif_panel);
        lv_label_set_text(notif_title_lbl, "");
        lv_obj_set_style_text_color(notif_title_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(notif_title_lbl, &lv_font_montserrat_20, 0);
        lv_label_set_long_mode(notif_title_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(notif_title_lbl, 280);
        lv_obj_align(notif_title_lbl, LV_ALIGN_TOP_LEFT, 0, 26);

        /* Body label */
        notif_body_lbl = lv_label_create(notif_panel);
        lv_label_set_text(notif_body_lbl, "");
        lv_obj_set_style_text_color(notif_body_lbl, lv_color_make(180, 180, 190), 0);
        lv_obj_set_style_text_font(notif_body_lbl, &lv_font_montserrat_20, 0);
        lv_label_set_long_mode(notif_body_lbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(notif_body_lbl, 280);
        lv_obj_align(notif_body_lbl, LV_ALIGN_TOP_LEFT, 0, 50);
    }
}

void clock_face_update(int h, int m, int s) {
    if (!saved_parent) return;

    /* Update digital time display */
    if (clock_time_seg7) {
        char buf[8];
        if (time_24h) {
            snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
        } else {
            int h12 = h % 12;
            if (h12 == 0) h12 = 12;
            snprintf(buf, sizeof(buf), "%d:%02d", h12, m);
        }
        seg7_set_text(clock_time_seg7, buf);
        lv_obj_align(seg7_get_obj(clock_time_seg7),
                     LV_ALIGN_CENTER, time_24h ? 0 : -17,
                     -(int)(saved_diameter * 0.22));

        if (time_24h) {
            lv_obj_add_flag(clock_ampm_lbl, LV_OBJ_FLAG_HIDDEN);
        } else {
            const char *ap = (h < 12) ? "AM" : "PM";
            lv_label_set_text(clock_ampm_lbl, ap);
            lv_obj_clear_flag(clock_ampm_lbl, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align_to(clock_ampm_lbl,
                seg7_get_obj(clock_time_seg7),
                LV_ALIGN_OUT_RIGHT_MID, 4, 0);
        }
    }

    int half = center_xy;
    int len_hour = (int)(saved_diameter * 0.28);
    int len_min  = (int)(saved_diameter * 0.40);
    int len_sec  = (int)(saved_diameter * 0.43);

    /* All points use (half, half) as the centre of the dial */

    /* Second hand */
    pts_sec[0] = (lv_point_t){half, half};
    hand_endpoint((float)s, len_sec, &pts_sec[1]);
    pts_sec[1].x += half;
    pts_sec[1].y += half;
    lv_line_set_points(line_sec, pts_sec, 2);

    /* Minute hand */
    pts_min[0] = (lv_point_t){half, half};
    hand_endpoint((float)m, len_min, &pts_min[1]);
    pts_min[1].x += half;
    pts_min[1].y += half;
    lv_line_set_points(line_min, pts_min, 2);

    /* Hour hand */
    float hour_val = (float)((h % 12) * 5) + (float)m / 12.0f;
    pts_hour[0] = (lv_point_t){half, half};
    hand_endpoint(hour_val, len_hour, &pts_hour[1]);
    pts_hour[1].x += half;
    pts_hour[1].y += half;
    lv_line_set_points(line_hour, pts_hour, 2);
}

void clock_face_set_battery(int percent, bool charging) {
    if (!batt_arc) return;
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    static int prev_percent = -1;
    static bool prev_charging = false;
    if (percent == prev_percent && charging == prev_charging) return;
    prev_percent = percent;
    prev_charging = charging;

    lv_color_t arc_col = charging ? lv_color_make(0, 200, 60) : COL_BATT;

    lv_arc_set_value(batt_arc, percent);
    lv_obj_set_style_arc_color(batt_arc, arc_col, LV_PART_INDICATOR);

    /* Update text label */
    if (batt_pct_label) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", percent);
        lv_label_set_text(batt_pct_label, buf);
    }

    /* Update icon colour and symbol */
    if (batt_icon_label) {
        if (charging) {
            lv_obj_set_style_text_color(batt_icon_label, lv_color_make(0, 200, 60), 0);
            lv_label_set_text(batt_icon_label, LV_SYMBOL_CHARGE);
        } else {
            if (percent <= 15)
                lv_obj_set_style_text_color(batt_icon_label, lv_color_make(220, 40, 40), 0);
            else if (percent <= 40)
                lv_obj_set_style_text_color(batt_icon_label, lv_color_make(220, 180, 40), 0);
            else
                lv_obj_set_style_text_color(batt_icon_label, COL_BATT, 0);

            lv_label_set_text(batt_icon_label,
                percent > 80 ? LV_SYMBOL_BATTERY_FULL :
                percent > 60 ? LV_SYMBOL_BATTERY_3 :
                percent > 40 ? LV_SYMBOL_BATTERY_2 :
                percent > 15 ? LV_SYMBOL_BATTERY_1 :
                               LV_SYMBOL_BATTERY_EMPTY);
        }
    }
}

void clock_face_set_date(int day, int month) {
    if (!date_label) return;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", day);
    lv_label_set_text(date_label, buf);
}

void clock_face_set_datetime(int year, int month, int day, int hour, int min) {
    /* Update the date window */
    clock_face_set_date(day, month);

    /* Update orrery planet positions */
    if (!planet_dots[0] || !orrery_on) return;

    double jd = orrery_jdn(year, month, day)
              + ((double)hour + (double)min / 60.0 - 12.0) / 24.0;
    double d  = jd - 2451545.0;   /* days since J2000.0 */

    int earth_px = center_xy, earth_py = center_xy;

    for (int i = 0; i < NUM_PLANETS; i++) {
        float L = pl_def[i].L0 + pl_def[i].n * (float)d;
        L = fmodf(L, 360.0f);
        if (L < 0.0f) L += 360.0f;

        float angle = L * ((float)M_PI / 180.0f);
        int sz = pl_def[i].dot_sz;
        int px = center_xy + (int)(sinf(angle) * pl_orbit_r[i]) - sz / 2;
        int py = center_xy - (int)(cosf(angle) * pl_orbit_r[i]) - sz / 2;
        lv_obj_set_pos(planet_dots[i], px, py);

        /* Update planet-to-sun line */
        pl_line_pts[i][0] = (lv_point_t){center_xy, center_xy};
        pl_line_pts[i][1] = (lv_point_t){px + sz / 2, py + sz / 2};
        lv_line_set_points(planet_lines[i], pl_line_pts[i], 2);

        if (i == 2) {          /* Earth – remember centre for moon */
            earth_px = px + sz / 2;
            earth_py = py + sz / 2;
        }
    }

    /* Moon position (orbiting Earth) */
    float moon_L = 218.316f + 13.176396f * (float)d;
    moon_L = fmodf(moon_L, 360.0f);
    if (moon_L < 0.0f) moon_L += 360.0f;
    float moon_a = moon_L * ((float)M_PI / 180.0f);
    int moon_r = (int)(pl_orbit_r[2] * 0.22f);
    if (moon_r < 8) moon_r = 8;
    int mx = earth_px + (int)(sinf(moon_a) * moon_r) - MOON_SZ / 2;
    int my = earth_py - (int)(cosf(moon_a) * moon_r) - MOON_SZ / 2;
    lv_obj_set_pos(moon_dot, mx, my);

    /* Moon-phase shape (synodic) */
    float sun_L = pl_def[2].L0 + pl_def[2].n * (float)d + 180.0f;
    float phase = fmodf(moon_L - sun_L, 360.0f);
    if (phase < 0.0f) phase += 360.0f;
    draw_moon_phase(phase);
}

void clock_face_set_steps(uint32_t steps) {
    if (!step_label) return;
    char buf[12];
    if (steps >= 100000)
        snprintf(buf, sizeof(buf), "%uk", (unsigned)(steps / 1000));
    else
        snprintf(buf, sizeof(buf), "%u", (unsigned)steps);
    lv_label_set_text(step_label, buf);
}

void clock_face_set_menu_callbacks(cf_wifi_toggle_cb_t wifi_cb,
                                   cf_bt_toggle_cb_t bt_cb,
                                   cf_reset_steps_cb_t reset_cb) {
    cb_wifi_toggle = wifi_cb;
    cb_bt_toggle   = bt_cb;
    cb_reset_steps = reset_cb;
}

void clock_face_set_bt_switch_state(bool on) {
    if (!bt_sw) return;
    if (on)
        lv_obj_add_state(bt_sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(bt_sw, LV_STATE_CHECKED);
}

void clock_face_set_wifi_state(bool connected) {
    if (!wifi_sw) return;
    if (connected)
        lv_obj_add_state(wifi_sw, LV_STATE_CHECKED);
    else
        lv_obj_clear_state(wifi_sw, LV_STATE_CHECKED);
}

void clock_face_set_screen_power_cb(cf_screen_power_cb_t cb) {
    cb_screen_power = cb;
}

int clock_face_get_timeout_idx(void) {
    return timeout_idx;
}

void clock_face_set_timeout_idx(int idx) {
    if (idx < 0 || idx > 3) idx = 3;
    timeout_idx = idx;
    _update_timeout_label();
    last_activity_tick = lv_tick_get();
}

void clock_face_set_settings_changed_cb(cf_settings_changed_cb_t cb) {
    cb_settings_changed = cb;
}

static void _set_indev_enabled(bool enabled) {
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_indev_enable(indev, enabled);
        }
        indev = lv_indev_get_next(indev);
    }
}

bool clock_face_process_activity(void) {
    uint32_t now_tick = lv_tick_get();

    /* Detect touch/press via LVGL indev */
    bool pressed = false;
    lv_indev_t *indev = lv_indev_get_next(NULL);
    while (indev) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            pressed = (indev->proc.state == LV_INDEV_STATE_PR);
            break;
        }
        indev = lv_indev_get_next(indev);
    }

    if (!screen_on) {
        /* Show touch-blocking overlay if not already visible */
        if (sleep_overlay) lv_obj_clear_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
        return false;  /* screen is off – wake only via button */
    }

    /* Screen is on – any touch resets the activity timer */
    if (pressed) {
        last_activity_tick = now_tick;
    }

    /* Check timeout */
    int timeout_sec = timeout_options[timeout_idx];
    if (timeout_sec > 0) {
        uint32_t elapsed = now_tick - last_activity_tick;
        if (elapsed >= (uint32_t)timeout_sec * 1000) {
            screen_on = false;
            _set_indev_enabled(false);
            if (sleep_overlay) lv_obj_clear_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
            if (cb_screen_power) cb_screen_power(false);
            return false;
        }
    }

    return true;  /* screen is on */
}

/* ── Notification popup ──────────────────────────────────────── */

static void _notif_timer_cb(lv_timer_t *t) {
    clock_face_dismiss_notification();
}

void clock_face_show_notification(const char *title, const char *body,
                                  const char *source) {
    printf("clock_face_show_notification: panel=%p title=%s body=%s src=%s\n",
           (void*)notif_panel, title ? title : "(null)",
           body ? body : "(null)", source ? source : "(null)");
    if (!notif_panel) { printf("  notif_panel is NULL, returning\n"); return; }
    if (notif_timer) {
        lv_timer_del(notif_timer);
        notif_timer = NULL;
    }
    lv_label_set_text(notif_src_lbl, source ? source : "");
    lv_label_set_text(notif_title_lbl, title ? title : "");
    lv_label_set_text(notif_body_lbl, body ? body : "");
    lv_obj_clear_flag(notif_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(notif_panel);  /* ensure on top of everything */
    /* Auto-dismiss after 8 seconds */
    notif_timer = lv_timer_create(_notif_timer_cb, 8000, NULL);
    lv_timer_set_repeat_count(notif_timer, 1);

    /* Wake screen if it was off */
    if (!screen_on) {
        screen_on = true;
        last_activity_tick = lv_tick_get();
        if (sleep_overlay) lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
        if (cb_screen_power) cb_screen_power(true);
        lv_obj_invalidate(lv_scr_act());
    }
    last_activity_tick = lv_tick_get();
}

void clock_face_dismiss_notification(void) {
    if (notif_panel)
        lv_obj_add_flag(notif_panel, LV_OBJ_FLAG_HIDDEN);
    if (notif_timer) {
        lv_timer_del(notif_timer);
        notif_timer = NULL;
    }
}

void clock_face_wake(void) {
    if (screen_on) {
        /* Screen is on – turn it off */
        screen_on = false;
        _set_indev_enabled(false);
        if (sleep_overlay) lv_obj_clear_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
        if (cb_screen_power) cb_screen_power(false);
    } else {
        /* Screen is off – wake up */
        screen_on = true;
        _set_indev_enabled(true);
        last_activity_tick = lv_tick_get();
        if (sleep_overlay) lv_obj_add_flag(sleep_overlay, LV_OBJ_FLAG_HIDDEN);
        if (cb_screen_power) cb_screen_power(true);
        lv_obj_invalidate(lv_scr_act());
    }
}

void clock_face_set_bt_state(bool connected) {
    if (!bt_icon_label) return;
    if (connected) {
        lv_obj_set_style_text_color(bt_icon_label,
            lv_color_make(60, 140, 255), 0);
    } else {
        lv_obj_set_style_text_color(bt_icon_label,
            lv_color_make(80, 80, 90), 0);
    }
}

bool clock_face_is_menu_open(void) {
    return menu_open;
}

bool clock_face_get_24h(void) {
    return time_24h;
}

void clock_face_set_24h(bool is24h) {
    time_24h = is24h;
    _update_fmt_label();
}

bool clock_face_get_orrery(void) {
    return orrery_on;
}

void clock_face_set_orrery(bool on) {
    orrery_on = on;
    _update_orrery_label();
    _orrery_set_visible(on);
}

bool clock_face_get_metric(void) {
    return use_metric;
}

void clock_face_set_metric(bool metric) {
    use_metric = metric;
    _update_unit_label();
}
