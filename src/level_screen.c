/*
 *  level_screen.c – Spirit level / inclinometer using QMI8658 IMU
 *
 *  A circular bubble level with concentric rings at 5° increments,
 *  crosshair markings, and numeric pitch/roll readouts.  The bubble
 *  moves opposite to tilt, exactly like a real spirit level.
 *
 *  All drawing uses native LVGL widgets (objects, lines, labels).
 */

#include "level_screen.h"
#include "screen_manager.h"
#include <lvgl.h>
#include <math.h>
#include <stdio.h>

/* ── Maths ─────────────────────────────────────────────────────── */
#define PI_F        3.14159265f
#define RAD_TO_DEG  (180.0f / PI_F)

/* ── Layout ────────────────────────────────────────────────────── */
#define LEVEL_RADIUS     95        /* outer ring radius (px)       */
#define BUBBLE_RADIUS    16        /* bubble circle radius (px)    */
#define DEG_PER_PX       (15.0f / (float)LEVEL_RADIUS)
                                   /* 15° maps to full radius      */
#define SMOOTH           0.25f     /* low-pass filter factor        */
#define NUM_RINGS         3        /* rings at 5°, 10°, 15°        */

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_TITLE    lv_color_make(100, 100, 110)
#define COL_RING     lv_color_make(80, 100,  80)
#define COL_CROSS    lv_color_make(70,  80,  70)
#define COL_LEVEL_BG lv_color_make(18,  22,  18)
#define COL_BUBBLE   lv_color_make(80, 200, 100)
#define COL_BUBBLE_C lv_color_make(50, 255,  80)  /* centred colour */
#define COL_ANGLE    lv_color_make(200, 200, 180)
#define COL_LABEL    lv_color_make(120, 120, 130)
#define COL_CENTRED  lv_color_make(50, 255, 80)

/* ── Screen centre ─────────────────────────────────────────────── */
static int cx, cy;

/* ── Smoothed tilt ─────────────────────────────────────────────── */
static float sm_pitch = 0;
static float sm_roll  = 0;

/* ── Calibration offsets (set by long-press) ───────────────────── */
static float cal_pitch = 0;
static float cal_roll  = 0;
static lv_obj_t *cal_lbl = NULL;   /* brief "Calibrated!" flash    */

/* ── LVGL objects ──────────────────────────────────────────────── */
static lv_obj_t *level_bg    = NULL;    /* dark square background  */
static lv_obj_t *bubble      = NULL;    /* the bubble              */

/* Concentric ring polylines (drawn as line segments, no AA masks) */
#define RING_PTS  37   /* 360/10 + 1 = closed polygon */
static lv_obj_t   *ring_objs[NUM_RINGS];
static lv_point_t  ring_pts[NUM_RINGS][RING_PTS];

/* Crosshair lines */
static lv_obj_t   *cross_h   = NULL;
static lv_point_t  cross_h_pts[2];
static lv_obj_t   *cross_v   = NULL;
static lv_point_t  cross_v_pts[2];

/* Angle labels */
static lv_obj_t *pitch_lbl   = NULL;
static lv_obj_t *roll_lbl    = NULL;
static lv_obj_t *status_lbl  = NULL;    /* "LEVEL" when centred    */

/* Degree markers around rings */
static lv_obj_t *deg_labels[4];         /* 5° labels at N/S/E/W    */

/* ── Create ────────────────────────────────────────────────────── */
void level_screen_create(lv_obj_t *parent, int diameter)
{
    (void)diameter;
    cx = 233;
    cy = 230;

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "LEVEL");
    lv_obj_set_style_text_color(title, COL_TITLE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* ── Concentric rings at 5°, 10°, 15° (polylines, no AA mask) ── */
    for (int i = 0; i < NUM_RINGS; i++) {
        int r = (int)((float)(i + 1) * 5.0f / DEG_PER_PX);
        /* Pre-compute circle polygon */
        for (int j = 0; j < RING_PTS; j++) {
            float a = (float)j * (2.0f * PI_F / (float)(RING_PTS - 1));
            ring_pts[i][j].x = (lv_coord_t)(cx + (int)(cosf(a) * (float)r));
            ring_pts[i][j].y = (lv_coord_t)(cy + (int)(sinf(a) * (float)r));
        }
        ring_objs[i] = lv_line_create(parent);
        lv_line_set_points(ring_objs[i], ring_pts[i], RING_PTS);
        lv_obj_set_style_line_color(ring_objs[i], COL_RING, 0);
        lv_obj_set_style_line_width(ring_objs[i], (i == 2) ? 2 : 1, 0);
        lv_obj_set_style_line_opa(ring_objs[i],
            (i == 2) ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_obj_clear_flag(ring_objs[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(ring_objs[i], 0, 0);
    }

    /* ── Degree labels at ring edges (5° markers) ── */
    static const char *deg_txts[4] = {"5\xC2\xB0", "5\xC2\xB0",
                                       "5\xC2\xB0", "5\xC2\xB0"};
    int r5 = (int)(5.0f / DEG_PER_PX);
    lv_point_t offsets[4] = {
        {0, (lv_coord_t)(-r5 - 14)},   /* top    */
        {0, (lv_coord_t)(r5 + 2)},     /* bottom */
        {(lv_coord_t)(-r5 - 22), 0},   /* left   */
        {(lv_coord_t)(r5 + 4), 0}      /* right  */
    };
    for (int i = 0; i < 4; i++) {
        deg_labels[i] = lv_label_create(parent);
        lv_label_set_text(deg_labels[i], deg_txts[i]);
        lv_obj_set_style_text_color(deg_labels[i], COL_LABEL, 0);
        lv_obj_set_style_text_font(deg_labels[i],
                                   &lv_font_montserrat_14, 0);
        lv_obj_set_pos(deg_labels[i],
                       cx + offsets[i].x - 8,
                       cy + offsets[i].y - 6);
    }

    /* ── Crosshair ── */
    cross_h = lv_line_create(parent);
    cross_h_pts[0] = (lv_point_t){
        (lv_coord_t)(cx - LEVEL_RADIUS), (lv_coord_t)cy};
    cross_h_pts[1] = (lv_point_t){
        (lv_coord_t)(cx + LEVEL_RADIUS), (lv_coord_t)cy};
    lv_line_set_points(cross_h, cross_h_pts, 2);
    lv_obj_set_style_line_color(cross_h, COL_CROSS, 0);
    lv_obj_set_style_line_width(cross_h, 2, 0);
    lv_obj_set_style_line_opa(cross_h, LV_OPA_80, 0);
    lv_obj_clear_flag(cross_h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(cross_h, 0, 0);

    cross_v = lv_line_create(parent);
    cross_v_pts[0] = (lv_point_t){
        (lv_coord_t)cx, (lv_coord_t)(cy - LEVEL_RADIUS)};
    cross_v_pts[1] = (lv_point_t){
        (lv_coord_t)cx, (lv_coord_t)(cy + LEVEL_RADIUS)};
    lv_line_set_points(cross_v, cross_v_pts, 2);
    lv_obj_set_style_line_color(cross_v, COL_CROSS, 0);
    lv_obj_set_style_line_width(cross_v, 2, 0);
    lv_obj_set_style_line_opa(cross_v, LV_OPA_80, 0);
    lv_obj_clear_flag(cross_v, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(cross_v, 0, 0);

    /* Centre dot */
    lv_obj_t *cdot = lv_obj_create(parent);
    lv_obj_set_size(cdot, 6, 6);
    lv_obj_set_style_radius(cdot, 3, 0);
    lv_obj_set_style_bg_color(cdot, COL_RING, 0);
    lv_obj_set_style_bg_opa(cdot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cdot, 0, 0);
    lv_obj_clear_flag(cdot,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(cdot, cx - 3, cy - 3);

    /* ── Bubble ── */
    bubble = lv_obj_create(parent);
    lv_obj_set_size(bubble, BUBBLE_RADIUS * 2, BUBBLE_RADIUS * 2);
    lv_obj_set_style_radius(bubble, BUBBLE_RADIUS, 0);
    lv_obj_set_style_bg_color(bubble, COL_BUBBLE, 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bubble, COL_BUBBLE, 0);
    lv_obj_set_style_border_width(bubble, 2, 0);
    lv_obj_set_style_border_opa(bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bubble, 0, 0);
    lv_obj_clear_flag(bubble,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(bubble, cx - BUBBLE_RADIUS, cy - BUBBLE_RADIUS);

    /* ── Pitch / Roll readouts ── */
    pitch_lbl = lv_label_create(parent);
    lv_label_set_text(pitch_lbl, "Pitch:  0.0\xC2\xB0");
    lv_obj_set_style_text_color(pitch_lbl, COL_ANGLE, 0);
    lv_obj_set_style_text_font(pitch_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(pitch_lbl, LV_ALIGN_CENTER, -55, LEVEL_RADIUS + 24);

    roll_lbl = lv_label_create(parent);
    lv_label_set_text(roll_lbl, "Roll:  0.0\xC2\xB0");
    lv_obj_set_style_text_color(roll_lbl, COL_ANGLE, 0);
    lv_obj_set_style_text_font(roll_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(roll_lbl, LV_ALIGN_CENTER, 55, LEVEL_RADIUS + 24);

    /* ── "LEVEL" status ── */
    status_lbl = lv_label_create(parent);
    lv_label_set_text(status_lbl, LV_SYMBOL_OK " LEVEL");
    lv_obj_set_style_text_color(status_lbl, COL_CENTRED, 0);
    lv_obj_set_style_text_font(status_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, LEVEL_RADIUS + 50);

    /* ── Calibration flash label (hidden by default) ── */
    cal_lbl = lv_label_create(parent);
    lv_label_set_text(cal_lbl, "Calibrated!");
    lv_obj_set_style_text_color(cal_lbl, COL_CENTRED, 0);
    lv_obj_set_style_text_font(cal_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(cal_lbl, LV_ALIGN_CENTER, 0, -(LEVEL_RADIUS + 20));
    lv_obj_add_flag(cal_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Long-press hint ── */
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "Long press to calibrate");
    lv_obj_set_style_text_color(hint, lv_color_make(80, 80, 90), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, LEVEL_RADIUS + 72);
}

/* ── Calibration flash hide callback ────────────────────────────── */
static lv_timer_t *hide_tmr = NULL;

static void _cal_hide_cb(lv_timer_t *t)
{
    (void)t;
    hide_tmr = NULL;  /* timer auto-deletes after firing (repeat=1) */
    if (cal_lbl) lv_obj_add_flag(cal_lbl, LV_OBJ_FLAG_HIDDEN);
}

/* ── Calibration trigger (called from long-press) ──────────────── */
static void _calibrate(void)
{
    cal_pitch = sm_pitch + cal_pitch;  /* absorb current smoothed angle */
    cal_roll  = sm_roll  + cal_roll;
    sm_pitch = 0;
    sm_roll  = 0;

    /* Flash "Calibrated!" for 1.5 s */
    if (cal_lbl) {
        lv_obj_clear_flag(cal_lbl, LV_OBJ_FLAG_HIDDEN);
        if (hide_tmr) {
            /* Timer still pending — just reset its period */
            lv_timer_reset(hide_tmr);
        } else {
            hide_tmr = lv_timer_create(_cal_hide_cb, 1500, NULL);
            lv_timer_set_repeat_count(hide_tmr, 1);
        }
    }
}

/* ── Long-press detection (polled from feed_imu) ───────────────── */
#define LONGPRESS_MS  800
static bool     lp_tracking = false;
static uint32_t lp_start    = 0;
static bool     lp_fired    = false;

static void _check_longpress(void)
{
    /* Use LVGL indev press state */
    lv_indev_t *indev = lv_indev_get_next(NULL);
    if (!indev) return;
    bool pressed = (indev->proc.state == LV_INDEV_STATE_PRESSED);

    if (pressed && !lp_tracking) {
        lp_tracking = true;
        lp_fired    = false;
        lp_start    = lv_tick_get();
    } else if (pressed && lp_tracking && !lp_fired) {
        if (lv_tick_get() - lp_start >= LONGPRESS_MS) {
            lp_fired = true;
            _calibrate();
        }
    } else if (!pressed) {
        lp_tracking = false;
        lp_fired    = false;
    }
}

/* ── Feed IMU data ─────────────────────────────────────────────── */
void level_screen_feed_imu(float ax, float ay, float az)
{
    /* Compute tilt angles from accelerometer */
    float amag = sqrtf(ax * ax + ay * ay + az * az);
    if (amag < 0.01f) return;

    float pitch_raw = asinf(-ax / amag) * RAD_TO_DEG - cal_pitch;
    float roll_raw  = asinf( ay / amag) * RAD_TO_DEG - cal_roll;

    /* Low-pass filter */
    sm_pitch += SMOOTH * (pitch_raw - sm_pitch);
    sm_roll  += SMOOTH * (roll_raw  - sm_roll);

    /* Clamp to ±15° for display (bubble stays inside circle) */
    float dp = sm_pitch;
    float dr = sm_roll;
    if (dp >  15.0f) dp =  15.0f;
    if (dp < -15.0f) dp = -15.0f;
    if (dr >  15.0f) dr =  15.0f;
    if (dr < -15.0f) dr = -15.0f;

    /* Map tilt to pixel offset (bubble moves opposite to tilt) */
    float bx = -dr / DEG_PER_PX;
    float by =  dp / DEG_PER_PX;   /* pitch up → bubble up  */

    /* Clamp to circular boundary */
    float dist = sqrtf(bx * bx + by * by);
    float max_r = (float)(LEVEL_RADIUS - BUBBLE_RADIUS);
    if (dist > max_r) {
        float scale = max_r / dist;
        bx *= scale;
        by *= scale;
    }

    /* Rate-limit visuals to ~30 Hz and only when visible */
    static uint32_t last_vis = 0;
    uint32_t tnow = lv_tick_get();
    if (tnow - last_vis < 33) return;
    if (screen_manager_current() != SCR_LEVEL) return;
    last_vis = tnow;

    /* Check for long-press → calibrate */
    _check_longpress();

    /* Position bubble */
    lv_obj_set_pos(bubble,
                   cx + (int)bx - BUBBLE_RADIUS,
                   cy + (int)by - BUBBLE_RADIUS);

    /* Check if centred (within 1°) */
    float total_tilt = sqrtf(sm_pitch * sm_pitch + sm_roll * sm_roll);
    if (total_tilt < 1.0f) {
        lv_obj_set_style_bg_color(bubble, COL_BUBBLE_C, 0);
        lv_obj_set_style_border_color(bubble, COL_BUBBLE_C, 0);
        lv_obj_clear_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_style_bg_color(bubble, COL_BUBBLE, 0);
        lv_obj_set_style_border_color(bubble, COL_BUBBLE, 0);
        lv_obj_add_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Update angle readouts */
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "P: %+5.1f\xC2\xB0", sm_pitch);
        lv_label_set_text(pitch_lbl, buf);
        snprintf(buf, sizeof(buf), "R: %+5.1f\xC2\xB0", sm_roll);
        lv_label_set_text(roll_lbl, buf);
    }
}
