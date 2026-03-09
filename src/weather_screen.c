/*
 *  weather_screen.c – Weather display screen (pure LVGL)
 *
 *  Shows weather data from Gadgetbridge: temperature, condition text,
 *  humidity, wind speed, hi/lo temperatures, and location name.
 *  Uses text-based weather icons derived from OWM condition codes.
 */

#include "weather_screen.h"
#include "clock_face.h"
#include "seg7.h"
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

LV_FONT_DECLARE(lv_font_weather);

/* ── Weather Icons font codepoints (UTF-8 encoded) ────────────── */
#define WI_DAY_SUNNY      "\xEF\x80\x8D"   /* U+F00D */
#define WI_DAY_CLOUDY      "\xEF\x80\x82"   /* U+F002 */
#define WI_CLOUDY           "\xEF\x80\x93"   /* U+F013 */
#define WI_RAIN             "\xEF\x80\x99"   /* U+F019 */
#define WI_SPRINKLE         "\xEF\x80\x9C"   /* U+F01C */
#define WI_SNOW             "\xEF\x80\x9B"   /* U+F01B */
#define WI_THUNDERSTORM     "\xEF\x80\x9E"   /* U+F01E */
#define WI_FOG              "\xEF\x80\x94"   /* U+F014 */
#define WI_STRONG_WIND      "\xEF\x81\x90"   /* U+F050 */
#define WI_HUMIDITY         "\xEF\x81\xB2"   /* U+F072 */
#define WI_THERMOMETER      "\xEF\x81\xB6"   /* U+F076 */
#define WI_SUNRISE          "\xEF\x81\x91"   /* U+F051 */
#define WI_SUNSET           "\xEF\x81\x92"   /* U+F052 */
#define WI_MOON_NEW         "\xEF\x83\xAB"   /* U+F0EB */
#define WI_MOON_WAX_CRES    "\xEF\x83\x90"   /* U+F0D0 */
#define WI_MOON_FIRST_Q     "\xEF\x83\x92"   /* U+F0D2 */
#define WI_MOON_WAX_GIB     "\xEF\x83\x94"   /* U+F0D4 */
#define WI_MOON_FULL        "\xEF\x83\x9D"   /* U+F0DD */
#define WI_MOON_WAN_GIB     "\xEF\x83\x9E"   /* U+F0DE */
#define WI_MOON_THIRD_Q     "\xEF\x83\xA0"   /* U+F0E0 */
#define WI_MOON_WAN_CRES    "\xEF\x83\xA2"   /* U+F0E2 */

/* ── Colours ───────────────────────────────────────────────────── */
#define COL_BG       lv_color_make(10, 10, 15)
#define COL_TEXT     lv_color_white()
#define COL_DIM      lv_color_make(100, 100, 110)
#define COL_TEMP     lv_color_make(255, 200, 80)
#define COL_HUM      lv_color_make(80, 180, 255)
#define COL_WIND     lv_color_make(140, 220, 180)
#define COL_HILO     lv_color_make(180, 180, 190)
#define COL_LOC      lv_color_make(160, 160, 170)
#define COL_ICON     lv_color_make(255, 220, 100)
#define COL_NODATA   lv_color_make(80, 80, 90)
#define COL_SUNRISE  lv_color_make(255, 180, 80)
#define COL_SUNSET   lv_color_make(255, 120, 60)
#define COL_MOON     lv_color_make(200, 200, 220)

/* ── UI elements ───────────────────────────────────────────────── */
static lv_obj_t *icon_lbl       = NULL;   /* large weather icon      */
static lv_obj_t *temp_lbl       = NULL;   /* big temperature         */
static lv_obj_t *cond_lbl       = NULL;   /* condition text          */
static lv_obj_t *hilo_lbl       = NULL;   /* hi/lo line              */
static lv_obj_t *hum_icon_lbl   = NULL;
static lv_obj_t *hum_lbl        = NULL;   /* humidity                */
static lv_obj_t *wind_icon_lbl  = NULL;
static lv_obj_t *wind_lbl       = NULL;   /* wind speed              */
static lv_obj_t *loc_lbl        = NULL;   /* location name           */
static lv_obj_t *nodata_lbl     = NULL;   /* "No weather data" msg   */
static lv_obj_t *rise_icon_lbl  = NULL;   /* sunrise icon            */
static lv_obj_t *rise_lbl       = NULL;   /* sunrise time            */
static lv_obj_t *set_icon_lbl   = NULL;   /* sunset icon             */
static lv_obj_t *set_lbl        = NULL;   /* sunset time             */
static lv_obj_t *moon_icon_lbl  = NULL;   /* moon phase icon         */
static lv_obj_t *moon_lbl       = NULL;   /* moon phase name         */

static bool has_data = false;
static gb_weather_info_t last_weather;     /* cached for re-render    */

/* ── Weather icon from OWM code ────────────────────────────────── */
/*  Maps OpenWeatherMap condition codes to LVGL symbols / text.
    OWM codes: https://openweathermap.org/weather-conditions
      2xx = Thunderstorm, 3xx = Drizzle, 5xx = Rain,
      6xx = Snow, 7xx = Atmosphere (fog/mist),
      800 = Clear, 80x = Clouds                            */
static const char *_weather_icon(int code) {
    if (code >= 200 && code < 300) return WI_THUNDERSTORM;
    if (code >= 300 && code < 400) return WI_SPRINKLE;
    if (code >= 500 && code < 600) return WI_RAIN;
    if (code >= 600 && code < 700) return WI_SNOW;
    if (code >= 700 && code < 800) return WI_FOG;
    if (code == 800)               return WI_DAY_SUNNY;
    if (code >= 801 && code <= 802) return WI_DAY_CLOUDY;
    if (code >= 803)               return WI_CLOUDY;
    return WI_DAY_SUNNY;  /* fallback */
}

static lv_color_t _weather_icon_color(int code) {
    if (code >= 200 && code < 300) return lv_color_make(255, 255, 100);  /* yellow - thunder */
    if (code >= 300 && code < 600) return lv_color_make(100, 160, 255);  /* blue - rain */
    if (code >= 600 && code < 700) return lv_color_make(200, 220, 255);  /* light blue - snow */
    if (code >= 700 && code < 800) return lv_color_make(160, 160, 170);  /* grey - fog */
    if (code == 800)               return lv_color_make(255, 220, 80);   /* gold - clear */
    return lv_color_make(190, 195, 200);                                  /* silver - clouds */
}

/* ── Wind direction to compass label ───────────────────────────── */
static const char *_wind_dir_str(int deg) {
    if (deg < 0) return "";
    static const char *dirs[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    int idx = ((deg + 11) / 22) % 16;
    return dirs[idx];
}

/* ── Moon phase from date (Conway's method) ────────────────────── */
/*  Returns 0–7:  0=new, 1=wax-cres, 2=first-q, 3=wax-gib,
                  4=full, 5=wan-gib, 6=third-q, 7=wan-cres     */
static int _moon_phase_idx(int year, int month, int day) {
    /* Simple synodic approximation: days since known new moon
       (Jan 6 2000 18:14 UTC ≈ J2000.0 + 5.76) divided by 29.53059 */
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;
    double jdn = (double)(day + (153 * m + 2) / 5
                 + 365 * y + y / 4 - y / 100 + y / 400 - 32045);
    /* JDN of known new moon: 2451550.1 (Jan 6.6, 2000) */
    double days_since = jdn - 2451550.1;
    double phase = days_since / 29.53059;
    phase -= (int)phase;  /* fractional cycle 0–1 */
    if (phase < 0) phase += 1.0;
    return (int)(phase * 8.0 + 0.5) % 8;
}

static const char *_moon_icon(int idx) {
    static const char *icons[] = {
        WI_MOON_NEW, WI_MOON_WAX_CRES, WI_MOON_FIRST_Q, WI_MOON_WAX_GIB,
        WI_MOON_FULL, WI_MOON_WAN_GIB, WI_MOON_THIRD_Q, WI_MOON_WAN_CRES
    };
    return icons[idx & 7];
}

static const char *_moon_name(int idx) {
    static const char *names[] = {
        "New Moon", "Waxing Crescent", "First Quarter", "Waxing Gibbous",
        "Full Moon", "Waning Gibbous", "Third Quarter", "Waning Crescent"
    };
    return names[idx & 7];
}

/* ── Create ────────────────────────────────────────────────────── */
void weather_screen_create(lv_obj_t *parent, int diameter) {
    (void)diameter;
    lv_obj_set_style_bg_color(parent, COL_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "WEATHER");
    lv_obj_set_style_text_color(title, COL_DIM, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 100);

    /* "No weather data" message (shown until first update) */
    nodata_lbl = lv_label_create(parent);
    lv_label_set_text(nodata_lbl, "No weather data\nConnect via Gadgetbridge");
    lv_obj_set_style_text_color(nodata_lbl, COL_NODATA, 0);
    lv_obj_set_style_text_font(nodata_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(nodata_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(nodata_lbl, LV_ALIGN_CENTER, 0, 0);

    /* ── Weather icon (large) ── */
    icon_lbl = lv_label_create(parent);
    lv_label_set_text(icon_lbl, "");
    lv_obj_set_style_text_font(icon_lbl, &lv_font_weather, 0);
    lv_obj_set_style_text_color(icon_lbl, COL_ICON, 0);
    lv_obj_align(icon_lbl, LV_ALIGN_CENTER, 0, -75);
    lv_obj_add_flag(icon_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Temperature (own line) ── */
    temp_lbl = lv_label_create(parent);
    lv_label_set_text(temp_lbl, "");
    lv_obj_set_style_text_font(temp_lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(temp_lbl, COL_TEMP, 0);
    lv_obj_align(temp_lbl, LV_ALIGN_CENTER, 0, -28);
    lv_obj_add_flag(temp_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Condition text (separate line below temp) ── */
    cond_lbl = lv_label_create(parent);
    lv_label_set_text(cond_lbl, "");
    lv_obj_set_style_text_font(cond_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(cond_lbl, COL_TEXT, 0);
    lv_obj_set_width(cond_lbl, 280);
    lv_obj_set_style_text_align(cond_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(cond_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(cond_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(cond_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Hi/Lo temperatures ── */
    hilo_lbl = lv_label_create(parent);
    lv_label_set_text(hilo_lbl, "");
    lv_obj_set_style_text_font(hilo_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hilo_lbl, COL_HILO, 0);
    lv_obj_align(hilo_lbl, LV_ALIGN_CENTER, 0, 27);
    lv_obj_add_flag(hilo_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Humidity row (icon + text, relative alignment) ── */
    hum_icon_lbl = lv_label_create(parent);
    lv_label_set_text(hum_icon_lbl, WI_HUMIDITY);
    lv_obj_set_style_text_font(hum_icon_lbl, &lv_font_weather, 0);
    lv_obj_set_style_text_color(hum_icon_lbl, COL_HUM, 0);
    lv_obj_align(hum_icon_lbl, LV_ALIGN_CENTER, -110, 55);
    lv_obj_add_flag(hum_icon_lbl, LV_OBJ_FLAG_HIDDEN);

    hum_lbl = lv_label_create(parent);
    lv_label_set_text(hum_lbl, "");
    lv_obj_set_style_text_font(hum_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(hum_lbl, COL_HUM, 0);
    lv_obj_align_to(hum_lbl, hum_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_add_flag(hum_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Wind row (icon + text, relative alignment) ── */
    wind_icon_lbl = lv_label_create(parent);
    lv_label_set_text(wind_icon_lbl, WI_STRONG_WIND);
    lv_obj_set_style_text_font(wind_icon_lbl, &lv_font_weather, 0);
    lv_obj_set_style_text_color(wind_icon_lbl, COL_WIND, 0);
    lv_obj_align(wind_icon_lbl, LV_ALIGN_CENTER, 40, 55);
    lv_obj_add_flag(wind_icon_lbl, LV_OBJ_FLAG_HIDDEN);

    wind_lbl = lv_label_create(parent);
    lv_label_set_text(wind_lbl, "");
    lv_obj_set_style_text_font(wind_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(wind_lbl, COL_WIND, 0);
    lv_obj_align_to(wind_lbl, wind_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_add_flag(wind_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Location ── */
    loc_lbl = lv_label_create(parent);
    lv_label_set_text(loc_lbl, "");
    lv_obj_set_style_text_font(loc_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(loc_lbl, COL_LOC, 0);
    lv_obj_set_width(loc_lbl, 260);
    lv_obj_set_style_text_align(loc_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(loc_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(loc_lbl, LV_ALIGN_CENTER, 0, 85);
    lv_obj_add_flag(loc_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Sunrise / Sunset row (relative alignment) ── */
    rise_icon_lbl = lv_label_create(parent);
    lv_label_set_text(rise_icon_lbl, WI_SUNRISE);
    lv_obj_set_style_text_font(rise_icon_lbl, &lv_font_weather, 0);
    lv_obj_set_style_text_color(rise_icon_lbl, COL_SUNRISE, 0);
    lv_obj_align(rise_icon_lbl, LV_ALIGN_CENTER, -95, 112);
    lv_obj_add_flag(rise_icon_lbl, LV_OBJ_FLAG_HIDDEN);

    rise_lbl = lv_label_create(parent);
    lv_label_set_text(rise_lbl, "");
    lv_obj_set_style_text_font(rise_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rise_lbl, COL_SUNRISE, 0);
    lv_obj_align_to(rise_lbl, rise_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    lv_obj_add_flag(rise_lbl, LV_OBJ_FLAG_HIDDEN);

    set_icon_lbl = lv_label_create(parent);
    lv_label_set_text(set_icon_lbl, WI_SUNSET);
    lv_obj_set_style_text_font(set_icon_lbl, &lv_font_weather, 0);
    lv_obj_set_style_text_color(set_icon_lbl, COL_SUNSET, 0);
    lv_obj_align(set_icon_lbl, LV_ALIGN_CENTER, 25, 112);
    lv_obj_add_flag(set_icon_lbl, LV_OBJ_FLAG_HIDDEN);

    set_lbl = lv_label_create(parent);
    lv_label_set_text(set_lbl, "");
    lv_obj_set_style_text_font(set_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(set_lbl, COL_SUNSET, 0);
    lv_obj_align_to(set_lbl, set_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    lv_obj_add_flag(set_lbl, LV_OBJ_FLAG_HIDDEN);

    /* ── Moon phase (relative alignment) ── */
    moon_icon_lbl = lv_label_create(parent);
    lv_label_set_text(moon_icon_lbl, "");
    lv_obj_set_style_text_font(moon_icon_lbl, &lv_font_weather, 0);
    lv_obj_set_style_text_color(moon_icon_lbl, COL_MOON, 0);
    lv_obj_align(moon_icon_lbl, LV_ALIGN_CENTER, -60, 142);
    lv_obj_add_flag(moon_icon_lbl, LV_OBJ_FLAG_HIDDEN);

    moon_lbl = lv_label_create(parent);
    lv_label_set_text(moon_lbl, "");
    lv_obj_set_style_text_font(moon_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(moon_lbl, COL_MOON, 0);
    lv_obj_align_to(moon_lbl, moon_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_add_flag(moon_lbl, LV_OBJ_FLAG_HIDDEN);
}

/* ── Update ────────────────────────────────────────────────────── */
void weather_screen_update(const gb_weather_info_t *w) {
    if (!w) return;
    last_weather = *w;  /* cache for refresh */
    has_data = true;

    /* Hide "no data" message */
    if (nodata_lbl) lv_obj_add_flag(nodata_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Show all weather elements */
    lv_obj_clear_flag(icon_lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(temp_lbl, LV_OBJ_FLAG_HIDDEN);

    /* Weather icon */
    lv_label_set_text(icon_lbl, _weather_icon(w->code));
    lv_obj_set_style_text_color(icon_lbl, _weather_icon_color(w->code), 0);

    /* Temperature */
    {
        char tbuf[16];
        if (clock_face_get_metric()) {
            snprintf(tbuf, sizeof(tbuf), "%d" "\xC2\xB0" "C", w->temp);
        } else {
            int f = w->temp * 9 / 5 + 32;
            snprintf(tbuf, sizeof(tbuf), "%d" "\xC2\xB0" "F", f);
        }
        lv_label_set_text(temp_lbl, tbuf);
    }

    /* Condition text */
    {
        const char *cond = w->txt[0] ? w->txt : "Unknown";
        lv_label_set_text(cond_lbl, cond);
        lv_obj_clear_flag(cond_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Hi/Lo */
    if (w->temp_min != 0 || w->temp_max != 0) {
        char hilo[32];
        if (clock_face_get_metric()) {
            snprintf(hilo, sizeof(hilo), "%d" "\xC2\xB0" " / %d" "\xC2\xB0", w->temp_min, w->temp_max);
        } else {
            int fmin = w->temp_min * 9 / 5 + 32;
            int fmax = w->temp_max * 9 / 5 + 32;
            snprintf(hilo, sizeof(hilo), "%d" "\xC2\xB0" " / %d" "\xC2\xB0", fmin, fmax);
        }
        lv_label_set_text(hilo_lbl, hilo);
        lv_obj_clear_flag(hilo_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(hilo_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Humidity */
    if (w->humidity > 0) {
        char hum[16];
        snprintf(hum, sizeof(hum), "%d%%", w->humidity);
        lv_label_set_text(hum_lbl, hum);
        lv_obj_clear_flag(hum_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(hum_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(hum_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hum_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Wind */
    if (w->wind > 0) {
        char wnd[32];
        const char *dir = _wind_dir_str(w->wind_dir);
        if (clock_face_get_metric()) {
            snprintf(wnd, sizeof(wnd), "%d km/h %s", w->wind, dir);
        } else {
            int mph = w->wind * 10 / 16;  /* km/h to mph approx */
            snprintf(wnd, sizeof(wnd), "%d mph %s", mph, dir);
        }
        lv_label_set_text(wind_lbl, wnd);
        lv_obj_clear_flag(wind_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wind_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(wind_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wind_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Location */
    if (w->location[0]) {
        lv_label_set_text(loc_lbl, w->location);
        lv_obj_clear_flag(loc_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(loc_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Sunrise / Sunset */
    if (w->sunrise >= 0 && w->sunset >= 0) {
        int rh = w->sunrise / 3600, rm = (w->sunrise % 3600) / 60;
        int sh = w->sunset  / 3600, sm = (w->sunset  % 3600) / 60;
        char rb[8], sb[8];
        if (clock_face_get_24h()) {
            snprintf(rb, sizeof(rb), "%02d:%02d", rh, rm);
            snprintf(sb, sizeof(sb), "%02d:%02d", sh, sm);
        } else {
            int rh12 = rh % 12; if (rh12 == 0) rh12 = 12;
            int sh12 = sh % 12; if (sh12 == 0) sh12 = 12;
            snprintf(rb, sizeof(rb), "%d:%02d%s", rh12, rm, rh < 12 ? "a" : "p");
            snprintf(sb, sizeof(sb), "%d:%02d%s", sh12, sm, sh < 12 ? "a" : "p");
        }
        lv_label_set_text(rise_lbl, rb);
        lv_label_set_text(set_lbl, sb);
        lv_obj_clear_flag(rise_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(rise_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(set_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(set_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(rise_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(rise_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(set_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(set_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Moon phase (computed from current date) */
    {
        #ifdef ARDUINO
        /* Use system time on ESP32 */
        extern int cur_hour;
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        int mi = _moon_phase_idx(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
        #else
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        int mi = _moon_phase_idx(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
        #endif
        lv_label_set_text(moon_icon_lbl, _moon_icon(mi));
        lv_label_set_text(moon_lbl, _moon_name(mi));
        lv_obj_clear_flag(moon_icon_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(moon_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    /* Re-align text labels relative to their icons after content changes */
    lv_obj_update_layout(lv_obj_get_parent(icon_lbl));
    lv_obj_align_to(hum_lbl, hum_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_align_to(wind_lbl, wind_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_align_to(rise_lbl, rise_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    lv_obj_align_to(set_lbl, set_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 2, 0);
    lv_obj_align_to(moon_lbl, moon_icon_lbl, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
}

/* ── Refresh (re-render with cached data after settings change) ── */
void weather_screen_refresh(void) {
    if (has_data) weather_screen_update(&last_weather);
}
