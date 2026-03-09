/*
 *  stopwatch.c – Stopwatch screen (pure LVGL)
 *
 *  Features: Start/Pause, Reset, MM:SS.ss display.
 *  Uses lv_tick_get() for timing so runs independently
 *  of which tile is visible.
 */

#include "stopwatch.h"
#include "seg7.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdbool.h>

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_BG      lv_color_make(10, 10, 15)
#define COL_TEXT    lv_color_white()
#define COL_DIM     lv_color_make(100, 100, 110)
#define COL_GREEN   lv_color_make(40, 160, 80)
#define COL_AMBER   lv_color_make(200, 160, 40)
#define COL_GREY    lv_color_make(55, 55, 70)

/* ── State ─────────────────────────────────────────────────────── */
static seg7_display_t *seg7_disp = NULL;
static lv_obj_t *start_btn  = NULL;
static lv_obj_t *start_lbl  = NULL;

static bool     running        = false;
static uint32_t elapsed_ms     = 0;   /* accumulated while paused */
static uint32_t run_start_tick = 0;   /* tick when last started   */

static void (*settings_changed_cb)(void) = NULL;

/* ── Helpers ───────────────────────────────────────────────────── */
static void update_display(void) {
    uint32_t total = elapsed_ms;
    if (running) total += lv_tick_get() - run_start_tick;

    uint32_t cs = (total / 10) % 100;
    uint32_t s  = (total / 1000) % 60;
    uint32_t m  = total / 60000;

    char buf[16];
    if (m >= 60) {
        uint32_t h = m / 60;
        m %= 60;
        snprintf(buf, sizeof(buf), "%u:%02u:%02u.%02u",
                 (unsigned)h, (unsigned)m, (unsigned)s, (unsigned)cs);
    } else {
        snprintf(buf, sizeof(buf), "%02u:%02u.%02u",
                 (unsigned)m, (unsigned)s, (unsigned)cs);
    }

    seg7_set_text(seg7_disp, buf);
}

/* ── Callbacks ─────────────────────────────────────────────────── */
static void _start_cb(lv_event_t *e) {
    (void)e;
    if (running) {
        /* Pause */
        elapsed_ms += lv_tick_get() - run_start_tick;
        running = false;
        lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  Resume");
        lv_obj_set_style_bg_color(start_btn, COL_GREEN, 0);
        if (settings_changed_cb) settings_changed_cb();
    } else {
        /* Start / Resume */
        run_start_tick = lv_tick_get();
        running = true;
        lv_label_set_text(start_lbl, LV_SYMBOL_PAUSE "  Pause");
        lv_obj_set_style_bg_color(start_btn, COL_AMBER, 0);
    }
}

static void _long_press_cb(lv_event_t *e) {
    (void)e;
    /* Long-press resets only when paused (has elapsed time) */
    if (!running && elapsed_ms > 0) {
        elapsed_ms = 0;
        run_start_tick = 0;
        lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  Start");
        lv_obj_set_style_bg_color(start_btn, COL_GREEN, 0);
        update_display();
        if (settings_changed_cb) settings_changed_cb();
    }
}

/* ── Create ────────────────────────────────────────────────────── */
void stopwatch_create(lv_obj_t *parent, int diameter) {
    (void)diameter;
    lv_obj_set_style_bg_color(parent, COL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "STOPWATCH");
    lv_obj_set_style_text_color(title, COL_DIM, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* Time display — seg7 widget */
    seg7_disp = seg7_create(parent, 11, 36, COL_TEXT, 4);
    lv_obj_align(seg7_get_obj(seg7_disp), LV_ALIGN_CENTER, 0, -30);
    seg7_set_text(seg7_disp, "00:00.00");

    /* Start / Pause button (centered, long-press to reset) */
    start_btn = lv_btn_create(parent);
    lv_obj_set_size(start_btn, 200, 52);
    lv_obj_align(start_btn, LV_ALIGN_CENTER, 0, 50);
    lv_obj_set_style_bg_color(start_btn, COL_GREEN, 0);
    lv_obj_set_style_radius(start_btn, 14, 0);
    lv_obj_set_style_shadow_width(start_btn, 12, 0);
    lv_obj_set_style_shadow_color(start_btn, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(start_btn, LV_OPA_40, 0);
    start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  Start");
    lv_obj_set_style_text_color(start_lbl, lv_color_white(), 0);
    lv_obj_center(start_lbl);
    lv_obj_add_event_cb(start_btn, _start_cb, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_add_event_cb(start_btn, _long_press_cb, LV_EVENT_LONG_PRESSED, NULL);
}

/* ── Loop ──────────────────────────────────────────────────────── */
void stopwatch_loop(void) {
    if (!running) return;
    static uint32_t last_upd = 0;
    uint32_t now = lv_tick_get();
    if (now - last_upd < 33) return;   /* ~30 Hz */
    last_upd = now;
    update_display();
}

/* ── Persistence API ──────────────────────────────────────────── */
uint32_t stopwatch_get_elapsed_ms(void) {
    uint32_t total = elapsed_ms;
    if (running) total += lv_tick_get() - run_start_tick;
    return total;
}

void stopwatch_set_elapsed_ms(uint32_t ms) {
    running = false;
    elapsed_ms = ms;
    run_start_tick = 0;
    if (start_lbl) {
        if (ms > 0) {
            lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  Resume");
        } else {
            lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  Start");
        }
        lv_obj_set_style_bg_color(start_btn, COL_GREEN, 0);
    }
    if (seg7_disp) update_display();
}

void stopwatch_set_changed_cb(void (*cb)(void)) {
    settings_changed_cb = cb;
}
