#pragma once
/*
 *  compass_screen.h – 3D compass display using QMI8658 IMU
 *
 *  Integrates gyroscope yaw for heading and uses accelerometer
 *  for tilt-based 3D perspective.  Long-press sets current
 *  direction as "north" (no magnetometer on board).
 */

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create compass UI on the given tile parent. */
void compass_screen_create(lv_obj_t *parent, int diameter);

/* Feed IMU data every frame (~20-60 Hz).
   ax/ay/az in g,  gx/gy/gz in degrees/sec,  dt in seconds. */
void compass_screen_feed_imu(float ax, float ay, float az,
                              float gx, float gy, float gz, float dt);

/* Programmatically set north to current heading (same as long-press). */
void compass_screen_set_north(void);

/* Set raw heading directly (for simulator use). */
void compass_screen_set_heading(float deg);

#ifdef __cplusplus
}
#endif
