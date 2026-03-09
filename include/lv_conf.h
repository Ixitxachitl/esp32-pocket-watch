/**
 * lv_conf.h – LVGL 8.4 configuration for ESP32-S3-Touch-AMOLED-1.75C
 *
 * Minimal config: 16-bit colour, 466×466, tick via esp_timer.
 * Also used by the native/SDL simulator build.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* ── Colour depth ───────────────────────────────────────────────── */
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   1    /* LVGL pre-swaps bytes; use BeRGB flush */

/* ── Memory ─────────────────────────────────────────────────────── */
#ifdef ARDUINO
  /* Route all LVGL allocations to PSRAM so that internal SRAM stays
     free for WiFi/BLE/TCP stacks that *require* internal memory.
     PSRAM is plenty fast for widget trees, styles, etc. */
  #define LV_MEM_CUSTOM            1
  #define LV_MEM_CUSTOM_INCLUDE    "esp_heap_caps.h"
  #define LV_MEM_CUSTOM_ALLOC(size)          heap_caps_malloc(size, MALLOC_CAP_SPIRAM)
  #define LV_MEM_CUSTOM_FREE                 heap_caps_free
  #define LV_MEM_CUSTOM_REALLOC(ptr, size)   heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM)
#else
  /* Native / simulator – built-in pool is fine */
  #define LV_MEM_CUSTOM      0
  #define LV_MEM_SIZE        (1024U * 1024U)
#endif

/* ── Display ────────────────────────────────────────────────────── */
#define LV_HOR_RES_MAX     466
#define LV_VER_RES_MAX     466
#define LV_DPI_DEF         200

/* ── Tick ───────────────────────────────────────────────────────── */
#define LV_DISP_DEF_REFR_PERIOD 16   /* ~60 FPS */
#define LV_TICK_CUSTOM      1
#ifdef ARDUINO
  #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR ((uint32_t)millis())
#else
  /* Native / SDL build */
  #include <stdint.h>
  uint32_t lv_sdl_tick_get(void);
  #define LV_TICK_CUSTOM_INCLUDE <stdint.h>
  #define LV_TICK_CUSTOM_SYS_TIME_EXPR (lv_sdl_tick_get())
#endif

/* ── Logging ────────────────────────────────────────────────────── */
#ifdef SIMULATOR
  #define LV_USE_LOG         1
  #define LV_LOG_LEVEL       LV_LOG_LEVEL_WARN
  #define LV_LOG_PRINTF      1
#else
  #define LV_USE_LOG         0
#endif

/* ── GPU / draw ─────────────────────────────────────────────────── */
#define LV_USE_GPU_STM32_DMA2D    0
#define LV_USE_GPU_NXP_PXP        0
#define LV_USE_GPU_NXP_VG_LITE    0
#define LV_USE_GPU_SDL            0

/* ── Fonts ──────────────────────────────────────────────────────── */
#define LV_FONT_MONTSERRAT_14     1
#define LV_FONT_MONTSERRAT_16     0
#define LV_FONT_MONTSERRAT_20     1
#define LV_FONT_MONTSERRAT_24     1
#define LV_FONT_MONTSERRAT_28     1
#define LV_FONT_MONTSERRAT_36     1
#define LV_FONT_DEFAULT           &lv_font_montserrat_20

/* ── Widgets ────────────────────────────────────────────────────── */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      0
#define LV_USE_METER      1   /* needed for the analogue gauge/clock face */

/* ── Extra widgets ──────────────────────────────────────────────── */
#define LV_USE_ANIMIMG    0
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   0
#define LV_USE_LED        0
#define LV_USE_LIST       0
#define LV_USE_MENU       0
#define LV_USE_MSGBOX     1
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    0
#define LV_USE_TABVIEW    0
#define LV_USE_TILEVIEW   1
#define LV_USE_WIN        0

/* ── Themes ─────────────────────────────────────────────────────── */
#define LV_USE_THEME_DEFAULT   1
#define LV_THEME_DEFAULT_DARK  1
#define LV_USE_THEME_BASIC     1
#define LV_USE_THEME_MONO      0

/* ── Demos (disabled) ───────────────────────────────────────────── */
#define LV_USE_DEMO_WIDGETS       0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0
#define LV_USE_DEMO_BENCHMARK     0
#define LV_USE_DEMO_STRESS        0
#define LV_USE_DEMO_MUSIC         0

/* ── File system (disabled) ─────────────────────────────────────── */
#define LV_USE_FS_STDIO    0
#define LV_USE_FS_POSIX    0
#define LV_USE_FS_WIN32    0
#define LV_USE_FS_FATFS    0

/* ── Others ─────────────────────────────────────────────────────── */
#define LV_USE_SNAPSHOT    1
#define LV_BUILD_EXAMPLES  0

#endif /* LV_CONF_H */
