#pragma once
/*
 *  stopwatch.h – Stopwatch screen for the tileview
 */

#include <lvgl.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create stopwatch UI on the given parent (tile). */
void stopwatch_create(lv_obj_t *parent, int diameter);

/* Call every frame – updates running display. */
void stopwatch_loop(void);

/* Get/set state for persistence. */
uint32_t stopwatch_get_elapsed_ms(void);
void     stopwatch_set_elapsed_ms(uint32_t ms);

/* Register callback fired when stopwatch is paused or reset. */
void stopwatch_set_changed_cb(void (*cb)(void));

#ifdef __cplusplus
}
#endif
