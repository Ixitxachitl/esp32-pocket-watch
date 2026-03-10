/*
 *  screen_manager.c – Custom horizontal-swipe screen management
 *
 *  Manages four full-screen panels arranged horizontally:
 *    0 = Clock   1 = Stopwatch   2 = Timer   3 = Alarm
 *
 *  Uses manual touch tracking + lv_anim_t for smooth swiping with
 *  seamless wrap-around (Alarm ↔ Clock).  No lv_tileview.
 *
 *  Each non-clock panel gets a "watch frame" meter: the same concentric
 *  gradient, outer rings, and battery arc that the clock face has.
 *  Page-indicator dots are drawn on top.
 */

#include "screen_manager.h"
#include "clock_face.h"
#include "stopwatch.h"
#include "timer_screen.h"
#include "alarm_screen.h"
#include "weather_screen.h"
#include "radar_screen.h"
#include "compass_screen.h"
#include "media_screen.h"
#include "level_screen.h"
#include "sysinfo_screen.h"
#include "nav_screen.h"
#include "seg7.h"
#include <lvgl.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

/* ── Geometry ──────────────────────────────────────────────────── */
#define DOT_SIZE   8
#define DOT_GAP    12
#define DOT_BOTTOM 24

#define SWIPE_THRESHOLD  60   /* px drag to trigger page change       */
#define ANIM_DURATION    200  /* ms for slide animation               */
#define DRAG_MIN_DELTA    3   /* min px change to re-layout           */
#define FLICK_VEL_THRESH 300  /* px/sec to trigger velocity-based swipe */
#define FLICK_MIN_DIST    15  /* min px movement for velocity swipe    */
#define VEL_WINDOW_MS     80  /* velocity sampling window (ms)         */

/* ── Colours (match clock_face.c) ──────────────────────────────── */
#define COL_BG          lv_color_make(10, 10, 15)
#define COL_RING_OUTER  lv_color_make(60, 60, 65)
#define COL_RING_INNER  lv_color_make(35, 35, 40)
#define COL_BATT        lv_color_make(0, 140, 255)

/* ── Dot colours ───────────────────────────────────────────────── */
#define COL_DOT_ON    lv_color_white()
#define COL_DOT_OFF   lv_color_make(60, 60, 70)

/* ── Internal state ────────────────────────────────────────────── */
static lv_obj_t *container     = NULL;   /* clipping parent         */
static lv_obj_t *tiles[SCR_COUNT] = {NULL};
static lv_obj_t *dots[SCR_COUNT]  = {NULL};
static int       scr_w         = 466;    /* screen width            */

/* ── Virtual screen order ─────────────────────────────────────── */
/* cur_virt is the current index into active_order[]. */
static int       active_order[SCR_COUNT] = {0,1,2,3,4,5,6,7,8,9,10};
static int       active_count = SCR_COUNT;
static int       cur_virt     = 0;
static lv_obj_t *dot_parent   = NULL;  /* parent for dots, saved at init */

static const char *screen_names[SCR_COUNT] = {
    "Clock", "Stopwatch", "Timer", "Alarm",
    "Weather", "Radar", "Compass", "Media",
    "Level", "System", "Navigation"
};

static int _phys_to_virt(int phys) {
    for (int i = 0; i < active_count; i++)
        if (active_order[i] == phys) return i;
    return -1;
}

/* Watch-frame meters + battery arcs for non-clock screens */
static lv_obj_t              *frame_meters[SCR_COUNT] = {NULL};
/* Snapshot of the watch frame (shared across all non-clock tiles) */
static lv_img_dsc_t   frame_snap_dsc;
static void          *frame_snap_buf = NULL;
/* Lightweight battery arcs (one per non-clock tile) */
static lv_obj_t      *frame_batt_arc[SCR_COUNT] = {NULL};

/* Time header seg7 displays (one per non-clock tile) */
static seg7_display_t *time_headers[SCR_COUNT] = {NULL};
static lv_obj_t       *time_ampm_lbls[SCR_COUNT] = {NULL};

/* Touch-tracking state */
static bool      dragging      = false;
static bool      animating     = false;
static bool      drag_locked_v = false;  /* vertical drag detected → ignore */
static bool      drag_locked_h = false;  /* confirmed horizontal drag       */
static lv_coord_t drag_start_x = 0;
static lv_coord_t drag_start_y = 0;
static lv_coord_t drag_offset  = 0;  /* current drag delta (px)   */
static bool      was_pressed   = false;  /* previous frame press state */
#define DRAG_LOCK_RATIO 1  /* if |dy| > |dx| at lock point, lock vertical */

/* Velocity tracking for flick detection */
static lv_coord_t vel_ref_x    = 0;
static uint32_t   vel_ref_time = 0;
static int        anim_vel_hint = 0;   /* px/sec at release, for adaptive duration */

/* ── Gradient band data (same as clock_face.c) ─────────────────── */
typedef struct { int r_mod; uint8_t r, g, b; } grad_t;
static const grad_t grad_bands[] = {
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
#define N_GRAD_BANDS ((int)(sizeof(grad_bands)/sizeof(grad_bands[0])))

/* ── Helper: wrapping index arithmetic ─────────────────────────── */
static int wrap_idx(int i) {
    return ((i % active_count) + active_count) % active_count;
}

/* Track currently visible tiles to avoid redundant lv_obj_set_pos / flag changes */
static int vis_cur  = -1;
static int vis_prev = -1;
static int vis_next = -1;

/* Forward-declare so _layout_tiles can reference it for 2-screen case */
static lv_coord_t anim_start_off;

/* ── Position tiles around cur_virt + drag_offset ──────────────── */
/*  Only the current tile and its two neighbours are visible.
    Everything else is hidden so LVGL skips all widget processing. */
static void _layout_tiles(void) {
    int virt_prev = wrap_idx(cur_virt - 1);
    int virt_next = wrap_idx(cur_virt + 1);

    int phys_cur  = active_order[cur_virt];
    int phys_prev = active_order[virt_prev];
    int phys_next = active_order[virt_next];

    /* If visible set changed, update hidden/shown flags */
    if (phys_cur != vis_cur || phys_prev != vis_prev || phys_next != vis_next) {
        for (int i = 0; i < SCR_COUNT; i++) {
            if (!tiles[i]) continue;
            if (i == phys_cur || i == phys_prev || i == phys_next) {
                lv_obj_clear_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
            } else if (i == vis_cur || i == vis_prev || i == vis_next ||
                       vis_cur < 0) {
                /* Only hide tiles that were previously visible (or first call) */
                lv_obj_add_flag(tiles[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(tiles[i], scr_w * 3, 0);
            }
        }
        vis_cur  = phys_cur;
        vis_prev = phys_prev;
        vis_next = phys_next;
    }

    /* Update positions of the visible tiles */
    lv_obj_set_pos(tiles[phys_cur], drag_offset, 0);
    if (phys_prev == phys_next) {
        /* Only 2 screens: place the other tile on the side the user is
           dragging toward so both swipe directions work.  During
           animation cur_virt is already set to the target, so the
           tile that *was* current is now prev; place it opposite the
           travel direction (anim goes toward 0 from anim_start_off). */
        if (animating)
            lv_obj_set_pos(tiles[phys_prev],
                           drag_offset + (anim_start_off > 0 ? -scr_w : scr_w), 0);
        else
            lv_obj_set_pos(tiles[phys_prev],
                           drag_offset + (drag_offset >= 0 ? -scr_w : scr_w), 0);
    } else {
        lv_obj_set_pos(tiles[phys_prev], drag_offset - scr_w, 0);
        lv_obj_set_pos(tiles[phys_next], drag_offset + scr_w, 0);
    }
}

/* ── Animation callback: animate drag_offset → 0 ──────────────── */
static int       anim_target     = 0;

static void _anim_exec(void *var, int32_t val) {
    (void)var;
    drag_offset = (lv_coord_t)val;
    _layout_tiles();
}

static void _anim_ready(lv_anim_t *a) {
    (void)a;
    drag_offset = 0;
    cur_virt = anim_target;
    animating = false;
    _layout_tiles();
    screen_manager_update_indicators();
}

static void _animate_to(int target_virt, lv_coord_t current_offset) {
    anim_target = target_virt;
    animating = true;

    if (target_virt != cur_virt) {
        int old = cur_virt;
        cur_virt = target_virt;
        /* Translate offset into the new tile's frame of reference.
           With only 2 screens wrap_idx(old+1)==wrap_idx(old-1), so
           use the actual drag direction (offset sign) instead. */
        bool swiped_left;
        if (active_count == 2)
            swiped_left = (current_offset < 0);
        else
            swiped_left = (target_virt == wrap_idx(old + 1));

        if (swiped_left) {
            /* swiped left → target was to the right */
            anim_start_off = current_offset + scr_w;
        } else {
            /* swiped right → target was to the left */
            anim_start_off = current_offset - scr_w;
        }
    } else {
        anim_start_off = current_offset;
    }

    drag_offset = anim_start_off;
    _layout_tiles();

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, NULL);
    lv_anim_set_exec_cb(&a, _anim_exec);
    lv_anim_set_values(&a, anim_start_off, 0);
    /* Adapt duration: faster flick → shorter settle animation */
    int dur = ANIM_DURATION;
    int abs_vel = anim_vel_hint < 0 ? -anim_vel_hint : anim_vel_hint;
    if (abs_vel > 600)      dur = 120;
    else if (abs_vel > 400) dur = 150;
    lv_anim_set_time(&a, dur);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&a, _anim_ready);
    lv_anim_start(&a);
}

/* ── Swipe tracking (called from screen_manager_loop) ──────────── */
/* Polls the first input device directly – no touch overlay needed,
   so all child widgets (buttons, switches) keep working normally. */
static void _poll_swipe(void) {
    if (animating) return;
    if (clock_face_is_menu_open()) { dragging = false; return; }

    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    bool pressed = (indev->proc.state == LV_INDEV_STATE_PRESSED);

    if (pressed && !was_pressed) {
        /* Finger just went down */
        dragging = true;
        drag_locked_v = false;
        drag_locked_h = false;
        drag_start_x = pt.x;
        drag_start_y = pt.y;
        drag_offset = 0;
        vel_ref_x = pt.x;
        vel_ref_time = lv_tick_get();
    }
    else if (pressed && dragging) {
        if (drag_locked_v) goto done;

        lv_coord_t dx = pt.x - drag_start_x;
        lv_coord_t dy = pt.y - drag_start_y;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;

        if (!drag_locked_h) {
            if (adx < 10 && ady < 10) goto done;
            if (ady > adx * DRAG_LOCK_RATIO) {
                drag_locked_v = true;
                goto done;
            }
            drag_locked_h = true;
            /* Cancel any button press LVGL may have started and
               tell it to ignore the rest of this touch cycle.
               Our own raw-state polling is unaffected.            */
            lv_obj_t *pressed_obj = indev->proc.types.pointer.act_obj;
            if (pressed_obj) {
                lv_obj_clear_state(pressed_obj, LV_STATE_PRESSED);
            }
            lv_indev_reset(indev, NULL);
            lv_indev_wait_release(indev);
        }

        if (dx != drag_offset) {
            int diff = dx - drag_offset;
            if (diff < 0) diff = -diff;
            if (diff >= DRAG_MIN_DELTA) {
                drag_offset = dx;
                _layout_tiles();
            }
        }

        /* Update velocity reference periodically */
        {
            uint32_t now_ms = lv_tick_get();
            if (now_ms - vel_ref_time >= VEL_WINDOW_MS) {
                vel_ref_x = pt.x;
                vel_ref_time = now_ms;
            }
        }
    }
    else if (!pressed && was_pressed && dragging) {
        dragging = false;
        if (drag_locked_v || !drag_locked_h) {
            drag_locked_v = false;
            drag_locked_h = false;
            goto done;
        }
        drag_locked_h = false;

        lv_coord_t off = drag_offset;

        /* Calculate release velocity for flick detection */
        uint32_t dt_ms = lv_tick_get() - vel_ref_time;
        if (dt_ms < 1) dt_ms = 1;
        int32_t vel_pps = (int32_t)(pt.x - vel_ref_x) * 1000 / (int32_t)dt_ms;
        anim_vel_hint = (int)vel_pps;

        if (off < -SWIPE_THRESHOLD ||
            (off < -FLICK_MIN_DIST && vel_pps < -FLICK_VEL_THRESH)) {
            _animate_to(wrap_idx(cur_virt + 1), off);
        } else if (off > SWIPE_THRESHOLD ||
                   (off > FLICK_MIN_DIST && vel_pps > FLICK_VEL_THRESH)) {
            _animate_to(wrap_idx(cur_virt - 1), off);
        } else {
            anim_vel_hint = 0;
            _animate_to(cur_virt, off);
        }
    }

done:
    was_pressed = pressed;
}

/* ── Create a decorative watch frame from pre-rendered snapshot ── */
static void _create_watch_frame(lv_obj_t *tile, int idx, int diameter) {
    /* Display the shared snapshot image */
    if (frame_snap_buf) {
        lv_obj_t *img = lv_img_create(tile);
        lv_img_set_src(img, &frame_snap_dsc);
        lv_obj_center(img);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        if (idx >= 0 && idx < SCR_COUNT)
            frame_meters[idx] = img;
    }

    /* Battery arc (lightweight lv_arc widget) */
    if (idx >= 0 && idx < SCR_COUNT) {
        int arc_w = (int)(diameter * 0.025);
        int arc_r_offset = (int)(diameter * 0.005);
        int arc_diam = diameter - arc_r_offset * 2;
        lv_obj_t *ba = lv_arc_create(tile);
        lv_obj_set_size(ba, arc_diam, arc_diam);
        lv_obj_center(ba);
        lv_arc_set_rotation(ba, 270);
        lv_arc_set_range(ba, 0, 100);
        lv_arc_set_value(ba, 0);
        lv_arc_set_bg_angles(ba, 0, 360);
        lv_obj_set_style_arc_width(ba, arc_w, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(ba, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(ba, arc_w, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(ba, COL_BATT, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(ba, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(ba, 0, LV_PART_KNOB);
        lv_obj_clear_flag(ba, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        frame_batt_arc[idx] = ba;
    }
}

/* ── Build the shared frame snapshot once ───────────────────── */
static void _build_frame_snapshot(lv_obj_t *tmp_parent, int diameter) {
    lv_obj_t *m = lv_meter_create(tmp_parent);
    lv_obj_set_size(m, diameter, diameter);
    lv_obj_center(m);

    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_bg_color(m, COL_BG, 0);
    lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(m, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(m, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(m, 30, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(m, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(m, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_bg_color(m, COL_BG, LV_PART_TICKS);
    lv_obj_set_style_width(m, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(m, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, LV_PART_INDICATOR);

    lv_meter_scale_t *scale = lv_meter_add_scale(m);
    lv_meter_set_scale_ticks(m, scale,
        61, 1, (int)(diameter * 0.03), COL_BG);
    lv_meter_set_scale_major_ticks(m, scale,
        5, 1, (int)(diameter * 0.06), COL_BG, (int)(diameter * 0.06));
    lv_meter_set_scale_range(m, scale, 0, 60, 360, 270);
    lv_obj_set_style_text_opa(m, LV_OPA_TRANSP, LV_PART_TICKS);

    /* Outer ring arcs */
    {
        lv_meter_indicator_t *r1 = lv_meter_add_arc(m, scale, 4, COL_RING_OUTER, 0);
        lv_meter_set_indicator_start_value(m, r1, 0);
        lv_meter_set_indicator_end_value(m, r1, 60);
        lv_meter_indicator_t *r2 = lv_meter_add_arc(m, scale, 2, COL_RING_INNER, -4);
        lv_meter_set_indicator_start_value(m, r2, 0);
        lv_meter_set_indicator_end_value(m, r2, 60);
    }

    /* Circular gradient */
    for (int i = 0; i < N_GRAD_BANDS; i++) {
        int width = (i == 0) ? (int)(diameter * 0.08)
                             : (int)(diameter * 0.10);
        lv_meter_indicator_t *g = lv_meter_add_arc(m, scale,
            width,
            lv_color_make(grad_bands[i].r, grad_bands[i].g, grad_bands[i].b),
            grad_bands[i].r_mod);
        lv_meter_set_indicator_start_value(m, g, 0);
        lv_meter_set_indicator_end_value(m, g, 60);
    }

    /* Decorative inner ring */
    lv_meter_indicator_t *ri = lv_meter_add_arc(m, scale,
        2, COL_RING_INNER, -(int)(diameter * 0.12));
    lv_meter_set_indicator_start_value(m, ri, 0);
    lv_meter_set_indicator_end_value(m, ri, 60);

    /* Force layout then snapshot */
    lv_obj_update_layout(m);
    uint32_t snap_size = lv_snapshot_buf_size_needed(m, LV_IMG_CF_TRUE_COLOR);
#ifdef ESP_PLATFORM
    frame_snap_buf = heap_caps_malloc(snap_size, MALLOC_CAP_SPIRAM);
#else
    frame_snap_buf = malloc(snap_size);
#endif
    if (frame_snap_buf) {
        lv_snapshot_take_to_buf(m, LV_IMG_CF_TRUE_COLOR,
                               &frame_snap_dsc, frame_snap_buf, snap_size);
    }
    lv_obj_del(m);
}



/* ── Public API ────────────────────────────────────────────────── */

void screen_manager_init(lv_obj_t *parent, int diameter) {
    if (!parent) parent = lv_scr_act();

    int w = lv_obj_get_width(parent);
    int h = lv_obj_get_height(parent);
    if (w == 0) w = 466;
    if (h == 0) h = 466;
    scr_w = w;

    /* Dark background */
    lv_obj_set_style_bg_color(parent, COL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* ── Clipping container (hides off-screen tiles) ───────── */
    container = lv_obj_create(parent);
    lv_obj_set_size(container, w, h);
    lv_obj_set_pos(container, 0, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_clip_corner(container, true, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Create tile containers ────────────────────────────── */
    for (int i = 0; i < SCR_COUNT; i++) {
        tiles[i] = lv_obj_create(container);
        lv_obj_set_size(tiles[i], w, h);
        lv_obj_set_style_bg_color(tiles[i], COL_BG, 0);
        lv_obj_set_style_bg_opa(tiles[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(tiles[i], 0, 0);
        lv_obj_set_style_pad_all(tiles[i], 0, 0);
        lv_obj_set_style_radius(tiles[i], 0, 0);
        lv_obj_clear_flag(tiles[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    /* ── Build shared frame snapshot (one-time meter render) ── */
    _build_frame_snapshot(tiles[SCR_STOPWATCH], diameter);

    /* ── Watch frames on non-clock tiles ───────────────────── */
    _create_watch_frame(tiles[SCR_STOPWATCH], SCR_STOPWATCH, diameter);
    _create_watch_frame(tiles[SCR_TIMER],     SCR_TIMER,     diameter);
    _create_watch_frame(tiles[SCR_ALARM],     SCR_ALARM,     diameter);
    _create_watch_frame(tiles[SCR_WEATHER],   SCR_WEATHER,   diameter);
    _create_watch_frame(tiles[SCR_RADAR],     SCR_RADAR,     diameter);
    _create_watch_frame(tiles[SCR_COMPASS],   SCR_COMPASS,   diameter);
    _create_watch_frame(tiles[SCR_MEDIA],     SCR_MEDIA,     diameter);
    _create_watch_frame(tiles[SCR_LEVEL],     SCR_LEVEL,     diameter);
    _create_watch_frame(tiles[SCR_SYSINFO],   SCR_SYSINFO,   diameter);
    _create_watch_frame(tiles[SCR_NAV],       SCR_NAV,       diameter);

    /* ── Build each screen's content ─────────────────────── */
    clock_face_create(tiles[SCR_CLOCK], diameter);
    stopwatch_create(tiles[SCR_STOPWATCH], diameter);
    timer_screen_create(tiles[SCR_TIMER], diameter);
    alarm_screen_create(tiles[SCR_ALARM], diameter);
    weather_screen_create(tiles[SCR_WEATHER], diameter);
    radar_screen_create(tiles[SCR_RADAR], diameter);
    compass_screen_create(tiles[SCR_COMPASS], diameter);
    media_screen_create(tiles[SCR_MEDIA], diameter);
    level_screen_create(tiles[SCR_LEVEL], diameter);
    sysinfo_screen_create(tiles[SCR_SYSINFO], diameter);
    nav_screen_create(tiles[SCR_NAV], diameter);

    /* ── Time header seg7 displays on non-clock screens ──── */
    {
        lv_color_t hdr_col = lv_color_white();
        int screens_with_header[] = { SCR_STOPWATCH, SCR_TIMER, SCR_ALARM, SCR_WEATHER, SCR_RADAR, SCR_COMPASS, SCR_MEDIA, SCR_LEVEL, SCR_SYSINFO, SCR_NAV };
        for (int si = 0; si < 10; si++) {
            int idx = screens_with_header[si];
            time_headers[idx] = seg7_create(tiles[idx], 5, 18, hdr_col, 2);
            lv_obj_align(seg7_get_obj(time_headers[idx]),
                         LV_ALIGN_TOP_MID, 0, 70);

            time_ampm_lbls[idx] = lv_label_create(tiles[idx]);
            lv_obj_set_style_text_font(time_ampm_lbls[idx],
                                       &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(time_ampm_lbls[idx], hdr_col, 0);
            lv_obj_align(time_ampm_lbls[idx], LV_ALIGN_TOP_MID, 60, 72);
            lv_obj_add_flag(time_ampm_lbls[idx], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* ── Overlays (menu / notification / sleep) on parent ──── */
    clock_face_create_overlays(parent);


    /* ── Page indicator dots ───────────────────────────────── */
    {
        int total_w = SCR_COUNT * DOT_SIZE + (SCR_COUNT - 1) * DOT_GAP;
        int start_x = (w - total_w) / 2;
        int dot_y   = h - DOT_BOTTOM;

        for (int i = 0; i < SCR_COUNT; i++) {
            dots[i] = lv_obj_create(parent);
            lv_obj_set_size(dots[i], DOT_SIZE, DOT_SIZE);
            lv_obj_set_pos(dots[i],
                           start_x + i * (DOT_SIZE + DOT_GAP), dot_y);
            lv_obj_set_style_radius(dots[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dots[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(dots[i], 0, 0);
            lv_obj_set_style_pad_all(dots[i], 0, 0);
            lv_obj_clear_flag(dots[i],
                LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    /* Save parent for dot repositioning later */
    dot_parent = parent;

    /* Initial layout */
    cur_virt = 0;
    drag_offset = 0;
    _layout_tiles();
    screen_manager_update_indicators();
}

void screen_manager_loop(void) {
    _poll_swipe();
    /* Skip sub-screen UI updates during animation – the screens are
       sliding off-screen so visual updates are wasted work.  The
       underlying timers/counters still tick via their own interrupts. */
    if (!animating) {
        stopwatch_loop();
        timer_screen_loop();
        alarm_screen_loop();
    }
}

bool screen_manager_is_swiping(void) {
    return dragging || animating;
}

void screen_manager_set_time(int h, int m, int sec) {
    alarm_screen_set_current_time(h, m, sec);

    /* Only update headers when hour or minute changes */
    static int prev_h = -1, prev_m = -1;
    static bool prev_24h = true;
    bool is24h = clock_face_get_24h();
    if (h == prev_h && m == prev_m && is24h == prev_24h) return;
    prev_h = h; prev_m = m; prev_24h = is24h;
    char buf[8];
    if (is24h) {
        snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    } else {
        int h12 = h % 12;
        if (h12 == 0) h12 = 12;
        snprintf(buf, sizeof(buf), "%d:%02d", h12, m);
    }
    const char *ap = (h < 12) ? "AM" : "PM";

    int screens_with_header[] = { SCR_STOPWATCH, SCR_TIMER, SCR_ALARM, SCR_WEATHER, SCR_RADAR, SCR_COMPASS, SCR_MEDIA, SCR_LEVEL, SCR_SYSINFO, SCR_NAV };
    for (int si = 0; si < 10; si++) {
        int idx = screens_with_header[si];
        if (!time_headers[idx]) continue;
        seg7_set_text(time_headers[idx], buf);
        int hdr_y = 70;
        lv_obj_align(seg7_get_obj(time_headers[idx]),
                     LV_ALIGN_TOP_MID, is24h ? 0 : -17, hdr_y);

        if (time_ampm_lbls[idx]) {
            if (is24h) {
                lv_obj_add_flag(time_ampm_lbls[idx], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_label_set_text(time_ampm_lbls[idx], ap);
                lv_obj_clear_flag(time_ampm_lbls[idx], LV_OBJ_FLAG_HIDDEN);
                lv_obj_align_to(time_ampm_lbls[idx],
                    seg7_get_obj(time_headers[idx]),
                    LV_ALIGN_OUT_RIGHT_MID, 4, 0);
            }
        }
    }
}

void screen_manager_goto(int phys_idx) {
    if (phys_idx < 0 || phys_idx >= SCR_COUNT) return;
    if (animating) return;
    int virt = _phys_to_virt(phys_idx);
    if (virt < 0) return;   /* screen not active */
    if (virt == cur_virt) return;
    /* Determine shortest wrap direction */
    int fwd = ((virt - cur_virt) + active_count) % active_count;
    int bwd = ((cur_virt - virt) + active_count) % active_count;
    lv_coord_t start;
    if (fwd <= bwd) {
        start = -scr_w;  /* slide left */
    } else {
        start = scr_w;   /* slide right */
    }
    anim_vel_hint = 0;   /* programmatic navigation: use default duration */
    _animate_to(virt, start);
}

int screen_manager_current(void) {
    return active_order[cur_virt];
}

lv_obj_t *screen_manager_get_tile(int idx) {
    if (idx < 0 || idx >= SCR_COUNT) return NULL;
    return tiles[idx];
}

void screen_manager_update_indicators(void) {
    int total_w = active_count * DOT_SIZE + (active_count - 1) * DOT_GAP;
    int start_x = (scr_w - total_w) / 2;
    int dy      = scr_w - DOT_BOTTOM;

    for (int i = 0; i < SCR_COUNT; i++) {
        if (!dots[i]) continue;
        if (i < active_count) {
            lv_obj_clear_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(dots[i],
                           start_x + i * (DOT_SIZE + DOT_GAP), dy);
            lv_obj_set_style_bg_color(dots[i],
                (i == cur_virt) ? COL_DOT_ON : COL_DOT_OFF, 0);
        } else {
            lv_obj_add_flag(dots[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void screen_manager_set_battery(int percent, bool charging) {
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;

    static int prev_percent = -1;
    static bool prev_charging = false;
    if (percent == prev_percent && charging == prev_charging) return;
    prev_percent = percent;
    prev_charging = charging;

    clock_face_set_battery(percent, charging);

    lv_color_t arc_col = charging ? lv_color_make(0, 200, 60)
                                  : lv_color_make(0, 140, 255);
    for (int i = 0; i < SCR_COUNT; i++) {
        if (i == SCR_CLOCK || !frame_batt_arc[i]) continue;
        lv_arc_set_value(frame_batt_arc[i], percent);
        lv_obj_set_style_arc_color(frame_batt_arc[i], arc_col, LV_PART_INDICATOR);
    }
}

/* ── Screen order / enable API ─────────────────────────────────── */

void screen_manager_set_screen_order(const int *phys_ids, int count) {
    /* Clock is always first */
    active_order[0] = SCR_CLOCK;
    active_count = 1;

    for (int i = 0; i < count && active_count < SCR_COUNT; i++) {
        int id = phys_ids[i];
        if (id <= SCR_CLOCK || id >= SCR_COUNT) continue;
        /* Avoid duplicates */
        bool dup = false;
        for (int j = 0; j < active_count; j++) {
            if (active_order[j] == id) { dup = true; break; }
        }
        if (!dup) {
            active_order[active_count++] = id;
        }
    }

    /* Reset to clock screen */
    cur_virt = 0;
    drag_offset = 0;
    vis_cur = vis_prev = vis_next = -1;  /* force full visibility update */
    _layout_tiles();
    screen_manager_update_indicators();
}

int screen_manager_get_screen_order(int *out, int max) {
    int n = active_count < max ? active_count : max;
    for (int i = 0; i < n; i++) out[i] = active_order[i];
    return n;
}

int screen_manager_get_active_count(void) {
    return active_count;
}

const char *screen_manager_get_screen_name(int physical_idx) {
    if (physical_idx < 0 || physical_idx >= SCR_COUNT) return "?";
    return screen_names[physical_idx];
}
