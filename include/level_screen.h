#pragma once
/*
 *  level_screen.h – Spirit level / inclinometer using QMI8658 IMU
 *
 *  Renders a circular bubble level.  The "bubble" moves opposite to
 *  the device tilt, like a real spirit level.  Concentric rings
 *  mark 5° / 10° / 15° tilt increments, with a crosshair for centre.
 *  Pitch and roll angles are displayed as numeric readouts.
 */

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create level UI on the given tile parent. */
void level_screen_create(lv_obj_t *parent, int diameter);

/* Feed accelerometer data every frame (~20-60 Hz).
   ax/ay/az in g-units. */
void level_screen_feed_imu(float ax, float ay, float az);

#ifdef __cplusplus
}
#endif
