/*
 *  seg7.c – 7-segment display widget for LVGL
 *
 *  Each digit is drawn with 7 lv_line objects (segments a–g).
 *  Colons, dots, dashes, and spaces are also supported.
 *
 *  Segment layout (classic 7-seg):
 *
 *      aaaa
 *     f    b
 *     f    b
 *      gggg
 *     e    c
 *     e    c
 *      dddd
 */

#include "seg7.h"
#include <stdlib.h>
#include <string.h>

/* ── Segment bit masks ─────────────────────────────────────────── */
/*       a     b     c     d     e     f     g                     */
#define SA 0x01
#define SB 0x02
#define SC 0x04
#define SD 0x08
#define SE 0x10
#define SF 0x20
#define SG 0x40

static const uint8_t digit_map[10] = {
    /* 0 */ SA|SB|SC|SD|SE|SF,
    /* 1 */ SB|SC,
    /* 2 */ SA|SB|SD|SE|SG,
    /* 3 */ SA|SB|SC|SD|SG,
    /* 4 */ SB|SC|SF|SG,
    /* 5 */ SA|SC|SD|SF|SG,
    /* 6 */ SA|SC|SD|SE|SF|SG,
    /* 7 */ SA|SB|SC,
    /* 8 */ SA|SB|SC|SD|SE|SF|SG,
    /* 9 */ SA|SB|SC|SD|SF|SG,
};

/* ── Character slot ────────────────────────────────────────────── */
#define SEGS_PER_CHAR 7

typedef struct {
    lv_obj_t     *lines[SEGS_PER_CHAR];   /* a–g segment lines     */
    lv_point_t    pts[SEGS_PER_CHAR][2];   /* 2 endpoints per seg   */
    lv_obj_t     *colon_dots[2];           /* for ':' – two circles */
    lv_obj_t     *dot_obj;                 /* for '.' – one circle  */
} char_slot_t;

struct seg7_display {
    lv_obj_t    *cont;       /* container object              */
    int          num_chars;
    int          digit_h;    /* height in px                  */
    int          digit_w;    /* width = digit_h * 0.55        */
    int          thickness;
    lv_color_t   color;
    char_slot_t *slots;
    char         last_text[16]; /* cache for change detection */
};

/* ── Dim / off colour (faint ghost segments) ───────────────────── */
static lv_color_t seg_off_color(void) {
    return lv_color_make(25, 25, 30);
}

/* ── Compute segment endpoints for a digit at local origin ─────── */
static void _calc_seg_pts(int w, int h, int th, int seg_idx,
                          lv_point_t out[2]) {
    int m = th / 2;   /* margin so segments don't overlap at corners */
    int hh = h / 2;
    switch (seg_idx) {
        case 0: /* a – top horizontal */
            out[0] = (lv_point_t){m, 0};
            out[1] = (lv_point_t){w - m, 0};
            break;
        case 1: /* b – top-right vertical */
            out[0] = (lv_point_t){w, m};
            out[1] = (lv_point_t){w, hh - m};
            break;
        case 2: /* c – bottom-right vertical */
            out[0] = (lv_point_t){w, hh + m};
            out[1] = (lv_point_t){w, h - m};
            break;
        case 3: /* d – bottom horizontal */
            out[0] = (lv_point_t){m, h};
            out[1] = (lv_point_t){w - m, h};
            break;
        case 4: /* e – bottom-left vertical */
            out[0] = (lv_point_t){0, hh + m};
            out[1] = (lv_point_t){0, h - m};
            break;
        case 5: /* f – top-left vertical */
            out[0] = (lv_point_t){0, m};
            out[1] = (lv_point_t){0, hh - m};
            break;
        case 6: /* g – middle horizontal */
            out[0] = (lv_point_t){m, hh};
            out[1] = (lv_point_t){w - m, hh};
            break;
    }
}

/* ── Create ────────────────────────────────────────────────────── */
seg7_display_t *seg7_create(lv_obj_t *parent, int num_chars,
                            int digit_h, lv_color_t color, int thickness) {
    seg7_display_t *d = (seg7_display_t *)lv_mem_alloc(sizeof(seg7_display_t));
    if (!d) return NULL;
    memset(d, 0, sizeof(*d));

    d->num_chars = num_chars;
    d->digit_h   = digit_h;
    d->digit_w   = (int)(digit_h * 0.55f);
    d->thickness  = thickness;
    d->color      = color;

    /* Spacing between characters */
    int char_pitch = d->digit_w + thickness * 2 + 2;
    int narrow     = thickness * 3;  /* width for colon / dot */
    /* Estimate total width (will be accurate after first set_text) */
    int total_w = num_chars * char_pitch;

    int margin = thickness / 2 + 1;  /* prevent line clipping at edges */

    d->cont = lv_obj_create(parent);
    lv_obj_set_size(d->cont, total_w + margin * 2, digit_h + thickness + margin);
    lv_obj_set_style_bg_opa(d->cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(d->cont, 0, 0);
    lv_obj_set_style_pad_all(d->cont, 0, 0);
    lv_obj_clear_flag(d->cont, LV_OBJ_FLAG_SCROLLABLE);

    d->slots = (char_slot_t *)lv_mem_alloc(sizeof(char_slot_t) * num_chars);
    if (!d->slots) return d;
    memset(d->slots, 0, sizeof(char_slot_t) * num_chars);

    lv_color_t off = seg_off_color();

    for (int ch = 0; ch < num_chars; ch++) {
        char_slot_t *s = &d->slots[ch];

        /* Create 7 segment lines */
        for (int seg = 0; seg < SEGS_PER_CHAR; seg++) {
            _calc_seg_pts(d->digit_w, d->digit_h, thickness, seg,
                          s->pts[seg]);

            s->lines[seg] = lv_line_create(d->cont);
            lv_line_set_points(s->lines[seg], s->pts[seg], 2);
            lv_obj_set_style_line_width(s->lines[seg], thickness, 0);
            lv_obj_set_style_line_rounded(s->lines[seg], true, 0);
            lv_obj_set_style_line_color(s->lines[seg], off, 0);
            lv_obj_clear_flag(s->lines[seg], LV_OBJ_FLAG_CLICKABLE);
        }

        /* Colon dots (hidden by default) */
        for (int ci = 0; ci < 2; ci++) {
            s->colon_dots[ci] = lv_obj_create(d->cont);
            lv_obj_set_size(s->colon_dots[ci], thickness + 1, thickness + 1);
            lv_obj_set_style_radius(s->colon_dots[ci], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(s->colon_dots[ci], color, 0);
            lv_obj_set_style_bg_opa(s->colon_dots[ci], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(s->colon_dots[ci], 0, 0);
            lv_obj_set_style_pad_all(s->colon_dots[ci], 0, 0);
            lv_obj_clear_flag(s->colon_dots[ci], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(s->colon_dots[ci], LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(s->colon_dots[ci], LV_OBJ_FLAG_HIDDEN);
        }

        /* Decimal dot (hidden by default) */
        s->dot_obj = lv_obj_create(d->cont);
        lv_obj_set_size(s->dot_obj, thickness + 1, thickness + 1);
        lv_obj_set_style_radius(s->dot_obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s->dot_obj, color, 0);
        lv_obj_set_style_bg_opa(s->dot_obj, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s->dot_obj, 0, 0);
        lv_obj_set_style_pad_all(s->dot_obj, 0, 0);
        lv_obj_clear_flag(s->dot_obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(s->dot_obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s->dot_obj, LV_OBJ_FLAG_HIDDEN);
    }

    return d;
}

lv_obj_t *seg7_get_obj(seg7_display_t *d) {
    return d ? d->cont : NULL;
}

/* ── Set text ──────────────────────────────────────────────────── */
void seg7_set_text(seg7_display_t *d, const char *text) {
    if (!d || !d->slots) return;

    /* Skip if text hasn't changed */
    if (strcmp(d->last_text, text) == 0) return;
    strncpy(d->last_text, text, sizeof(d->last_text) - 1);
    d->last_text[sizeof(d->last_text) - 1] = '\0';

    int len = (int)strlen(text);
    lv_color_t off = seg_off_color();
    int th = d->thickness;
    int dw = d->digit_w;
    int dh = d->digit_h;
    int char_pitch = dw + th * 2 + 2;
    int narrow = th * 3 + 2;
    int margin = th / 2 + 1;  /* prevent line clipping at edges */

    /* First pass: compute total width */
    int total_w = 0;
    for (int i = 0; i < len && i < d->num_chars; i++) {
        char c = text[i];
        if (c == ':' || c == '.') total_w += narrow;
        else total_w += char_pitch;
    }
    lv_obj_set_width(d->cont, (total_w > 0 ? total_w : char_pitch) + margin * 2);

    /* Position cursor – start with margin so left edge isn't clipped */
    int x = margin;

    for (int i = 0; i < d->num_chars; i++) {
        char_slot_t *s = &d->slots[i];
        char c = (i < len) ? text[i] : ' ';

        /* Hide everything first */
        for (int seg = 0; seg < SEGS_PER_CHAR; seg++)
            lv_obj_add_flag(s->lines[seg], LV_OBJ_FLAG_HIDDEN);
        for (int ci = 0; ci < 2; ci++)
            lv_obj_add_flag(s->colon_dots[ci], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s->dot_obj, LV_OBJ_FLAG_HIDDEN);

        if (i >= len) continue;  /* unused slot */

        if (c >= '0' && c <= '9') {
            uint8_t mask = digit_map[c - '0'];
            for (int seg = 0; seg < SEGS_PER_CHAR; seg++) {
                lv_obj_clear_flag(s->lines[seg], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(s->lines[seg], x, th / 2);
                bool on = (mask >> seg) & 1;
                lv_obj_set_style_line_color(s->lines[seg],
                    on ? d->color : off, 0);
            }
            x += char_pitch;
        }
        else if (c == ':') {
            int cx = x + narrow / 2 - (th + 1) / 2;
            int cy1 = dh / 3;
            int cy2 = dh * 2 / 3;
            lv_obj_clear_flag(s->colon_dots[0], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s->colon_dots[1], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s->colon_dots[0], cx, cy1);
            lv_obj_set_pos(s->colon_dots[1], cx, cy2);
            x += narrow;
        }
        else if (c == '.') {
            int cx = x + narrow / 2 - (th + 1) / 2;
            lv_obj_clear_flag(s->dot_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s->dot_obj, cx, dh);
            x += narrow;
        }
        else if (c == '-') {
            /* Show only the middle segment (g) */
            lv_obj_clear_flag(s->lines[6], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s->lines[6], x, th / 2);
            lv_obj_set_style_line_color(s->lines[6], d->color, 0);
            x += char_pitch;
        }
        else {
            /* Space or unknown: leave hidden, advance */
            x += char_pitch;
        }
    }

    /* Force full container redraw to clear ghost segments after format change */
    lv_obj_invalidate(d->cont);
}

/* ── Set colour ────────────────────────────────────────────────── */
void seg7_set_color(seg7_display_t *d, lv_color_t color) {
    if (!d) return;
    d->color = color;
    /* Update colon and dot colours immediately */
    for (int i = 0; i < d->num_chars; i++) {
        char_slot_t *s = &d->slots[i];
        for (int ci = 0; ci < 2; ci++)
            lv_obj_set_style_bg_color(s->colon_dots[ci], color, 0);
        lv_obj_set_style_bg_color(s->dot_obj, color, 0);
    }
    /* Segment colours are updated on next seg7_set_text call */
    d->last_text[0] = '\0';  /* invalidate cache so set_text re-applies */
}
