#pragma once
/*
 *  seg7.h – 7-segment display widget for LVGL
 *
 *  Renders digits (0–9), colon, dot, dash, and space using lv_line
 *  objects, giving a classic LED / VFD look.
 */

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle returned by seg7_create */
typedef struct seg7_display seg7_display_t;

/*
 * Create a 7-segment display with `num_chars` character slots.
 * `parent`     – LVGL parent object
 * `num_chars`  – max characters the display can show
 * `digit_h`    – total height of one digit in pixels (width = digit_h * 0.55)
 * `color`      – segment colour
 * `thickness`  – segment line thickness in pixels
 *
 * Returns an opaque handle; the widget is an lv_obj (container) that can be
 * positioned/aligned normally via seg7_get_obj().
 */
seg7_display_t *seg7_create(lv_obj_t *parent, int num_chars,
                            int digit_h, lv_color_t color, int thickness);

/* Get the underlying lv_obj_t* for alignment / positioning */
lv_obj_t *seg7_get_obj(seg7_display_t *d);

/* Set the text to display.  Supported chars: 0-9 : . - (space)
   If the string is shorter than num_chars, it is right-justified.
   If longer, it is truncated from the left. */
void seg7_set_text(seg7_display_t *d, const char *text);

/* Change the segment colour dynamically */
void seg7_set_color(seg7_display_t *d, lv_color_t color);

#ifdef __cplusplus
}
#endif
