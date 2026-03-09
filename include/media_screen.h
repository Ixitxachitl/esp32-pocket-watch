#pragma once
/*
 *  media_screen.h – Media / music control screen
 *
 *  Displays current track info (artist, title, album) from
 *  Gadgetbridge and provides play/pause, next, previous controls.
 */

#include <lvgl.h>
#include "gadgetbridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create media screen UI on the given tile parent. */
void media_screen_create(lv_obj_t *parent, int diameter);

/* Update displayed track info (called from GB music callback). */
void media_screen_update(const gb_music_info_t *info);

#ifdef __cplusplus
}
#endif
