#pragma once
/*
 *  weather_screen.h – Weather display screen for the tileview
 *
 *  Shows current weather data received from Gadgetbridge:
 *  temperature, condition, humidity, wind, and location.
 */

#include <lvgl.h>
#include "gadgetbridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create weather UI on the given parent (tile). */
void weather_screen_create(lv_obj_t *parent, int diameter);

/* Update displayed weather data. */
void weather_screen_update(const gb_weather_info_t *weather);

/* Re-render with cached data (call after units change). */
void weather_screen_refresh(void);

#ifdef __cplusplus
}
#endif
