/*
 *  timer_screen.c – Countdown timer screen (pure LVGL)
 *
 *  Features: Set minutes/seconds, Start/Pause, Reset.
 *  Plays a ding sound when the countdown reaches zero.
 */

#include "timer_screen.h"
#include "seg7.h"
#include "sound.h"
#include <lvgl.h>
#include <stdio.h>
#include <stdbool.h>

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_BG      lv_color_make(10, 10, 15)
#define COL_TEXT    lv_color_white()
#define COL_DIM     lv_color_make(100, 100, 110)
#define COL_BLUE    lv_color_make(60, 130, 230)
#define COL_AMBER   lv_color_make(200, 160, 40)
#define COL_RED     lv_color_make(220, 50, 50)
#define COL_GREY    lv_color_make(55, 55, 70)
#define COL_BTN_DK  lv_color_make(40, 40, 55)

/* ── State ─────────────────────────────────────────────────────── */
static seg7_display_t *seg7_disp = NULL;
static lv_obj_t *start_btn  = NULL;
static lv_obj_t *start_lbl  = NULL;
static lv_obj_t *dismiss_btn = NULL;

static int      set_hr   = 0;
static int      set_min  = 5;
static int      set_sec  = 0;
static bool     running  = false;
static bool     finished = false;
static uint32_t remaining_ms       = 0;
static uint32_t run_start_tick     = 0;
static uint32_t run_start_remaining = 0;

/* Settings-changed callback (for persistence) */
typedef void (*timer_changed_cb_t)(void);
static timer_changed_cb_t timer_changed_cb = NULL;

/* ── Ringing state (same pattern as alarm) ─────────────────────── */
static lv_timer_t *ring_timer = NULL;
static int ring_count = 0;
#define RING_REPEATS 30   /* max dings (30 s @ 1 s interval) */

static void stop_ringing(void);

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
    finished = true;
    ring_count = 0;
    sound_play_ding();
    if (ring_timer) lv_timer_del(ring_timer);
    ring_timer = lv_timer_create(_ring_cb, 1000, NULL);
    if (dismiss_btn) lv_obj_clear_flag(dismiss_btn, LV_OBJ_FLAG_HIDDEN);
}

static void stop_ringing(void) {
    ring_count = 0;
    if (ring_timer) {
        lv_timer_del(ring_timer);
        ring_timer = NULL;
    }
    if (dismiss_btn) lv_obj_add_flag(dismiss_btn, LV_OBJ_FLAG_HIDDEN);
}

static void _dismiss_cb(lv_event_t *e) {
    (void)e;
    stop_ringing();
}

/* ── Helpers ───────────────────────────────────────────────────── */
static void update_display(void) {
    uint32_t total = remaining_ms;

    if (running) {
        uint32_t elapsed = lv_tick_get() - run_start_tick;
        if (elapsed >= run_start_remaining) {
            total = 0;
            remaining_ms = 0;
            running = false;
            finished = true;
            lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  Start");
            lv_obj_set_style_bg_color(start_btn, COL_BLUE, 0);
            start_ringing();
        } else {
            total = run_start_remaining - elapsed;
        }
    }

    char buf[16];
    if (!running && !finished) {
        /* Set mode — show HH:MM:SS */
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", set_hr, set_min, set_sec);
        seg7_set_color(seg7_disp, COL_TEXT);
    } else {
        /* Countdown mode — H:MM:SS.D or MM:SS.D */
        uint32_t s  = (total / 1000) % 60;
        uint32_t m  = (total / 60000) % 60;
        uint32_t h  = total / 3600000;
        uint32_t ds = (total / 100) % 10;

        if (h > 0) {
            snprintf(buf, sizeof(buf), "%u:%02u:%02u.%u",
                     (unsigned)h, (unsigned)m, (unsigned)s, (unsigned)ds);
        } else {
            snprintf(buf, sizeof(buf), "%02u:%02u.%u",
                     (unsigned)m, (unsigned)s, (unsigned)ds);
        }

        seg7_set_color(seg7_disp, finished ? COL_RED : COL_TEXT);
    }

    seg7_set_text(seg7_disp, buf);
    lv_obj_align(seg7_get_obj(seg7_disp), LV_ALIGN_CENTER, 0, -50);
}

/* ── Callbacks ─────────────────────────────────────────────────── */
static void _hr_plus_cb(lv_event_t *e) {
    (void)e;
    if (!running) {
        set_hr = (set_hr + 1) % 24;
        finished = false;
        update_display();
        if (timer_changed_cb) timer_changed_cb();
    }
}
static void _hr_minus_cb(lv_event_t *e) {
    (void)e;
    if (!running) {
        set_hr = (set_hr + 23) % 24;
        finished = false;
        update_display();
        if (timer_changed_cb) timer_changed_cb();
    }
}
static void _min_plus_cb(lv_event_t *e) {
    (void)e;
    if (!running) {
        set_min = (set_min + 1) % 60;
        finished = false;
        update_display();
        if (timer_changed_cb) timer_changed_cb();
    }
}
static void _min_minus_cb(lv_event_t *e) {
    (void)e;
    if (!running) {
        set_min = (set_min + 59) % 60;
        finished = false;
        update_display();
        if (timer_changed_cb) timer_changed_cb();
    }
}
static void _sec_plus_cb(lv_event_t *e) {
    (void)e;
    if (!running) {
        set_sec = (set_sec + 1) % 60;
        finished = false;
        update_display();
        if (timer_changed_cb) timer_changed_cb();
    }
}
static void _sec_minus_cb(lv_event_t *e) {
    (void)e;
    if (!running) {
        set_sec = (set_sec + 59) % 60;
        finished = false;
        update_display();
        if (timer_changed_cb) timer_changed_cb();
    }
}

static void _start_cb(lv_event_t *e) {
    (void)e;
    if (running) {
        /* Pause */
        uint32_t elapsed = lv_tick_get() - run_start_tick;
        remaining_ms = (elapsed >= run_start_remaining)
                     ? 0 : run_start_remaining - elapsed;
        running = false;
        lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  Resume");
        lv_obj_set_style_bg_color(start_btn, COL_BLUE, 0);
        if (timer_changed_cb) timer_changed_cb();
    } else {
        /* Start / Resume */
        if (finished) {
            finished = false;
            remaining_ms = (uint32_t)set_hr  * 3600000
                         + (uint32_t)set_min * 60000
                         + (uint32_t)set_sec * 1000;
        }
        if (remaining_ms == 0) {
            remaining_ms = (uint32_t)set_hr  * 3600000
                         + (uint32_t)set_min * 60000
                         + (uint32_t)set_sec * 1000;
        }
        if (remaining_ms == 0) return;  /* nothing to count */

        run_start_tick = lv_tick_get();
        run_start_remaining = remaining_ms;
        running = true;
        lv_label_set_text(start_lbl, LV_SYMBOL_PAUSE "  Pause");
        lv_obj_set_style_bg_color(start_btn, COL_AMBER, 0);
    }
}

static void _long_press_cb(lv_event_t *e) {
    (void)e;
    /* Long-press resets only when paused (not running, has remaining time) */
    if (!running && (remaining_ms > 0 || finished)) {
        running = false;
        finished = false;
        remaining_ms = 0;
        stop_ringing();
        lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  Start");
        lv_obj_set_style_bg_color(start_btn, COL_BLUE, 0);
        update_display();
        if (timer_changed_cb) timer_changed_cb();
    }
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
void timer_screen_create(lv_obj_t *parent, int diameter) {
    (void)diameter;
    lv_obj_set_style_bg_color(parent, COL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "TIMER");
    lv_obj_set_style_text_color(title, COL_DIM, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* Time display — seg7 widget */
    seg7_disp = seg7_create(parent, 11, 30, COL_TEXT, 4);
    lv_obj_align(seg7_get_obj(seg7_disp), LV_ALIGN_CENTER, 0, -50);
    seg7_set_text(seg7_disp, "00:05:00");

    /* HR / MIN / SEC labels */
    lv_obj_t *hr_lbl = lv_label_create(parent);
    lv_label_set_text(hr_lbl, "HR");
    lv_obj_set_style_text_color(hr_lbl, COL_DIM, 0);
    lv_obj_set_style_text_font(hr_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(hr_lbl, LV_ALIGN_CENTER, -100, -10);

    lv_obj_t *min_lbl = lv_label_create(parent);
    lv_label_set_text(min_lbl, "MIN");
    lv_obj_set_style_text_color(min_lbl, COL_DIM, 0);
    lv_obj_set_style_text_font(min_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(min_lbl, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *sec_lbl = lv_label_create(parent);
    lv_label_set_text(sec_lbl, "SEC");
    lv_obj_set_style_text_color(sec_lbl, COL_DIM, 0);
    lv_obj_set_style_text_font(sec_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(sec_lbl, LV_ALIGN_CENTER, 100, -10);

    /* Adjustment buttons */
    make_adj_btn(parent, LV_SYMBOL_MINUS, LV_ALIGN_CENTER, -125, 25, _hr_minus_cb);
    make_adj_btn(parent, LV_SYMBOL_PLUS,  LV_ALIGN_CENTER,  -75, 25, _hr_plus_cb);
    make_adj_btn(parent, LV_SYMBOL_MINUS, LV_ALIGN_CENTER,  -25, 25, _min_minus_cb);
    make_adj_btn(parent, LV_SYMBOL_PLUS,  LV_ALIGN_CENTER,   25, 25, _min_plus_cb);
    make_adj_btn(parent, LV_SYMBOL_MINUS, LV_ALIGN_CENTER,   75, 25, _sec_minus_cb);
    make_adj_btn(parent, LV_SYMBOL_PLUS,  LV_ALIGN_CENTER,  125, 25, _sec_plus_cb);

    /* Start / Pause / Reset button (centered, long-press to reset) */
    start_btn = lv_btn_create(parent);
    lv_obj_set_size(start_btn, 200, 52);
    lv_obj_align(start_btn, LV_ALIGN_CENTER, 0, 100);
    lv_obj_set_style_bg_color(start_btn, COL_BLUE, 0);
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
    lv_label_set_text(src_lbl, "TIMER");
    lv_obj_set_style_text_font(src_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(src_lbl, lv_color_make(255, 200, 200), 0);
    lv_obj_align(src_lbl, LV_ALIGN_CENTER, 0, 22);
    lv_obj_add_event_cb(dismiss_btn, _dismiss_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(dismiss_btn, LV_OBJ_FLAG_HIDDEN);
}

/* ── Loop ──────────────────────────────────────────────────────── */
void timer_screen_loop(void) {
    if (!running) return;
    static uint32_t last_upd = 0;
    uint32_t now = lv_tick_get();
    if (now - last_upd < 33) return;   /* ~30 Hz */
    last_upd = now;
    update_display();
}

/* ── Persistence API ──────────────────────────────────────────── */
int timer_screen_get_set_hr(void)  { return set_hr; }
int timer_screen_get_set_min(void) { return set_min; }
int timer_screen_get_set_sec(void) { return set_sec; }

void timer_screen_set_preset(int hr, int min, int sec) {
    set_hr  = hr  % 24;
    set_min = min % 60;
    set_sec = sec % 60;
    if (seg7_disp && !running && !finished) update_display();
}

uint32_t timer_screen_get_remaining_ms(void) {
    return remaining_ms;
}

void timer_screen_set_remaining_ms(uint32_t ms) {
    remaining_ms = ms;
    if (ms > 0 && !running && !finished && seg7_disp) {
        /* Show paused state so user can resume */
        lv_label_set_text(start_lbl, LV_SYMBOL_PLAY "  Resume");
        lv_obj_set_style_bg_color(start_btn, COL_BLUE, 0);
        update_display();
    }
}

void timer_screen_set_changed_cb(void (*cb)(void)) {
    timer_changed_cb = cb;
}
