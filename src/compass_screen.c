/*
 *  compass_screen.c – Spherical flight compass using QMI8658 IMU
 *
 *  Renders a 3D compass ball inspired by aircraft directional
 *  gyros.  Heading from gyro-Z integration rotates the ball;
 *  accelerometer pitch/roll tilts it for a convincing 3-D effect.
 *  Long-press sets current heading as "north".
 *
 *  All drawing uses native LVGL widgets (lines, labels, objects).
 */

#include "compass_screen.h"
#include "screen_manager.h"
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Maths ─────────────────────────────────────────────────────── */
#define PI_F        3.14159265f
#define DEG_TO_RAD  (PI_F / 180.0f)
#define RAD_TO_DEG  (180.0f / PI_F)

/* ── Sphere geometry ───────────────────────────────────────────── */
#define SPHERE_R      105         /* ball radius (px)               */

/* ── Grid resolution ───────────────────────────────────────────── */
#define NUM_LAT        5          /* latitude lines                 */
#define NUM_LON       12          /* longitude lines (every 30°)    */
#define ARC_STEP       5          /* azimuth sample step (deg)      */
#define ARC_SAMPLES   72          /* 360 / ARC_STEP                 */
#define MAX_ARC_PTS   74          /* max pts per visible lat arc    */
#define MAX_MER_PTS   20          /* max pts per meridian           */

/* ── Heading labels ────────────────────────────────────────────── */
#define NUM_HDG_LBL   12
static const char  *hdg_texts[NUM_HDG_LBL] = {
    "N","3","6","E","12","15","S","21","24","W","30","33"
};
static const float  hdg_az[NUM_HDG_LBL] = {
    0,30,60,90,120,150,180,210,240,270,300,330
};

/* ── Aviation-instrument colour palette ────────────────────────── */
#define COL_BALL     lv_color_make(25,  30,  35)
#define COL_EDGE     lv_color_make(55,  60,  55)
#define COL_GRID     lv_color_make(70,  80,  70)
#define COL_EQUATOR  lv_color_make(110, 120, 100)
#define COL_LABEL    lv_color_make(220, 220, 200)
#define COL_NORTH    lv_color_make(255, 70,  50)
#define COL_LUBBER   lv_color_make(255, 160,  0)
#define COL_HEADING  lv_color_make(255, 255, 255)
#define COL_DIM      lv_color_make(80,  80,  90)
#define COL_TITLE    lv_color_make(100, 100, 110)
#define COL_CAL      lv_color_make(255, 200, 80)

/* ── Screen centre ─────────────────────────────────────────────── */
static int cx, cy;

/* ── Heading / tilt state ──────────────────────────────────────── */
static float heading_raw  = 0;
static float north_offset = 0;
static float cur_pitch    = 0;   /* rad, from accelerometer        */
static float cur_roll     = 0;   /* rad                            */

/* ── LVGL objects ──────────────────────────────────────────────── */
static lv_obj_t *sphere_bg   = NULL;
static lv_obj_t *heading_lbl = NULL;
static lv_obj_t *cal_lbl     = NULL;

/* Latitude polylines */
static lv_obj_t   *lat_objs[NUM_LAT];
static lv_point_t  lat_pts[NUM_LAT][MAX_ARC_PTS];

/* Longitude polylines */
static lv_obj_t   *lon_objs[NUM_LON];
static lv_point_t  lon_pts[NUM_LON][MAX_MER_PTS];

/* Heading labels on equator band */
static lv_obj_t *hdg_objs[NUM_HDG_LBL];

/* Lubber mark (fixed reference at top) */
static lv_obj_t   *lubber_obj = NULL;
static lv_point_t  lubber_p[2];

/* Long-press detection */
static bool     lp_active    = false;
static uint32_t lp_start_ms  = 0;
static uint32_t cal_flash_ms = 0;
#define LONG_PRESS_MS 800

/* ── 3-D sphere projection ────────────────────────────────────── */
/*  az_deg  = azimuth on sphere (0 = north, 90 = east …)          *
 *  el_deg  = elevation (0 = equator, +90 = top pole)             *
 *  heading = current heading (deg)                                *
 *  pitch   = tilt fwd/back (rad)                                 *
 *  roll    = tilt left/right (rad)                                *
 *  Returns true when the point is on the visible (front)         *
 *  hemisphere.  *out_z receives normalised depth (0…1).          */
static bool _project(float az_deg, float el_deg,
                     float heading, float pitch, float roll,
                     int *sx, int *sy, float *out_z)
{
    float az = az_deg * DEG_TO_RAD;
    float el = el_deg * DEG_TO_RAD;

    /* Unit-sphere coordinates */
    float x = cosf(el) * sinf(az);
    float y = sinf(el);
    float z = cosf(el) * cosf(az);

    /* Heading – Y-axis rotation (ball rotates opposite to heading) */
    float h  = -heading * DEG_TO_RAD;
    float ch = cosf(h), sh = sinf(h);
    float x1 =  x * ch + z * sh;
    float y1 =  y;
    float z1 = -x * sh + z * ch;

    /* Pitch – X-axis rotation */
    float cp = cosf(pitch), sp = sinf(pitch);
    float x2 =  x1;
    float y2 =  y1 * cp - z1 * sp;
    float z2 =  y1 * sp + z1 * cp;

    /* Roll – Z-axis rotation */
    float cr = cosf(roll), sr = sinf(roll);
    float x3 =  x2 * cr - y2 * sr;
    float y3 =  x2 * sr + y2 * cr;
    float z3 =  z2;

    *sx = cx + (int)(x3 * (float)SPHERE_R);
    *sy = cy - (int)(y3 * (float)SPHERE_R);
    if (out_z) *out_z = z3;
    return z3 > 0.02f;
}

/* ── Update every visual element ───────────────────────────────── */
static void _update_visuals(void)
{
    float hdg = heading_raw - north_offset + 180.0f;
    while (hdg <    0) hdg += 360.0f;
    while (hdg >= 360) hdg -= 360.0f;

    float p = cur_pitch;
    float r = cur_roll;

    /* ── Heading readout ── */
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%03d\xC2\xB0",
                 ((int)(hdg + 0.5f)) % 360);
        lv_label_set_text(heading_lbl, buf);
    }

    /* ── Latitude lines ── */
    static const float lat_el[NUM_LAT] = {-60, -30, 0, 30, 60};

    for (int li = 0; li < NUM_LAT; li++) {
        float el = lat_el[li];

        /* Project every sample around the full circle */
        bool vis[ARC_SAMPLES];
        int  px_arr[ARC_SAMPLES], py_arr[ARC_SAMPLES];
        int  vis_count = 0;

        for (int ai = 0; ai < ARC_SAMPLES; ai++) {
            float az_s = (float)(ai * ARC_STEP);
            float zz;
            vis[ai] = _project(az_s, el, hdg, p, r,
                               &px_arr[ai], &py_arr[ai], &zz);
            if (vis[ai]) vis_count++;
        }

        if (vis_count < 2) {
            lv_obj_add_flag(lat_objs[li], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(lat_objs[li], LV_OBJ_FLAG_HIDDEN);

        /* Find the first invisible→visible transition (start of arc) */
        int start = 0;
        for (int ai = 0; ai < ARC_SAMPLES; ai++) {
            int prev = (ai + ARC_SAMPLES - 1) % ARC_SAMPLES;
            if (vis[ai] && !vis[prev]) { start = ai; break; }
        }

        /* Collect the single continuous visible arc */
        int n = 0;
        bool full_circle = true;
        for (int k = 0; k < ARC_SAMPLES && n < MAX_ARC_PTS; k++) {
            int ai = (start + k) % ARC_SAMPLES;
            if (vis[ai]) {
                lat_pts[li][n].x = (lv_coord_t)px_arr[ai];
                lat_pts[li][n].y = (lv_coord_t)py_arr[ai];
                n++;
            } else {
                full_circle = false;
                if (n > 0) break;
            }
        }

        /* Close the ring when the entire circle is visible */
        if (full_circle && n >= 2 && n < MAX_ARC_PTS) {
            lat_pts[li][n] = lat_pts[li][0];
            n++;
        }

        if (n >= 2) {
            lv_line_set_points(lat_objs[li], lat_pts[li], (uint16_t)n);
            lv_obj_set_pos(lat_objs[li], 0, 0);
        } else {
            lv_obj_add_flag(lat_objs[li], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* ── Longitude / meridian lines ── */
    for (int mi = 0; mi < NUM_LON; mi++) {
        float az = (float)(mi * 30);
        int n = 0;

        for (int ei = -8; ei <= 8 && n < MAX_MER_PTS; ei++) {
            float el = (float)(ei * 10);   /* -80° to +80° */
            int sx, sy;  float zz;
            if (_project(az, el, hdg, p, r, &sx, &sy, &zz)) {
                lon_pts[mi][n].x = (lv_coord_t)sx;
                lon_pts[mi][n].y = (lv_coord_t)sy;
                n++;
            } else if (n > 0) {
                break;   /* end of visible portion */
            }
        }

        if (n >= 2) {
            lv_obj_clear_flag(lon_objs[mi], LV_OBJ_FLAG_HIDDEN);
            lv_line_set_points(lon_objs[mi], lon_pts[mi], (uint16_t)n);
            lv_obj_set_pos(lon_objs[mi], 0, 0);
        } else {
            lv_obj_add_flag(lon_objs[mi], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* ── Heading labels (on equator band) ── */
    for (int i = 0; i < NUM_HDG_LBL; i++) {
        int sx, sy;  float zz;
        bool v = _project(hdg_az[i], 0, hdg, p, r, &sx, &sy, &zz);

        if (v && zz > 0.15f) {
            lv_obj_clear_flag(hdg_objs[i], LV_OBJ_FLAG_HIDDEN);

            /* Approximate centering based on character count */
            int len = (int)strlen(hdg_texts[i]);
            int off_x = (len == 1) ? -8 : -12;
            lv_obj_set_pos(hdg_objs[i], sx + off_x, sy - 12);

            /* Depth-based opacity fade for 3-D curvature feel */
            uint8_t opa = (uint8_t)(zz * 255.0f);
            if (opa < 50) opa = 50;
            lv_obj_set_style_text_opa(hdg_objs[i], opa, 0);
        } else {
            lv_obj_add_flag(hdg_objs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* ── Calibration flash ── */
    if (cal_flash_ms > 0) {
        uint32_t now = lv_tick_get();
        if (now - cal_flash_ms < 1500)
            lv_obj_clear_flag(cal_lbl, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_add_flag(cal_lbl, LV_OBJ_FLAG_HIDDEN);
            cal_flash_ms = 0;
        }
    }
}

/* ── Long-press callback ──────────────────────────────────────── */
static void _press_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lp_active   = true;
        lp_start_ms = lv_tick_get();
    } else if (code == LV_EVENT_PRESSING) {
        if (lp_active &&
            (lv_tick_get() - lp_start_ms >= LONG_PRESS_MS)) {
            lp_active = false;
            compass_screen_set_north();
        }
    } else if (code == LV_EVENT_RELEASED ||
               code == LV_EVENT_PRESS_LOST) {
        lp_active = false;
    }
}

/* ── Create ────────────────────────────────────────────────────── */
void compass_screen_create(lv_obj_t *parent, int diameter)
{
    (void)diameter;
    cx = 233;
    cy = 230;

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "COMPASS");
    lv_obj_set_style_text_color(title, COL_TITLE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* ── Dark sphere background ── */
    sphere_bg = lv_obj_create(parent);
    lv_obj_set_size(sphere_bg, SPHERE_R * 2, SPHERE_R * 2);
    lv_obj_set_style_radius(sphere_bg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sphere_bg, COL_BALL, 0);
    lv_obj_set_style_bg_opa(sphere_bg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sphere_bg, COL_EDGE, 0);
    lv_obj_set_style_border_width(sphere_bg, 2, 0);
    lv_obj_clear_flag(sphere_bg,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(sphere_bg, cx - SPHERE_R, cy - SPHERE_R);

    /* ── Latitude lines ── */
    for (int i = 0; i < NUM_LAT; i++) {
        lat_objs[i] = lv_line_create(parent);
        lv_obj_set_style_line_color(lat_objs[i],
            (i == 2) ? COL_EQUATOR : COL_GRID, 0);
        lv_obj_set_style_line_width(lat_objs[i],
            (i == 2) ? 2 : 1, 0);
        lv_obj_clear_flag(lat_objs[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(lat_objs[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* ── Longitude lines ── */
    for (int i = 0; i < NUM_LON; i++) {
        lon_objs[i] = lv_line_create(parent);
        lv_obj_set_style_line_color(lon_objs[i], COL_GRID, 0);
        lv_obj_set_style_line_width(lon_objs[i], 1, 0);
        lv_obj_clear_flag(lon_objs[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(lon_objs[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* ── Heading labels ── */
    for (int i = 0; i < NUM_HDG_LBL; i++) {
        hdg_objs[i] = lv_label_create(parent);
        lv_label_set_text(hdg_objs[i], hdg_texts[i]);

        bool cardinal = (i == 0 || i == 3 || i == 6 || i == 9);
        lv_obj_set_style_text_font(hdg_objs[i],
            cardinal ? &lv_font_montserrat_24
                     : &lv_font_montserrat_20, 0);

        lv_color_t col = (i == 0) ? COL_NORTH : COL_LABEL;
        lv_obj_set_style_text_color(hdg_objs[i], col, 0);
        lv_obj_set_style_text_align(hdg_objs[i],
                                    LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_clear_flag(hdg_objs[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(hdg_objs[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* ── Lubber line (fixed orange mark at top of sphere) ── */
    lubber_obj = lv_line_create(parent);
    lubber_p[0] = (lv_point_t){(lv_coord_t)cx,
                               (lv_coord_t)(cy - SPHERE_R - 4)};
    lubber_p[1] = (lv_point_t){(lv_coord_t)cx,
                               (lv_coord_t)(cy - SPHERE_R + 12)};
    lv_line_set_points(lubber_obj, lubber_p, 2);
    lv_obj_set_style_line_color(lubber_obj, COL_LUBBER, 0);
    lv_obj_set_style_line_width(lubber_obj, 3, 0);
    lv_obj_set_style_line_rounded(lubber_obj, true, 0);
    lv_obj_clear_flag(lubber_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(lubber_obj, 0, 0);

    /* ── Heading readout ── */
    heading_lbl = lv_label_create(parent);
    lv_label_set_text(heading_lbl, "000\xC2\xB0");
    lv_obj_set_style_text_font(heading_lbl, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(heading_lbl, COL_HEADING, 0);
    lv_obj_set_style_text_align(heading_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(heading_lbl, LV_ALIGN_CENTER, 0, SPHERE_R + 25);

    /* ── Calibration flash ── */
    cal_lbl = lv_label_create(parent);
    lv_label_set_text(cal_lbl, "North set!");
    lv_obj_set_style_text_font(cal_lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(cal_lbl, COL_CAL, 0);
    lv_obj_set_style_text_align(cal_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(cal_lbl, LV_ALIGN_CENTER, 0, -SPHERE_R - 28);
    lv_obj_add_flag(cal_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Hint ── */
    lv_obj_t *hint = lv_label_create(parent);
    lv_label_set_text(hint, "Long press to set north");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, COL_DIM, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, SPHERE_R + 58);

    /* ── Touch handling ── */
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(parent, _press_cb, LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(parent, _press_cb, LV_EVENT_PRESSING,   NULL);
    lv_obj_add_event_cb(parent, _press_cb, LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(parent, _press_cb, LV_EVENT_PRESS_LOST, NULL);

    _update_visuals();
}

/* ── Feed IMU data ─────────────────────────────────────────────── */
void compass_screen_feed_imu(float ax, float ay, float az,
                              float gx, float gy, float gz, float dt)
{
    /* Compute gravity-aligned yaw from all 3 gyro axes.
       Project gyro vector onto gravity direction to get the
       rotation rate about the vertical axis regardless of tilt. */
    float amag = sqrtf(ax * ax + ay * ay + az * az);
    if (amag > 0.01f) {
        float nx = ax / amag, ny = ay / amag, nz = az / amag;
        float yaw_rate = gx * nx + gy * ny + gz * nz;  /* dot(gyro, gravity) */
        heading_raw -= yaw_rate * dt;
        while (heading_raw <    0) heading_raw += 360.0f;
        while (heading_raw >= 360) heading_raw -= 360.0f;

        /* Pitch/roll for 3D tilt – use atan2 for full range.
           When held upright: ax ≈ -1g, az ≈ 0  → pitch_raw ≈ -π/2
           Offset by +π/2 so equator faces viewer. */
        cur_pitch = atan2f(ax, -az) + (PI_F / 2.0f);
        cur_roll  = asinf(ny);
        if (cur_roll  >  1.0f) cur_roll  =  1.0f;
        if (cur_roll  < -1.0f) cur_roll  = -1.0f;
    }

    /* Rate-limit visuals to ~30 Hz and only when visible */
    static uint32_t last_vis = 0;
    uint32_t now = lv_tick_get();
    if (now - last_vis < 33) return;
    if (screen_manager_current() != SCR_COMPASS) return;
    last_vis = now;

    _update_visuals();
}

/* ── Set north ─────────────────────────────────────────────────── */
void compass_screen_set_north(void)
{
    north_offset = heading_raw;
    cal_flash_ms = lv_tick_get();
    _update_visuals();
}

/* ── Set heading directly (simulator) ─────────────────────────── */
void compass_screen_set_heading(float deg)
{
    heading_raw = deg;
    _update_visuals();
}
