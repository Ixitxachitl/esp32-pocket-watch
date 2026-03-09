/*
 *  radar_screen.c – BLE + WiFi proximity radar display (pure LVGL)
 *
 *  Draws a green radar grid (concentric circles + cross-hairs) and
 *  plots nearby devices as coloured blips.  RSSI → distance from
 *  centre; a hash of the device MAC → angle, so each device keeps
 *  a consistent position between scans.
 *
 *  BLE nodes = cyan dots, WiFi nodes = yellow-green dots.
 *  A fading trail effect dims nodes not seen in the latest scan.
 */

#include "radar_screen.h"
#include <lvgl.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_BG        lv_color_make(5,   12,  5)
#define COL_GRID      lv_color_make(0,   60,  0)
#define COL_GRID_AXIS lv_color_make(0,   80,  0)
#define COL_BLE       lv_color_make(0,  220, 220)   /* cyan  */
#define COL_WIFI      lv_color_make(180, 220, 0)     /* lime  */
#define COL_DIM       lv_color_make(60,  60,  65)
#define COL_TITLE     lv_color_make(100, 100, 110)
#define COL_COUNT     lv_color_make(0,  180,  0)

/* ── Layout constants ──────────────────────────────────────────── */
#define RING_COUNT         4        /* concentric circles          */
#define NODE_DOT_SIZE      10       /* px, diameter of blip dot    */
#define NODE_LABEL_MAX     10       /* chars shown next to blip    */

/* ── Geometry cached at create time ────────────────────────────── */
static int       cx, cy;           /* centre of radar area        */
static int       max_r;            /* radius of outermost ring    */

/* ── Node state ────────────────────────────────────────────────── */
static radar_node_t nodes_buf[RADAR_MAX_NODES];
static int          node_count = 0;

/* ── LVGL blip objects (dot + label) ───────────────────────────── */
static lv_obj_t *blip_dots[RADAR_MAX_NODES]   = {NULL};
static lv_obj_t *blip_lbls[RADAR_MAX_NODES]   = {NULL};

/* ── Header labels ─────────────────────────────────────────────── */
static lv_obj_t *count_lbl = NULL;

/* ── Grid line objects (static points for lv_line) ─────────────── */
#define GRID_LINES  4   /* horiz, vert, diag1, diag2 */
static lv_point_t grid_pts[GRID_LINES][2];
static lv_obj_t  *grid_lines[GRID_LINES] = {NULL};

/* ── Helpers ───────────────────────────────────────────────────── */

/* Map RSSI (dBm) to pixel radius.  -30 dBm → near centre,
   -100 dBm → outermost ring.  Clamped. */
static int _rssi_to_radius(int rssi) {
    if (rssi > -30)  rssi = -30;
    if (rssi < -100) rssi = -100;
    /* 0.0 (strong, centre) → 1.0 (weak, edge) */
    float t = (float)(-30 - rssi) / 70.0f;
    int r = (int)(t * (float)(max_r - 12)) + 12;  /* keep blips off exact centre */
    return r;
}

/* Map node hash → angle in radians (consistent per device) */
static float _hash_to_angle(uint32_t h) {
    /* Golden-ratio based scatter */
    return (float)(h % 36000) / 36000.0f * 2.0f * 3.14159265f;
}

/* ── Create ────────────────────────────────────────────────────── */
void radar_screen_create(lv_obj_t *parent, int diameter) {
    (void)diameter;

    /* Centre of the radar area (offset down for title + time header) */
    cx = 233;  /* half of 466 */
    cy = 233;  /* true centre */
    max_r = 120;

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "RADAR");
    lv_obj_set_style_text_color(title, COL_TITLE, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* Device count label */
    count_lbl = lv_label_create(parent);
    lv_label_set_text(count_lbl, "0 devices");
    lv_obj_set_style_text_color(count_lbl, COL_COUNT, 0);
    lv_obj_set_style_text_font(count_lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(count_lbl, LV_ALIGN_CENTER, 0, max_r + 30);

    /* ── Concentric circle rings (lv_arc widgets styled as thin rings) ── */
    for (int i = 0; i < RING_COUNT; i++) {
        int r = max_r * (i + 1) / RING_COUNT;
        int sz = r * 2;
        lv_obj_t *ring = lv_arc_create(parent);
        lv_obj_set_size(ring, sz, sz);
        lv_arc_set_rotation(ring, 0);
        lv_arc_set_bg_angles(ring, 0, 360);
        lv_arc_set_angles(ring, 0, 0);   /* no indicator arc */
        /* Style the background arc (the full ring) */
        lv_obj_set_style_arc_color(ring, COL_GRID, LV_PART_MAIN);
        lv_obj_set_style_arc_width(ring, 2, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_MAIN);
        /* Hide the indicator arc */
        lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_INDICATOR);
        /* Hide the knob */
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(ring, 0, LV_PART_KNOB);
        /* No background fill on the arc widget itself */
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 0, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(ring, LV_ALIGN_CENTER, 0, cy - 233);
    }

    /* ── Cross-hair lines (native lv_line objects) ── */
    int d = (int)(max_r * 0.707f);
    int ocx = cx;       /* parent-relative offset X (cx = 233 = centre of 466) */
    int ocy = cy;       /* parent-relative offset Y */

    /* Horizontal axis */
    grid_pts[0][0] = (lv_point_t){ocx - max_r, ocy};
    grid_pts[0][1] = (lv_point_t){ocx + max_r, ocy};
    /* Vertical axis */
    grid_pts[1][0] = (lv_point_t){ocx, ocy - max_r};
    grid_pts[1][1] = (lv_point_t){ocx, ocy + max_r};
    /* Diagonal 1 (top-left → bottom-right) */
    grid_pts[2][0] = (lv_point_t){ocx - d, ocy - d};
    grid_pts[2][1] = (lv_point_t){ocx + d, ocy + d};
    /* Diagonal 2 (top-right → bottom-left) */
    grid_pts[3][0] = (lv_point_t){ocx + d, ocy - d};
    grid_pts[3][1] = (lv_point_t){ocx - d, ocy + d};

    for (int i = 0; i < GRID_LINES; i++) {
        grid_lines[i] = lv_line_create(parent);
        lv_line_set_points(grid_lines[i], grid_pts[i], 2);
        lv_obj_set_style_line_color(grid_lines[i],
            (i < 2) ? COL_GRID_AXIS : COL_GRID, 0);
        lv_obj_set_style_line_width(grid_lines[i], 2, 0);
        lv_obj_set_style_line_opa(grid_lines[i], LV_OPA_COVER, 0);
        lv_obj_clear_flag(grid_lines[i], LV_OBJ_FLAG_CLICKABLE);
        /* lv_line positions are absolute pixel coords; set origin to (0,0) */
        lv_obj_set_pos(grid_lines[i], 0, 0);
    }

    /* Pre-create blip objects (hidden until populated) */
    for (int i = 0; i < RADAR_MAX_NODES; i++) {
        /* Dot */
        blip_dots[i] = lv_obj_create(parent);
        lv_obj_set_size(blip_dots[i], NODE_DOT_SIZE, NODE_DOT_SIZE);
        lv_obj_set_style_radius(blip_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(blip_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(blip_dots[i], COL_BLE, 0);
        lv_obj_set_style_border_width(blip_dots[i], 0, 0);
        lv_obj_set_style_pad_all(blip_dots[i], 0, 0);
        lv_obj_clear_flag(blip_dots[i],
            LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(blip_dots[i], LV_OBJ_FLAG_HIDDEN);

        /* Label */
        blip_lbls[i] = lv_label_create(parent);
        lv_label_set_text(blip_lbls[i], "");
        lv_obj_set_style_text_font(blip_lbls[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(blip_lbls[i], COL_DIM, 0);
        lv_obj_add_flag(blip_lbls[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ── Update ────────────────────────────────────────────────────── */
void radar_screen_update(const radar_node_t *nodes, int count) {
    if (count > RADAR_MAX_NODES) count = RADAR_MAX_NODES;
    node_count = count;
    if (count > 0) memcpy(nodes_buf, nodes, count * sizeof(radar_node_t));

    /* Update device count label */
    char buf[24];
    snprintf(buf, sizeof(buf), "%d device%s", count, count == 1 ? "" : "s");
    lv_label_set_text(count_lbl, buf);

    /* Position each blip */
    for (int i = 0; i < RADAR_MAX_NODES; i++) {
        if (i >= count) {
            lv_obj_add_flag(blip_dots[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(blip_lbls[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const radar_node_t *n = &nodes_buf[i];
        int r = _rssi_to_radius(n->rssi);
        float a = _hash_to_angle(n->hash);
        int px = cx + (int)(r * cosf(a)) - NODE_DOT_SIZE / 2;
        int py = cy + (int)(r * sinf(a)) - NODE_DOT_SIZE / 2;

        /* Colour by type */
        lv_color_t col = (n->type == RADAR_NODE_WIFI) ? COL_WIFI : COL_BLE;
        if (!n->active) {
            /* Dim stale nodes */
            col = lv_color_make(
                lv_color_brightness(col) / 3,
                lv_color_brightness(col) / 3,
                lv_color_brightness(col) / 3);
        }
        lv_obj_set_style_bg_color(blip_dots[i], col, 0);
        lv_obj_set_pos(blip_dots[i], px, py);
        lv_obj_clear_flag(blip_dots[i], LV_OBJ_FLAG_HIDDEN);

        /* Abbreviated label */
        char short_name[NODE_LABEL_MAX + 1];
        if (n->name[0]) {
            strncpy(short_name, n->name, NODE_LABEL_MAX);
            short_name[NODE_LABEL_MAX] = '\0';
        } else {
            /* Show last 4 hex of hash as fallback */
            snprintf(short_name, sizeof(short_name), "%04X",
                     (unsigned)(n->hash & 0xFFFF));
        }
        lv_label_set_text(blip_lbls[i], short_name);
        lv_obj_set_style_text_color(blip_lbls[i],
            n->active ? lv_color_make(0, 160, 0) : COL_DIM, 0);
        lv_obj_set_pos(blip_lbls[i], px + NODE_DOT_SIZE + 2, py - 2);
        lv_obj_clear_flag(blip_lbls[i], LV_OBJ_FLAG_HIDDEN);
    }
}
