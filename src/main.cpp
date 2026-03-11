/*
 *  main.cpp – Analogue Clock (LVGL) + Config Portal
 *  Target: Waveshare ESP32-S3-Touch-AMOLED-1.75C
 *          CO5300 466×466 QSPI AMOLED
 *
 *  Display rendered via LVGL (lv_meter), driven through Arduino_GFX.
 *  Clock time sourced from NTP (WiFi) or Gadgetbridge (BLE).
 *  WiFi config portal on port 80.
 */

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <lvgl.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include "Arduino_GFX_Library.h"
#include <SensorQMI8658.hpp>
#include <TouchDrvCSTXXX.hpp>
#define XPOWERS_CHIP_AXP2101
#include <XPowersAXP2101.tpp>
#include "pin_config.h"
#include "config_portal.h"
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

// ── Settings persistence ────────────────────────────────────────────────────
static Preferences watchPrefs;

// ── Display bus & driver ────────────────────────────────────────────────────
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK,
    LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0 /* rotation */,
    LCD_WIDTH, LCD_HEIGHT,
    6 /* col_offset */, 0, 0, 0);

// ── LVGL draw buffer ────────────────────────────────────────────────────────
// Single full-frame buffer in PSRAM for direct_mode rendering.
// A small internal-DMA staging buffer shuttles pixel data to the QSPI display.
// Single buffer is optimal here: the flush is synchronous (blocking SPI),
// so double-buffering adds a costly PSRAM sync copy with zero benefit.
static const uint32_t LV_BUF_SIZE     = LCD_WIDTH * LCD_HEIGHT;  // full frame in PSRAM
static const uint32_t STAGING_LINES   = 120;               // SPI staging strip (larger = fewer DMA roundtrips)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1    = nullptr;   // PSRAM
static lv_color_t *staging = nullptr;   // internal DMA

// ── LVGL flush callback ────────────────────────────────────────────────────
static void my_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    gfx->startWrite();
    lv_coord_t y = area->y1;
    uint32_t rows_left = h;
    while (rows_left > 0) {
        uint32_t chunk_h = rows_left > STAGING_LINES ? STAGING_LINES : rows_left;
        uint32_t chunk_px = w * chunk_h;
        /* Copy from PSRAM frame buffer to DMA-capable staging.
           direct_mode: pixels sit at screen coords (stride = LCD_WIDTH).
           Full-width rows are contiguous so we can bulk-copy. */
        if (w == LCD_WIDTH) {
            memcpy(staging,
                   (uint16_t *)color_p + (uint32_t)y * LCD_WIDTH,
                   chunk_px * sizeof(uint16_t));
        } else {
            uint16_t *dst = (uint16_t *)staging;
            for (uint32_t r = 0; r < chunk_h; r++) {
                memcpy(dst,
                       (uint16_t *)color_p + ((uint32_t)(y + r) * LCD_WIDTH + area->x1),
                       w * sizeof(uint16_t));
                dst += w;
            }
        }
#if (LV_COLOR_16_SWAP != 0)
        gfx->draw16bitBeRGBBitmap(area->x1, y, (uint16_t *)staging, w, chunk_h);
#else
        gfx->draw16bitRGBBitmap(area->x1, y, (uint16_t *)staging, w, chunk_h);
#endif
        y += chunk_h;
        rows_left -= chunk_h;
    }
    gfx->endWrite();

    lv_disp_flush_ready(drv);
}

static int cur_hour = 12;
static int cur_min  = 0;
static int cur_sec  = 0;

// ── IMU (QMI8658) ───────────────────────────────────────────────────────
SensorQMI8658 imu;
static bool imu_ok = false;
static uint32_t step_count = 0;

/* Software step counter – peak detection on accel magnitude */
static float sw_ped_avg    = 1.0f;   /* running average of |accel| in g  */
static float sw_ped_peak   = 0.0f;   /* current peak deviation from avg  */
static bool  sw_ped_above  = false;  /* currently above threshold?       */
static unsigned long sw_ped_last_step_ms = 0;  /* debounce              */
#define SW_PED_THRESH      0.15f     /* g deviation to trigger step      */
#define SW_PED_DEBOUNCE_MS 250       /* min ms between steps             */
#define SW_PED_AVG_ALPHA   0.05f     /* low-pass for baseline            */

/* Wake-on-motion detection */
#define WOM_THRESH         0.12f     /* g deviation to trigger wake       */
#define WOM_COOLDOWN_MS    1500      /* min ms between wake triggers      */
static unsigned long wom_last_wake_ms = 0;

// ── PMIC (AXP2101) ──────────────────────────────────────────────────────────
XPowersAXP2101 pmic;
static bool pmic_ok = false;

// ── Touch (CST9217) ─────────────────────────────────────────────────────────
TouchDrvCST92xx touch;
static bool touch_ok = false;
static int16_t touch_x = 0, touch_y = 0;
static bool touch_pressed = false;

static void my_touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    int16_t x[1], y[1];
    if (touch_ok && touch.getPoint(x, y, 1)) {
        touch_x = (LCD_WIDTH - 1) - x[0];
        touch_y = (LCD_HEIGHT - 1) - y[0];
        touch_pressed = true;
        data->state = LV_INDEV_STATE_PR;
    } else {
        touch_pressed = false;
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = touch_x;
    data->point.y = touch_y;
}

// ── Menu callbacks ──────────────────────────────────────────────────────────
static bool wifi_enabled = true;
static bool bt_enabled   = true;

static void on_wifi_toggle(bool enable) {
    wifi_enabled = enable;
    if (enable) {
        Serial.println("WiFi: enabling");
        configPortalBegin();
    } else {
        Serial.println("WiFi: disabling");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
}

// ── Gadgetbridge callbacks (must precede on_bt_toggle) ──────────────────────
static void on_gb_time_set(int year, int month, int day,
                           int hour, int min, int sec) {
    if (configIsManualTimeOnly()) {
        Serial.println("GB time sync ignored (manual time only)");
        return;
    }
    Serial.printf("GB time sync: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, min, sec);
    /* Time already set by settimeofday in gadgetbridge.cpp – just update display */
    cur_hour = hour; cur_min = min; cur_sec = sec;
    clock_face_update(cur_hour, cur_min, cur_sec);
    clock_face_set_datetime(year, month, day, hour, min);
}

static void on_gb_notification(const gb_notification_t *n) {
    Serial.printf("GB notif [%s]: %s – %s\n", n->src, n->title, n->body);
    clock_face_show_notification(n->title, n->body, n->src);    sound_play_ding();}

static void on_gb_notif_dismiss(uint32_t id) {
    Serial.printf("GB dismiss notif %u\n", id);
    clock_face_dismiss_notification();
}

static void on_gb_music(const gb_music_info_t *m) {
    Serial.printf("GB music: %s – %s (%s)\n",
                  m->artist, m->track, m->playing ? "play" : "pause");
    media_screen_update(m);
}

static void on_gb_call(const char *cmd, const char *name, const char *number) {
    Serial.printf("GB call %s: %s (%s)\n", cmd, name, number);
}

static void on_gb_find(bool start) {
    Serial.printf("GB find: %s\n", start ? "START" : "STOP");
}

static void on_gb_weather(const gb_weather_info_t *w) {
    Serial.printf("GB weather: %dC %s (code %d) hum=%d%% wind=%d\n",
                  w->temp, w->txt, w->code, w->humidity, w->wind);
    weather_screen_update(w);
}

static void on_gb_nav(const gb_nav_info_t *nav) {
    Serial.printf("GB nav: %s %s %s (eta %s) active=%d\n",
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

static void on_bt_toggle(bool enable) {
    bt_enabled = enable;
    if (enable) {
        Serial.println("BLE: enabling");
        gb_init("Bangle.js PocketW", &gb_cbs);
    } else {
        Serial.println("BLE: disabling");
        gb_deinit();
    }
    clock_face_set_bt_state(false);  /* will update on next connected poll */
}

static void on_reset_steps(void) {
    Serial.println("Pedometer: reset");
    step_count = 0;
    sw_ped_avg = 1.0f;
    sw_ped_above = false;
    clock_face_set_steps(0);
}

static void on_screen_power(bool on) {
    if (on) {
        setCpuFrequencyMhz(240);
        Serial.println("Screen: ON (240 MHz)");
        gfx->displayOn();            /* SLPOUT – wake the AMOLED panel */
        gfx->setBrightness(200);
        digitalWrite(PA, HIGH);       /* re-enable power amplifier      */
    } else {
        Serial.println("Screen: OFF (80 MHz)");
        gfx->setBrightness(0);
        gfx->displayOff();            /* SLPIN – puts AMOLED into sleep */
        digitalWrite(PA, LOW);        /* disable power amplifier        */
        setCpuFrequencyMhz(80);
    }
}

static void on_settings_changed(void) {
    watchPrefs.putBool("wifi_on", wifi_enabled);
    watchPrefs.putBool("bt_on",   bt_enabled);
    watchPrefs.putInt("timeout_idx", clock_face_get_timeout_idx());
    watchPrefs.putBool("time_24h", clock_face_get_24h());
    watchPrefs.putBool("orrery_on", clock_face_get_orrery());
    watchPrefs.putBool("metric", clock_face_get_metric());
    watchPrefs.putBool("wake_mot", clock_face_get_wake_motion());
    /* Persist alarm */
    watchPrefs.putInt("alarm_hr", alarm_screen_get_hour());
    watchPrefs.putInt("alarm_mn", alarm_screen_get_min());
    watchPrefs.putBool("alarm_en", alarm_screen_get_enabled());
    /* Persist timer preset */
    watchPrefs.putInt("tmr_hr",  timer_screen_get_set_hr());
    watchPrefs.putInt("tmr_min", timer_screen_get_set_min());
    watchPrefs.putInt("tmr_sec", timer_screen_get_set_sec());
    watchPrefs.putULong("tmr_rem", timer_screen_get_remaining_ms());
    /* Persist stopwatch elapsed */
    watchPrefs.putULong("sw_ms", stopwatch_get_elapsed_ms());
    /* Persist screen order */
    {
        int order[SCR_COUNT];
        int cnt = screen_manager_get_screen_order(order, SCR_COUNT);
        String s;
        for (int i = 0; i < cnt; i++) {
            if (i > 0) s += ',';
            s += String(order[i]);
        }
        watchPrefs.putString("scr_ord", s);
    }
    Serial.println("Settings saved to NVS");
}

// ── Arduino entry points ────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Wire.begin(IIC_SDA, IIC_SCL);
    delay(50);  /* let I2C devices power up and stabilise */
    Serial.println("Pocket Watch – LVGL + CO5300");

    // ── Load saved settings ─────────────────────────────────────────────────
    watchPrefs.begin("watch", false);  /* namespace "watch", RW mode */
    wifi_enabled = watchPrefs.getBool("wifi_on", true);
    bt_enabled   = watchPrefs.getBool("bt_on",   true);
    int saved_timeout_idx = watchPrefs.getInt("timeout_idx", 3);  /* default: always on */
    bool saved_24h = watchPrefs.getBool("time_24h", true);  /* default: 24-hour */
    bool saved_orrery = watchPrefs.getBool("orrery_on", true);  /* default: on */
    bool saved_metric = watchPrefs.getBool("metric", true);  /* default: metric */
    bool saved_wake_motion = watchPrefs.getBool("wake_mot", false);  /* default: off */
    String saved_scr_order = watchPrefs.getString("scr_ord", "");

    // ── Initialise AXP2101 PMIC (must be first – it powers other I2C devices) ─
    if (pmic.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
        pmic_ok = true;
        pmic.enableBattDetection();
        pmic.enableBattVoltageMeasure();
        pmic.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
        pmic.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
        pmic.clearIrqStatus();
        Serial.println("AXP2101 PMIC found");
    } else {
        Serial.println("AXP2101 not found – battery disabled");
    }

    // ── Initialise QMI8658 IMU (pedometer) ──────────────────────────────────
    if (imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS)) {
        imu_ok = true;
        Serial.println("QMI8658 IMU found");
        imu.configAccelerometer(SensorQMI8658::ACC_RANGE_2G,
                                SensorQMI8658::ACC_ODR_62_5Hz,
                                SensorQMI8658::LPF_MODE_0);
        imu.configGyroscope(SensorQMI8658::GYR_RANGE_512DPS,
                            SensorQMI8658::GYR_ODR_56_05Hz,
                            SensorQMI8658::LPF_MODE_0);
        imu.enableAccelerometer();
        imu.enableGyroscope();
        Serial.println("IMU accel+gyro enabled (software pedometer)");
    } else {
        Serial.println("QMI8658 not found – steps disabled");
    }

    // ── Initialise display ──────────────────────────────────────────────────
    // Display init MUST come before touch: they share reset pin 2,
    // so gfx->begin() resets the touch controller.
    if (!gfx->begin()) {
        Serial.println("gfx->begin() FAILED – halting");
        while (true) delay(1000);
    }
    gfx->fillScreen(0x0000);
    gfx->setBrightness(200);

    // ── Initialise CST9217 touch (after display, shared reset pin) ──────────
    touch.setPins(TP_RST, TP_INT);
    touch_ok = touch.begin(Wire, CST92XX_SLAVE_ADDRESS);
    if (touch_ok) {
        Serial.println("CST9217 touch found");
    } else {
        Serial.println("CST9217 not found – touch disabled");
    }

    // ── LVGL init ───────────────────────────────────────────────────────────
    lv_init();

    /* Render buffer in PSRAM (full frame for direct_mode).
       A small internal-DMA staging buffer is used in the flush callback
       to shuttle chunks to the QSPI display (SPI DMA needs internal SRAM). */
    buf1 = (lv_color_t *)heap_caps_malloc(LV_BUF_SIZE * sizeof(lv_color_t),
                                           MALLOC_CAP_SPIRAM);
    staging = (lv_color_t *)heap_caps_malloc(
                  LCD_WIDTH * STAGING_LINES * sizeof(lv_color_t),
                  MALLOC_CAP_DMA);
    if (!buf1 || !staging) {
        Serial.println("LVGL buffer alloc failed!");
        while (true) delay(1000);
    }
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, LV_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = LCD_WIDTH;
    disp_drv.ver_res  = LCD_HEIGHT;
    disp_drv.flush_cb     = my_disp_flush;
    disp_drv.draw_buf     = &draw_buf;
    disp_drv.direct_mode  = 1;   /* frame-buffer mode: only flush dirty areas */
    lv_disp_drv_register(&disp_drv);

    // ── Touch input device ──────────────────────────────────────────────────
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // ── Initialise audio (before screens consume internal DMA memory) ────
    sound_init(0);

    // ── WiFi + BLE init (before LVGL screens to claim internal DRAM) ────────
    Serial.printf("Free heap: %u  internal: %u  PSRAM: %u\n",
                  ESP.getFreeHeap(),
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (wifi_enabled) {
        configPortalBegin();
    }
    if (bt_enabled) {
        gb_init("Bangle.js PocketW", &gb_cbs);
    }
    Serial.printf("After net init – internal: %u  PSRAM: %u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // ── Create screen manager (tileview: all screens) ───────────────────────
    screen_manager_init(lv_scr_act(), LCD_WIDTH > 440 ? 440 : LCD_WIDTH);
    clock_face_update(cur_hour, cur_min, cur_sec);
    {
        struct tm ti;
        if (getLocalTime(&ti, 10)) {
            clock_face_set_datetime(ti.tm_year + 1900, ti.tm_mon + 1,
                                    ti.tm_mday, ti.tm_hour, ti.tm_min);
        }
    }
    /* Restore screen order – try watchPrefs first, fall back to config portal */
    if (saved_scr_order.length() > 0) {
        int ids[SCR_COUNT], cnt = 0, start = 0;
        for (int i = 0; i <= (int)saved_scr_order.length() && cnt < SCR_COUNT; i++) {
            if (i == (int)saved_scr_order.length() || saved_scr_order[i] == ',') {
                if (i > start) ids[cnt++] = saved_scr_order.substring(start, i).toInt();
                start = i + 1;
            }
        }
        screen_manager_set_screen_order(ids, cnt);
    } else if (wifi_enabled) {
        configPortalApplyScreenOrder();
    }

    // ── Register menu callbacks ─────────────────────────────────────────────
    clock_face_set_menu_callbacks(on_wifi_toggle, on_bt_toggle, on_reset_steps);
    clock_face_set_screen_power_cb(on_screen_power);
    clock_face_set_settings_changed_cb(on_settings_changed);
    alarm_screen_set_changed_cb(on_settings_changed);
    timer_screen_set_changed_cb(on_settings_changed);
    stopwatch_set_changed_cb(on_settings_changed);
    clock_face_set_wifi_state(wifi_enabled);
    clock_face_set_bt_switch_state(bt_enabled);
    clock_face_set_timeout_idx(saved_timeout_idx);
    clock_face_set_24h(saved_24h);
    clock_face_set_orrery(saved_orrery);
    clock_face_set_metric(saved_metric);
    clock_face_set_wake_motion(saved_wake_motion);
    clock_face_set_steps(0);

    /* Restore alarm, timer, stopwatch from NVS */
    alarm_screen_set_alarm(
        watchPrefs.getInt("alarm_hr", 7),
        watchPrefs.getInt("alarm_mn", 0),
        watchPrefs.getBool("alarm_en", false));
    timer_screen_set_preset(
        watchPrefs.getInt("tmr_hr",  0),
        watchPrefs.getInt("tmr_min", 5),
        watchPrefs.getInt("tmr_sec", 0));
    timer_screen_set_remaining_ms(
        watchPrefs.getULong("tmr_rem", 0));
    stopwatch_set_elapsed_ms(
        watchPrefs.getULong("sw_ms", 0));

    Serial.printf("After screens – internal: %u  PSRAM: %u\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    Serial.println("Setup complete");

    // ── Startup chime ───────────────────────────────────────────────────────
    sound_play_rtttl("PocketWatch:d=16,o=6,b=180:e,g,b,4e7");
}

void loop() {
    // Process screen timeout / wake (always runs to detect long-press)
    bool screen_active = clock_face_process_activity();

    // Check AXP2101 power button – wake screen on short press
    if (pmic_ok) {
        pmic.getIrqStatus();
        if (pmic.isPekeyShortPressIrq()) {
            clock_face_wake();
        }
        pmic.clearIrqStatus();
    }

    // Wake-on-motion: poll IMU even while screen is off
    if (!screen_active && clock_face_get_wake_motion() && imu_ok && imu.getDataReady()) {
        float ax, ay, az;
        if (imu.getAccelerometer(ax, ay, az)) {
            float mag = sqrtf(ax * ax + ay * ay + az * az);
            float dev = mag - sw_ped_avg;
            if (dev < 0) dev = -dev;
            if (dev > WOM_THRESH) {
                unsigned long now_ms = millis();
                if (now_ms - wom_last_wake_ms >= WOM_COOLDOWN_MS) {
                    wom_last_wake_ms = now_ms;
                    clock_face_wake();
                }
            }
        }
    }

    // When screen is off, skip LVGL rendering and throttle the loop
    if (!screen_active) {
        // Still service BLE so notifications/time sync arrive
        if (bt_enabled) gb_loop();
        // Service timer/alarm (they can fire while screen off)
        screen_manager_loop();
        sound_loop();
        // Throttle CPU – 50 ms idle gives ~20 Hz poll rate for button/IMU
        delay(50);
        return;
    }

    // Service LVGL (render + input processing)
    lv_timer_handler();

    // Service Gadgetbridge BLE
    if (bt_enabled) gb_loop();

    // Service sound
    sound_loop();

    // Service screen manager (stopwatch, timer, alarm)
    screen_manager_loop();

    // Poll IMU: read accel for software pedometer + feed compass/level
    if (imu_ok && imu.getDataReady()) {
        float ax, ay, az, gx, gy, gz;
        bool got_accel = imu.getAccelerometer(ax, ay, az);
        bool got_gyro  = imu.getGyroscope(gx, gy, gz);

        /* Software step counter: peak detection on accel magnitude */
        if (got_accel) {
            float mag = sqrtf(ax * ax + ay * ay + az * az);
            float dev = mag - sw_ped_avg;
            sw_ped_avg += SW_PED_AVG_ALPHA * (mag - sw_ped_avg);

            if (!sw_ped_above && dev > SW_PED_THRESH) {
                sw_ped_above = true;
                unsigned long now_ms = millis();
                if (now_ms - sw_ped_last_step_ms >= SW_PED_DEBOUNCE_MS) {
                    sw_ped_last_step_ms = now_ms;
                    step_count++;
                    clock_face_set_steps(step_count);
                }
            } else if (sw_ped_above && dev < SW_PED_THRESH * 0.3f) {
                sw_ped_above = false;
            }
        }

        int cur_scr = screen_manager_current();
        if (got_accel && got_gyro && (cur_scr == SCR_COMPASS || cur_scr == SCR_LEVEL)) {
            static unsigned long last_imu_us = 0;
            unsigned long now_us = micros();
            float dt = (last_imu_us == 0) ? (1.0f / 56.0f)
                     : (float)(now_us - last_imu_us) * 1e-6f;
            if (dt > 0.2f) dt = 0.2f;   // cap at 200ms to avoid huge jumps
            last_imu_us = now_us;

            if (cur_scr == SCR_COMPASS)
                compass_screen_feed_imu(ax, ay, az, gx, gy, gz, dt);
            if (cur_scr == SCR_LEVEL)
                level_screen_feed_imu(ax, ay, az);
        }
    }

    // Service the web config portal (skip during swipe to avoid frame drops)
    if (wifi_enabled && !screen_manager_is_swiping()) configPortalLoop();

    // Update clock every second
    static unsigned long lastTick = millis();
    unsigned long now = millis();
    if (now - lastTick >= 1000) {
        lastTick += 1000;

        {
            struct tm ti;
            if (getLocalTime(&ti, 0)) {
                cur_hour = ti.tm_hour;
                cur_min  = ti.tm_min;
                cur_sec  = ti.tm_sec;
                clock_face_set_datetime(ti.tm_year + 1900, ti.tm_mon + 1,
                                        ti.tm_mday, cur_hour, cur_min);
            } else {
                cur_sec++;
                if (cur_sec >= 60) { cur_sec = 0; cur_min++; }
                if (cur_min >= 60) { cur_min = 0; cur_hour++; }
                if (cur_hour >= 24) { cur_hour = 0; }
            }
        }

        clock_face_update(cur_hour, cur_min, cur_sec);
        screen_manager_set_time(cur_hour, cur_min, cur_sec);

        // Update step display (already updated in real-time above)
        if (imu_ok) {
            static uint32_t ped_dbg_cnt = 0;
            if (++ped_dbg_cnt >= 5) {
                ped_dbg_cnt = 0;
                Serial.printf("Steps: %u (avg=%.3fg)\n", step_count, sw_ped_avg);
            }
            clock_face_set_steps(step_count);
        }

        // Update WiFi state indicator (reflects enabled state, not STA connection)
        clock_face_set_wifi_state(wifi_enabled);

        // Read battery from PMIC
        int batt_pct = 0;
        float batt_volt = 0.0f;
        if (pmic_ok) {
            batt_pct  = pmic.getBatteryPercent();
            batt_volt = pmic.getBattVoltage() / 1000.0f;
            bool charging = pmic.isCharging();
            screen_manager_set_battery(batt_pct, charging);
        }

        // Update Gadgetbridge connection state & send periodic data
        if (bt_enabled) {
            clock_face_set_bt_state(gb_is_connected());
            if (gb_is_connected()) {
                static int ble_send_counter = 0;
                if (++ble_send_counter >= 30) {   /* every 30 s */
                    ble_send_counter = 0;
                    gb_send_battery(batt_pct, batt_volt);
                    gb_send_steps(step_count);
                }
            }
        }

        // Update system info screen
        {
            sysinfo_data_t si = {};
            si.battery_pct  = pmic_ok ? pmic.getBatteryPercent() : 0;
            si.battery_volt = pmic_ok ? pmic.getBattVoltage() / 1000.0f : 0.0f;
            si.wifi_connected = wifi_enabled && configWiFiConnected();
            si.wifi_enabled = wifi_enabled;
            si.wifi_rssi    = si.wifi_connected ? WiFi.RSSI() : 0;
            si.wifi_ssid    = si.wifi_connected ? WiFi.SSID().c_str() : NULL;
            si.bt_connected = bt_enabled && gb_is_connected();
            si.uptime_s     = now / 1000;
            si.free_heap    = ESP.getFreeHeap();
            /* Show STA IP if connected, otherwise AP IP */
            static char ip_buf[16];
            if (si.wifi_connected)
                strncpy(ip_buf, WiFi.localIP().toString().c_str(), sizeof(ip_buf));
            else if (wifi_enabled) {
                String apip = WiFi.softAPIP().toString();
                if (apip != "0.0.0.0")
                    strncpy(ip_buf, apip.c_str(), sizeof(ip_buf));
                else
                    ip_buf[0] = '\0';
            } else
                ip_buf[0] = '\0';
            ip_buf[sizeof(ip_buf)-1] = '\0';
            si.wifi_ip = ip_buf[0] ? ip_buf : NULL;
            sysinfo_screen_update(&si);
        }

        // ── Radar scan (every ~10 seconds) ──────────────────────────
        {
            static unsigned long last_radar = 0;
            if (now - last_radar >= 10000) {
                last_radar = now;
                radar_node_t rnodes[RADAR_MAX_NODES];
                int rcount = 0;

                /* WiFi scan – ONLY on radar screen AND when no AP clients
                 * are connected.  Scanning hops channels which drops
                 * softAP clients and causes "can't connect". */
                int cur_scr_r = screen_manager_current();
                if (wifi_enabled && cur_scr_r == SCR_RADAR
                    && WiFi.softAPgetStationNum() == 0) {
                    int n = WiFi.scanComplete();
                    if (n == WIFI_SCAN_FAILED) {
                        WiFi.scanNetworks(true);  /* start async */
                    } else if (n > 0) {
                        for (int i = 0; i < n && rcount < RADAR_MAX_NODES; i++) {
                            radar_node_t *rn = &rnodes[rcount++];
                            rn->type   = RADAR_NODE_WIFI;
                            rn->rssi   = WiFi.RSSI(i);
                            rn->active = true;
                            String ssid = WiFi.SSID(i);
                            strncpy(rn->name, ssid.c_str(), sizeof(rn->name) - 1);
                            rn->name[sizeof(rn->name) - 1] = '\0';
                            uint8_t *b = WiFi.BSSID(i);
                            rn->hash = (uint32_t)b[2] << 24 | (uint32_t)b[3] << 16
                                     | (uint32_t)b[4] << 8  | (uint32_t)b[5];
                        }
                        WiFi.scanDelete();
                        WiFi.scanNetworks(true);
                    }
                }

                /* BLE scan (non-blocking, collect cached results) */
                if (bt_enabled) {
                    NimBLEScan *scan = NimBLEDevice::getScan();
                    /* Collect results from previous async scan */
                    NimBLEScanResults results = scan->getResults();
                    for (int i = 0; i < (int)results.getCount()
                             && rcount < RADAR_MAX_NODES; i++) {
                        NimBLEAdvertisedDevice dev = results.getDevice(i);
                        radar_node_t *rn = &rnodes[rcount++];
                        rn->type   = RADAR_NODE_BLE;
                        rn->rssi   = dev.getRSSI();
                        rn->active = true;
                        std::string nm = dev.getName();
                        if (nm.empty()) nm = dev.getAddress().toString();
                        strncpy(rn->name, nm.c_str(), sizeof(rn->name) - 1);
                        rn->name[sizeof(rn->name) - 1] = '\0';
                        /* Hash from address */
                        NimBLEAddress addr = dev.getAddress();
                        const uint8_t *ab = addr.getNative();
                        rn->hash = (uint32_t)ab[0] << 24 | (uint32_t)ab[1] << 16
                                 | (uint32_t)ab[2] << 8  | (uint32_t)ab[3];
                    }
                    scan->clearResults();
                    /* Start next async scan (3 seconds, non-blocking) */
                    scan->setActiveScan(true);
                    scan->setInterval(100);
                    scan->setWindow(99);
                    scan->start(3, nullptr, false);
                }

                radar_screen_update(rnodes, rcount);
            }
        }
    }

    delay(1);   /* yield to RTOS – LVGL internally paces redraws at 60 FPS */
}
