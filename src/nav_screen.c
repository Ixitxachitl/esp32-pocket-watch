/*
 *  nav_screen.c – Turn-by-turn navigation display (pure LVGL)
 *
 *  Shows navigation data from Gadgetbridge / phone maps:
 *    - Large direction icon (Material Design Icons font)
 *    - Distance to next manoeuvre
 *    - Instruction text (street name / turn description)
 *    - ETA at bottom
 */

#include "nav_screen.h"
#include "clock_face.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

LV_FONT_DECLARE(lv_font_nav);

/* ── Distance unit conversion ──────────────────────────────────── */
/* Parse the distance string from the phone, detect its unit, and
   convert to the user's preferred system (metric ↔ imperial).      */
static void _convert_distance(const char *src, char *out, int out_sz) {
    bool want_metric = clock_face_get_metric();
    double val = strtod(src, NULL);
    if (val <= 0.0) { snprintf(out, out_sz, "%s", src); return; }

    /* Detect source unit */
    bool src_metric;
    bool src_major; /* km/mi vs m/ft */
    if (strstr(src, "km")) {
        src_metric = true;  src_major = true;
    } else if (strstr(src, "mi")) {
        src_metric = false; src_major = true;
    } else if (strstr(src, "ft")) {
        src_metric = false; src_major = false;
    } else if (strstr(src, "yd")) {
        src_metric = false; src_major = false;
        val *= 0.9144;  /* yards → metres */
        src_metric = true;
    } else {
        /* assume metres ("m" or bare number) */
        src_metric = true;  src_major = false;
    }

    if (src_metric == want_metric) {
        snprintf(out, out_sz, "%s", src);
        return;
    }

    /* Convert to base unit (metres or feet) then to target */
    double metres;
    if (src_metric) {
        metres = src_major ? val * 1000.0 : val;
    } else {
        double feet = src_major ? val * 5280.0 : val;
        metres = feet * 0.3048;
    }

    if (want_metric) {
        if (metres >= 1000.0) {
            double km = metres / 1000.0;
            if (km >= 10.0) snprintf(out, out_sz, "%.0f km", km);
            else            snprintf(out, out_sz, "%.1f km", km);
        } else {
            snprintf(out, out_sz, "%.0f m", metres);
        }
    } else {
        double feet = metres / 0.3048;
        if (feet >= 5280.0) {
            double mi = feet / 5280.0;
            if (mi >= 10.0) snprintf(out, out_sz, "%.0f mi", mi);
            else            snprintf(out, out_sz, "%.1f mi", mi);
        } else {
            snprintf(out, out_sz, "%.0f ft", feet);
        }
    }
}

/* ── MDI Navigation icon codepoints (4-byte UTF-8, Plane 15 PUA) ── */
#define NAV_STRAIGHT       "\xF3\xB0\x9C\xB7"   /* U+F0737 arrow-up-bold           */
#define NAV_LEFT           "\xF3\xB0\x9C\xB1"   /* U+F0731 arrow-left-bold         */
#define NAV_RIGHT          "\xF3\xB0\x9C\xB4"   /* U+F0734 arrow-right-bold        */
#define NAV_SLIGHT_LEFT    "\xF3\xB0\x81\x9B"   /* U+F005B arrow-top-left          */
#define NAV_SLIGHT_RIGHT   "\xF3\xB0\x81\x9C"   /* U+F005C arrow-top-right         */
#define NAV_UTURN          "\xF3\xB1\x9E\xB9"   /* U+F17B9 arrow-u-up-left         */
#define NAV_FINISH         "\xF3\xB0\x88\xBC"   /* U+F023C flag-checkered          */
#define NAV_DESTINATION    "\xF3\xB0\x8D\x8E"   /* U+F034E map-marker              */
#define NAV_SHARP_LEFT     "\xF3\xB0\x98\x8C"   /* U+F060C subdirectory-arrow-left */
#define NAV_SHARP_RIGHT    "\xF3\xB0\x98\x8D"   /* U+F060D subdirectory-arrow-right*/
#define NAV_FORK           "\xF3\xB0\x99\x81"   /* U+F0641 directions-fork         */

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_BG       lv_color_make(10, 10, 15)
#define COL_TEXT     lv_color_white()
#define COL_DIM      lv_color_make(100, 100, 110)
#define COL_ARROW    lv_color_make(80, 200, 255)
#define COL_DIST     lv_color_make(255, 220, 80)
#define COL_ETA      lv_color_make(140, 220, 180)
#define COL_NODATA   lv_color_make(80, 80, 90)
#define COL_FINISH   lv_color_make(255, 120, 100)

/* ── UI elements ───────────────────────────────────────────────── */
static lv_obj_t *icon_lbl      = NULL;   /* large direction icon      */
static lv_obj_t *dist_lbl      = NULL;   /* distance to next turn     */
static lv_obj_t *instr_lbl     = NULL;   /* instruction text          */
static lv_obj_t *eta_lbl       = NULL;   /* ETA                       */
static lv_obj_t *nodata_lbl    = NULL;   /* "No navigation" msg       */

static bool has_data = false;
static gb_nav_info_t cached_nav;         /* last received nav data     */

/* ── Map action string to icon + colour ────────────────────────── */
static const char *_action_icon(const char *action) {
    if (!action || !action[0])                  return NAV_STRAIGHT;
    if (strcmp(action, "straight") == 0)         return NAV_STRAIGHT;
    if (strcmp(action, "continue") == 0)         return NAV_STRAIGHT;
    if (strcmp(action, "left") == 0)             return NAV_LEFT;
    if (strcmp(action, "turn-left") == 0)        return NAV_LEFT;
    if (strcmp(action, "sharp-left") == 0)       return NAV_SHARP_LEFT;
    if (strcmp(action, "right") == 0)            return NAV_RIGHT;
    if (strcmp(action, "turn-right") == 0)       return NAV_RIGHT;
    if (strcmp(action, "sharp-right") == 0)      return NAV_SHARP_RIGHT;
    if (strcmp(action, "slight-left") == 0)      return NAV_SLIGHT_LEFT;
    if (strcmp(action, "keep-left") == 0)        return NAV_SLIGHT_LEFT;
    if (strcmp(action, "slight-right") == 0)     return NAV_SLIGHT_RIGHT;
    if (strcmp(action, "keep-right") == 0)       return NAV_SLIGHT_RIGHT;
    if (strcmp(action, "uturn") == 0)            return NAV_UTURN;
    if (strcmp(action, "u-turn") == 0)           return NAV_UTURN;
    if (strcmp(action, "finish") == 0)           return NAV_FINISH;
    if (strcmp(action, "arrive") == 0)           return NAV_DESTINATION;
    if (strcmp(action, "destination") == 0)      return NAV_DESTINATION;
    if (strcmp(action, "fork") == 0)             return NAV_FORK;
    return NAV_STRAIGHT;  /* fallback */
}

static lv_color_t _action_color(const char *action) {
    if (!action || !action[0]) return COL_ARROW;
    if (strcmp(action, "finish") == 0 ||
        strcmp(action, "arrive") == 0 ||
        strcmp(action, "destination") == 0)      return COL_FINISH;
    return COL_ARROW;
}

/* ── Create ────────────────────────────────────────────────────── */
void nav_screen_create(lv_obj_t *parent, int diameter) {
    (void)diameter;
    lv_obj_set_style_bg_color(parent, COL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "NAVIGATION");
    lv_obj_set_style_text_color(title, COL_DIM, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* "No navigation" message */
    nodata_lbl = lv_label_create(parent);
    lv_label_set_text(nodata_lbl, "No active navigation\nStart maps on your phone");
    lv_obj_set_style_text_color(nodata_lbl, COL_NODATA, 0);
    lv_obj_set_style_text_font(nodata_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(nodata_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(nodata_lbl, LV_ALIGN_CENTER, 0, 0);

    /* Direction icon (large MDI glyph) */
    icon_lbl = lv_label_create(parent);
    lv_label_set_text(icon_lbl, "");
    lv_obj_set_style_text_font(icon_lbl, &lv_font_nav, 0);
    lv_obj_set_style_text_color(icon_lbl, COL_ARROW, 0);
    lv_obj_align(icon_lbl, LV_ALIGN_CENTER, 0, -60);
    lv_obj_add_flag(icon_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Distance (large, prominent) */
    dist_lbl = lv_label_create(parent);
    lv_label_set_text(dist_lbl, "");
    lv_obj_set_style_text_font(dist_lbl, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(dist_lbl, COL_DIST, 0);
    lv_obj_align(dist_lbl, LV_ALIGN_CENTER, 0, 15);
    lv_obj_add_flag(dist_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Instruction text (multi-line, wrapping) */
    instr_lbl = lv_label_create(parent);
    lv_label_set_text(instr_lbl, "");
    lv_obj_set_style_text_font(instr_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(instr_lbl, COL_TEXT, 0);
    lv_obj_set_width(instr_lbl, 280);
    lv_obj_set_style_text_align(instr_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(instr_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_align(instr_lbl, LV_ALIGN_CENTER, 0, 55);
    lv_obj_add_flag(instr_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ETA */
    eta_lbl = lv_label_create(parent);
    lv_label_set_text(eta_lbl, "");
    lv_obj_set_style_text_font(eta_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(eta_lbl, COL_ETA, 0);
    lv_obj_align(eta_lbl, LV_ALIGN_CENTER, 0, 110);
    lv_obj_add_flag(eta_lbl, LV_OBJ_FLAG_HIDDEN);
}

/* ── Update ────────────────────────────────────────────────────── */
void nav_screen_update(const gb_nav_info_t *nav) {
    if (!nav) return;
    cached_nav = *nav;

    if (!nav->active) {
        has_data = false;
        if (nodata_lbl)  lv_obj_clear_flag(nodata_lbl, LV_OBJ_FLAG_HIDDEN);
        if (icon_lbl)    lv_obj_add_flag(icon_lbl, LV_OBJ_FLAG_HIDDEN);
        if (dist_lbl)    lv_obj_add_flag(dist_lbl, LV_OBJ_FLAG_HIDDEN);
        if (instr_lbl)   lv_obj_add_flag(instr_lbl, LV_OBJ_FLAG_HIDDEN);
        if (eta_lbl)     lv_obj_add_flag(eta_lbl, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    has_data = true;
    if (nodata_lbl) lv_obj_add_flag(nodata_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Direction icon */
    lv_label_set_text(icon_lbl, _action_icon(nav->action));
    lv_obj_set_style_text_color(icon_lbl, _action_color(nav->action), 0);
    lv_obj_clear_flag(icon_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Distance (converted to user's preferred units) */
    if (nav->distance[0]) {
        char dist_buf[GB_NAV_DIST_MAX];
        _convert_distance(nav->distance, dist_buf, sizeof(dist_buf));
        lv_label_set_text(dist_lbl, dist_buf);
        lv_obj_clear_flag(dist_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(dist_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Instruction */
    if (nav->instr[0]) {
        lv_label_set_text(instr_lbl, nav->instr);
        lv_obj_clear_flag(instr_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(instr_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* ETA */
    if (nav->eta[0]) {
        char eta_buf[48];
        snprintf(eta_buf, sizeof(eta_buf), "ETA %s", nav->eta);
        lv_label_set_text(eta_lbl, eta_buf);
        lv_obj_clear_flag(eta_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(eta_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

void nav_screen_refresh(void) {
    if (has_data) nav_screen_update(&cached_nav);
}
