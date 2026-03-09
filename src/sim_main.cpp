/*
 *  sim_main.cpp – Native SDL2 simulator entry point
 *
 *  Renders the LVGL analogue clock in a 466×466 SDL window.
 *  No WiFi, no RTC, no Arduino – just the clock face ticking
 *  with the host system clock.
 *
 *  Build:  pio run -e simulator
 *  Run:    .pio/build/simulator/program
 *
 *  Prerequisites: SDL2 development libraries installed.
 *    Windows (MSYS2):  pacman -S mingw-w64-x86_64-SDL2
 *    Windows (vcpkg):  vcpkg install sdl2:x64-windows
 *    Linux:            sudo apt install libsdl2-dev
 *    macOS:            brew install sdl2
 */

#ifdef SIMULATOR   /* only compiled for the native/simulator environment */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <lvgl.h>
#include <SDL.h>
#include "clock_face.h"
#include "screen_manager.h"
#include "gadgetbridge.h"
#include "sound.h"
#include "alarm_screen.h"
#include "timer_screen.h"
#include "stopwatch.h"
#include "weather_screen.h"
#include "radar_screen.h"
#include "compass_screen.h"
#include "media_screen.h"
#include "level_screen.h"
#include "sysinfo_screen.h"
#include "nav_screen.h"

#define HOR_RES 466
#define VER_RES 466
#define SIM_SETTINGS_FILE "sim_settings.ini"

/* ── SDL window / renderer / texture ────────────────────────────────────── */
static SDL_Window   *sdl_window   = NULL;
static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture  *sdl_texture  = NULL;

/* ── Pixel buffer (LVGL draws here) ─────────────────────────────────────── */
static uint32_t framebuf[HOR_RES * VER_RES];

/* ── Simulated state ────────────────────────────────────────────────────── */
static int mouse_x = 0, mouse_y = 0;
static bool mouse_pressed = false;
static uint32_t sim_steps = 0;
static bool sim_wifi = true;
static bool sim_bt   = true;
static bool sim_screen_on = true;

/* ── Tick helper (used by lv_conf.h when !ARDUINO) ──────────────────────── */
extern "C" uint32_t lv_sdl_tick_get(void) {
    return (uint32_t)SDL_GetTicks();
}

/* ── SDL mouse → LVGL input device ──────────────────────────────────────── */
static void sdl_mouse_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    data->point.x = mouse_x < 0 ? 0 : (mouse_x >= HOR_RES ? HOR_RES - 1 : mouse_x);
    data->point.y = mouse_y < 0 ? 0 : (mouse_y >= VER_RES ? VER_RES - 1 : mouse_y);
    data->state = mouse_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}
/* ── Simulator settings persistence ───────────────────────────────────────────── */
static void sim_save_settings(void) {
    FILE *f = fopen(SIM_SETTINGS_FILE, "w");
    if (!f) return;
    fprintf(f, "wifi=%d\n", sim_wifi ? 1 : 0);
    fprintf(f, "bt=%d\n",   sim_bt   ? 1 : 0);
    fprintf(f, "timeout_idx=%d\n", clock_face_get_timeout_idx());
    fprintf(f, "time_24h=%d\n", clock_face_get_24h() ? 1 : 0);
    fprintf(f, "orrery_on=%d\n", clock_face_get_orrery() ? 1 : 0);
    fprintf(f, "metric=%d\n", clock_face_get_metric() ? 1 : 0);
    fprintf(f, "alarm_hr=%d\n", alarm_screen_get_hour());
    fprintf(f, "alarm_mn=%d\n", alarm_screen_get_min());
    fprintf(f, "alarm_en=%d\n", alarm_screen_get_enabled() ? 1 : 0);
    fprintf(f, "tmr_hr=%d\n",  timer_screen_get_set_hr());
    fprintf(f, "tmr_min=%d\n", timer_screen_get_set_min());
    fprintf(f, "tmr_sec=%d\n", timer_screen_get_set_sec());
    fprintf(f, "tmr_rem=%u\n", (unsigned)timer_screen_get_remaining_ms());
    fprintf(f, "sw_ms=%u\n", (unsigned)stopwatch_get_elapsed_ms());
    fclose(f);
    printf("Settings saved to %s\n", SIM_SETTINGS_FILE);
}

static void sim_load_settings(void) {
    FILE *f = fopen(SIM_SETTINGS_FILE, "r");
    if (!f) return;
    char line[64];
    int alarm_hr = 7, alarm_mn = 0, alarm_en = 0;
    int tmr_hr = 0, tmr_min = 5, tmr_sec = 0;
    unsigned tmr_rem = 0;
    unsigned sw_ms = 0;
    while (fgets(line, sizeof(line), f)) {
        int val;
        unsigned uval;
        if (sscanf(line, "wifi=%d", &val) == 1) sim_wifi = (val != 0);
        if (sscanf(line, "bt=%d",   &val) == 1) sim_bt   = (val != 0);
        if (sscanf(line, "timeout_idx=%d", &val) == 1) {
            if (val >= 0 && val <= 3) clock_face_set_timeout_idx(val);
        }
        if (sscanf(line, "time_24h=%d", &val) == 1) clock_face_set_24h(val != 0);
        if (sscanf(line, "orrery_on=%d", &val) == 1) clock_face_set_orrery(val != 0);
        if (sscanf(line, "metric=%d", &val) == 1) clock_face_set_metric(val != 0);
        if (sscanf(line, "alarm_hr=%d", &val) == 1) alarm_hr = val;
        if (sscanf(line, "alarm_mn=%d", &val) == 1) alarm_mn = val;
        if (sscanf(line, "alarm_en=%d", &val) == 1) alarm_en = val;
        if (sscanf(line, "tmr_hr=%d",  &val) == 1) tmr_hr  = val;
        if (sscanf(line, "tmr_min=%d", &val) == 1) tmr_min = val;
        if (sscanf(line, "tmr_sec=%d", &val) == 1) tmr_sec = val;
        if (sscanf(line, "tmr_rem=%u", &uval) == 1) tmr_rem = uval;
        if (sscanf(line, "sw_ms=%u", &uval) == 1) sw_ms = uval;
    }
    fclose(f);
    alarm_screen_set_alarm(alarm_hr, alarm_mn, alarm_en != 0);
    timer_screen_set_preset(tmr_hr, tmr_min, tmr_sec);
    timer_screen_set_remaining_ms((uint32_t)tmr_rem);
    stopwatch_set_elapsed_ms((uint32_t)sw_ms);
    printf("Settings loaded from %s\n", SIM_SETTINGS_FILE);
}
/* ── Gadgetbridge callbacks (simulator) ────────────────────────────────────── */
static void on_gb_time_set(int year, int month, int day,
                           int hour, int min, int sec) {
    printf("GB time sync: %04d-%02d-%02d %02d:%02d:%02d\n",
           year, month, day, hour, min, sec);
    /* Simulator ignores time set – uses host clock */
}

static void on_gb_notification(const gb_notification_t *n) {
    printf("on_gb_notification called: src=%s title=%s body=%s\n", n->src, n->title, n->body);
    clock_face_show_notification(n->title, n->body, n->src);
    sound_play_ding();
    printf("clock_face_show_notification returned\n");
}

static void on_gb_notif_dismiss(uint32_t id) {
    printf("GB dismiss notif %u\n", id);
    clock_face_dismiss_notification();
}

static void on_gb_music(const gb_music_info_t *m) {
    printf("GB music: %s - %s (%s)\n",
           m->artist, m->track, m->playing ? "play" : "pause");
    media_screen_update(m);
}

static void on_gb_call(const char *cmd, const char *name, const char *number) {
    printf("GB call %s: %s (%s)\n", cmd, name, number);
}

static void on_gb_find(bool start) {
    printf("GB find: %s\n", start ? "START" : "STOP");
}

static void on_gb_weather(const gb_weather_info_t *w) {
    printf("GB weather: %d C %s (code %d) hum=%d%% wind=%d\n",
           w->temp, w->txt, w->code, w->humidity, w->wind);
    weather_screen_update(w);
}

static void on_gb_nav(const gb_nav_info_t *nav) {
    printf("GB nav: %s %s %s (eta %s) active=%d\n",
           nav->action, nav->distance, nav->instr,
           nav->eta, nav->active);
    nav_screen_update(nav);
}

static const gb_callbacks_t gb_cbs = {
    .on_time_set     = on_gb_time_set,
    .on_notification = on_gb_notification,
    .on_notif_dismiss= on_gb_notif_dismiss,
    .on_music        = on_gb_music,
    .on_call         = on_gb_call,
    .on_find         = on_gb_find,
    .on_weather      = on_gb_weather,
    .on_nav          = on_gb_nav,
};

/* ── Menu callbacks (simulator) ─────────────────────────────────────────── */
static void on_wifi_toggle(bool enable) {
    sim_wifi = enable;
    printf("WiFi %s (simulated)\n", enable ? "ON" : "OFF");
    clock_face_set_wifi_state(enable);
}

static void on_bt_toggle(bool enable) {
    sim_bt = enable;
    printf("Bluetooth %s (simulated)\n", enable ? "ON" : "OFF");
    if (enable) {
        gb_init("Pocket Watch", &gb_cbs);
        gb_send_firmware_info("1.0.0", "SIM");
    } else {
        gb_deinit();
    }
    clock_face_set_bt_state(false);
}

static void on_reset_steps(void) {
    sim_steps = 0;
    printf("Steps reset (simulated)\n");
    clock_face_set_steps(0);
}

static void on_screen_power(bool on) {
    sim_screen_on = on;
    printf("Screen %s (simulated)\n", on ? "ON" : "OFF");
    SDL_SetWindowTitle(sdl_window, on ? "Pocket Watch Simulator"
                                      : "Pocket Watch Simulator [SCREEN OFF]");
    if (!on) {
        /* Black out the window immediately */
        memset(framebuf, 0, sizeof(framebuf));
        SDL_UpdateTexture(sdl_texture, NULL, framebuf, HOR_RES * sizeof(uint32_t));
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
        SDL_RenderPresent(sdl_renderer);
    }
}

static void on_settings_changed(void) {
    sim_save_settings();
}

/* ── LVGL flush: copy pixels to framebuffer, blit when complete ─────────── */
static void sdl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_p) {
    /* When screen is off, skip rendering to keep the display black */
    if (!sim_screen_on) {
        lv_disp_flush_ready(drv);
        return;
    }

    int32_t x, y;
    for (y = area->y1; y <= area->y2; y++) {
        for (x = area->x1; x <= area->x2; x++) {
            /* Convert lv_color_t (RGB565) to ARGB8888 */
            uint8_t r = (color_p->ch.red     << 3) | (color_p->ch.red     >> 2);
            uint8_t g6 = (color_p->ch.green_h << 3) | color_p->ch.green_l;
            uint8_t g = (g6 << 2) | (g6 >> 4);
            uint8_t b = (color_p->ch.blue    << 3) | (color_p->ch.blue    >> 2);
            framebuf[y * HOR_RES + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            color_p++;
        }
    }

    /* When LVGL has finished all partial updates for this frame, blit */
    if (lv_disp_flush_is_last(drv)) {
        SDL_UpdateTexture(sdl_texture, NULL, framebuf, HOR_RES * sizeof(uint32_t));
        SDL_RenderClear(sdl_renderer);
        SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
        SDL_RenderPresent(sdl_renderer);
    }

    lv_disp_flush_ready(drv);
}

/* ── Main ──────────────────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Disable stdout buffering so printf shows immediately */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* SDL init */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    sdl_window = SDL_CreateWindow("Pocket Watch Simulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        HOR_RES, VER_RES, 0);
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    sdl_texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        HOR_RES, VER_RES);

    /* LVGL init */
    lv_init();

    /* Draw buffer */
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[HOR_RES * 40];
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, HOR_RES * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = HOR_RES;
    disp_drv.ver_res  = VER_RES;
    disp_drv.flush_cb = sdl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* Mouse input device */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&indev_drv);

    /* Create screen manager (tileview: clock, stopwatch, timer, alarm) */
    screen_manager_init(lv_scr_act(), 440);
    screen_manager_set_battery(75, false);   /* demo: 75% battery */

    /* Register menu callbacks */
    clock_face_set_menu_callbacks(on_wifi_toggle, on_bt_toggle, on_reset_steps);
    clock_face_set_screen_power_cb(on_screen_power);
    clock_face_set_settings_changed_cb(on_settings_changed);
    alarm_screen_set_changed_cb(on_settings_changed);
    timer_screen_set_changed_cb(on_settings_changed);
    stopwatch_set_changed_cb(on_settings_changed);
    clock_face_set_wifi_state(sim_wifi);
    clock_face_set_bt_switch_state(sim_bt);
    clock_face_set_steps(0);

    /* Load saved settings (after clock face is created so widgets exist) */
    sim_load_settings();
    clock_face_set_wifi_state(sim_wifi);
    clock_face_set_bt_switch_state(sim_bt);

    /* Gadgetbridge TCP init (localhost:9001) */
    if (sim_bt) {
        gb_init("Pocket Watch", &gb_cbs);
        gb_send_firmware_info("1.0.0", "SIM");
    }
    /* Sound init */
    sound_init(-1);
    /* Get initial time from host */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    clock_face_update(t->tm_hour, t->tm_min, t->tm_sec);
    clock_face_set_datetime(t->tm_year + 1900, t->tm_mon + 1,
                            t->tm_mday, t->tm_hour, t->tm_min);

    printf("Pocket Watch Simulator running – close window to exit\n");

    /* Seed simulated weather data immediately */
    {
        gb_weather_info_t w = {};
        w.temp = 22;
        w.humidity = 65;
        w.code = 800;       /* Clear sky */
        snprintf(w.txt, sizeof(w.txt), "Clear");
        w.wind = 12;
        w.wind_dir = 180;
        snprintf(w.location, sizeof(w.location), "London");
        w.temp_min = 18;
        w.temp_max = 25;
        w.sunrise = 6 * 3600 + 45 * 60;   /* 06:45 */
        w.sunset  = 18 * 3600 + 20 * 60;  /* 18:20 */
        weather_screen_update(&w);
    }

    /* Main loop */
    int last_sec = -1;
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_MOUSEBUTTONDOWN) mouse_pressed = true;
            if (e.type == SDL_MOUSEBUTTONUP)   mouse_pressed = false;
            if (e.type == SDL_MOUSEMOTION) {
                mouse_x = e.motion.x;
                mouse_y = e.motion.y;
            }
        }

        /* Process screen timeout / wake */
        clock_face_process_activity();

        /* Service Gadgetbridge TCP */
        if (sim_bt) {
            gb_loop();
            clock_face_set_bt_state(gb_is_connected());
        }

        /* Service sound */
        sound_loop();

        /* Service screen manager (stopwatch, timer, alarm) */
        screen_manager_loop();
        screen_manager_set_time(t->tm_hour, t->tm_min, t->tm_sec);

        /* Update clock every second */
        now = time(NULL);
        t = localtime(&now);
        if (t->tm_sec != last_sec) {
            last_sec = t->tm_sec;
            clock_face_update(t->tm_hour, t->tm_min, t->tm_sec);
            clock_face_set_datetime(t->tm_year + 1900, t->tm_mon + 1,
                                    t->tm_mday, t->tm_hour, t->tm_min);

            /* Simulate ~80 steps/min for demo */
            sim_steps += 1 + (rand() % 3);
            clock_face_set_steps(sim_steps);

            /* Send periodic data to Gadgetbridge */
            if (sim_bt && gb_is_connected()) {
                gb_send_battery(75, 4.1f);
                gb_send_steps(sim_steps);
            }

            /* Update simulated radar data every ~10 seconds */
            if (t->tm_sec % 10 == 0) {
                static const struct { radar_node_type_t type; int8_t rssi; const char *name; uint32_t hash; } sim_devices[] = {
                    { RADAR_NODE_BLE,  -42, "AirPods Pro",  0xA1B2C3D4 },
                    { RADAR_NODE_BLE,  -55, "Galaxy Watch", 0x12345678 },
                    { RADAR_NODE_BLE,  -68, "Mi Band 7",    0xDEADBEEF },
                    { RADAR_NODE_BLE,  -73, "Pixel Buds",   0xCAFEBABE },
                    { RADAR_NODE_BLE,  -88, "BT Speaker",   0xF00DCAFE },
                    { RADAR_NODE_WIFI, -35, "HomeNet",      0x11223344 },
                    { RADAR_NODE_WIFI, -52, "Starbucks",    0x55667788 },
                    { RADAR_NODE_WIFI, -64, "5G-Router",    0x99AABBCC },
                    { RADAR_NODE_WIFI, -78, "Neighbor-5G",  0xDDEEFF00 },
                    { RADAR_NODE_WIFI, -91, "FreeWiFi",     0x01020304 },
                };
                int dev_count = sizeof(sim_devices) / sizeof(sim_devices[0]);
                radar_node_t rnodes[RADAR_MAX_NODES];
                /* Vary the count and RSSI slightly each cycle */
                int active = 6 + (rand() % (dev_count - 5));
                if (active > dev_count) active = dev_count;
                for (int i = 0; i < active; i++) {
                    rnodes[i].type   = sim_devices[i].type;
                    rnodes[i].rssi   = sim_devices[i].rssi + (rand() % 11) - 5;
                    rnodes[i].active = true;
                    rnodes[i].hash   = sim_devices[i].hash;
                    strncpy(rnodes[i].name, sim_devices[i].name,
                            sizeof(rnodes[i].name) - 1);
                    rnodes[i].name[sizeof(rnodes[i].name) - 1] = '\0';
                }
                radar_screen_update(rnodes, active);
            }
        }

        lv_timer_handler();

        /* Feed simulated compass (slow drift + gentle tilt) */
        {
            static float sim_heading = 0;
            sim_heading += 0.15f;  /* slow rotation for demo */
            if (sim_heading >= 360.0f) sim_heading -= 360.0f;
            float sim_pitch = 0.1f * sinf(sim_heading * 0.05f);
            float sim_roll  = 0.08f * cosf(sim_heading * 0.07f);
            compass_screen_feed_imu(
                sinf(sim_roll) * 9.8f,       /* ax */
                -sinf(sim_pitch) * 9.8f,     /* ay */
                cosf(sim_pitch) * 9.8f,      /* az */
                0.0f, 0.0f, 0.0f,            /* gx, gy, gz */
                0.005f);
            compass_screen_set_heading(sim_heading);

            /* Feed the same simulated tilt to the level screen */
            level_screen_feed_imu(
                sinf(sim_roll) * 9.8f,
                -sinf(sim_pitch) * 9.8f,
                cosf(sim_pitch) * 9.8f);
        }

        /* Feed simulated system info (every ~1 s) */
        {
            static uint32_t last_si = 0;
            uint32_t now_ms = SDL_GetTicks();
            if (now_ms - last_si >= 1000) {
                last_si = now_ms;
                sysinfo_data_t si;
                memset(&si, 0, sizeof(si));
                si.battery_pct  = 75;
                si.battery_volt = 4.1f;
                si.wifi_connected = 1;
                si.wifi_rssi    = -55;
                si.wifi_ssid    = "SimNetwork";
                si.bt_connected = 1;
                si.uptime_s     = now_ms / 1000;
                si.free_heap    = 245760;
                sysinfo_screen_update(&si);
            }
        }

        /* Seed simulated music data (once, early) */
        {
            static bool music_seeded = false;
            static uint32_t seed_tick = 0;
            if (!music_seeded && seed_tick == 0) seed_tick = SDL_GetTicks();
            if (!music_seeded && (SDL_GetTicks() - seed_tick > 2000)) {
                gb_music_info_t m = {};
                strncpy(m.artist, "Pink Floyd", GB_MUSIC_MAX - 1);
                strncpy(m.track,  "Comfortably Numb", GB_MUSIC_MAX - 1);
                strncpy(m.album,  "The Wall", GB_MUSIC_MAX - 1);
                m.playing = true;
                media_screen_update(&m);
                music_seeded = true;
            }
        }

        /* Simulated navigation – cycle through a fake route */
        {
            static uint32_t last_nav = 0;
            static int nav_step = 0;
            uint32_t now_ms = SDL_GetTicks();
            if (now_ms - last_nav >= 4000) {
                last_nav = now_ms;
                static const struct {
                    const char *action; const char *distance;
                    const char *instr;  const char *eta;
                } route[] = {
                    { "straight",     "500 m",  "Continue on Main Street",           "12:42" },
                    { "right",        "200 m",  "Turn right onto Oak Avenue",        "12:41" },
                    { "straight",     "1.2 km", "Continue on Oak Avenue",            "12:40" },
                    { "slight-left",  "800 m",  "Keep left at the fork",             "12:38" },
                    { "left",         "300 m",  "Turn left onto Elm Drive",          "12:36" },
                    { "straight",     "2.5 km", "Continue on Elm Drive",             "12:34" },
                    { "right",        "150 m",  "Turn right onto Pine Road",         "12:30" },
                    { "uturn",        "100 m",  "Make a U-turn",                     "12:29" },
                    { "slight-right", "600 m",  "Keep right onto Highway 7",         "12:28" },
                    { "straight",     "3.1 km", "Continue on Highway 7",             "12:24" },
                    { "left",         "400 m",  "Exit left toward Downtown",         "12:18" },
                    { "right",        "50 m",   "Turn right onto Cedar Lane",        "12:16" },
                    { "finish",       "20 m",   "Arrive at destination on the left", "12:15" },
                };
                int nsteps = sizeof(route) / sizeof(route[0]);
                if (nav_step < nsteps) {
                    gb_nav_info_t nav = {};
                    nav.active = true;
                    strncpy(nav.action,   route[nav_step].action,   GB_NAV_ACTION_MAX - 1);
                    strncpy(nav.distance, route[nav_step].distance, GB_NAV_DIST_MAX - 1);
                    strncpy(nav.instr,    route[nav_step].instr,    GB_NAV_INSTR_MAX - 1);
                    strncpy(nav.eta,      route[nav_step].eta,      GB_NAV_ETA_MAX - 1);
                    nav_screen_update(&nav);
                    nav_step++;
                    if (nav_step >= nsteps) nav_step = 0;  /* loop route */
                }
            }
        }

        SDL_Delay(5);
    }

    /* Save on exit */
    sim_save_settings();

    SDL_DestroyTexture(sdl_texture);
    SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);
    SDL_Quit();
    return 0;
}

#endif /* SIMULATOR */
