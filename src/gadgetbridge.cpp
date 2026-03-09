/*
 *  gadgetbridge.cpp – BLE Gadgetbridge interface (ESP32-S3 / NimBLE)
 *
 *  Implements the BangleJS protocol over Nordic UART Service (NUS).
 *  Gadgetbridge → watch: "GB({json})\n" or "setTime(epoch)\n"
 *  Watch → Gadgetbridge: raw JSON strings via NUS notify.
 */

#ifndef SIMULATOR

#include "gadgetbridge.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>
#include <stdio.h>

/* ── Nordic UART Service UUIDs ───────────────────────────────── */
#define NUS_SERVICE_UUID   "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX_CHAR_UUID   "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  /* phone writes here */
#define NUS_RX_CHAR_UUID   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  /* watch notifies here */

/* ── State ───────────────────────────────────────────────────── */
static NimBLEServer         *ble_server = nullptr;
static NimBLECharacteristic *rx_char    = nullptr;  /* notify to phone */
static NimBLECharacteristic *tx_char    = nullptr;  /* phone writes  */
static NimBLEAdvertising    *advertising = nullptr;
static bool                  connected   = false;
static uint16_t              conn_id     = 0;
static gb_callbacks_t        cbs         = {};
static char rx_buf[512];
static int  rx_buf_len = 0;

/* ── Outgoing stream buffer (one BLE chunk per gb_loop call) ── */
#define TX_BUF_SIZE    512
static char     tx_buf[TX_BUF_SIZE];
static int      tx_buf_head = 0;       /* write position             */
static int      tx_buf_tail = 0;       /* read position              */
static uint32_t tx_last_ms  = 0;
static uint32_t connect_ms  = 0;       /* millis() when BLE connected */
#define TX_INTERVAL_MS   50            /* min ms between BLE notifies */
#define CONNECT_DELAY_MS 1500          /* wait for MTU exchange       */

/* ── Helpers: simple JSON value extraction ───────────────────── */
static bool json_get_string(const char *json, const char *key,
                            char *out, int max_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end) return false;
    int len = end - start;
    if (len >= max_len) len = max_len - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool json_get_int(const char *json, const char *key, int *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    *out = atoi(start);
    return true;
}

static bool json_get_bool(const char *json, const char *key, bool *out) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    while (*start == ' ') start++;
    *out = (*start == 't');
    return true;
}

/* ── Process a complete line from Gadgetbridge ───────────────── */
static void process_line(const char *line) {
    /* Skip leading control chars (e.g. 0x10 DLE framing byte from Gadgetbridge) */
    while (*line && (unsigned char)*line < 0x20) line++;
    if (!*line) return;

    Serial.printf("GB rx: %s\n", line);

    /* setTime(epoch) – also extract timezone from E.setTimeZone() if present */
    if (strncmp(line, "setTime(", 8) == 0) {
        long epoch = atol(line + 8);
        /* Look for E.setTimeZone(offset) in the same line */
        const char *tz_str = strstr(line, "E.setTimeZone(");
        if (tz_str) {
            float tz_hours = atof(tz_str + 14);
            /* Build POSIX TZ string: offset sign is inverted (UTC-7 = UTC+7 in POSIX) */
            int tz_sec = (int)(tz_hours * 3600.0f);
            int sign = (tz_sec >= 0) ? -1 : 1;  /* POSIX inverts sign */
            int abs_sec = (tz_sec < 0) ? -tz_sec : tz_sec;
            int h = abs_sec / 3600;
            int m = (abs_sec % 3600) / 60;
            char posix_tz[32];
            if (m > 0)
                snprintf(posix_tz, sizeof(posix_tz), "UTC%c%d:%02d",
                         sign > 0 ? '+' : '-', h, m);
            else
                snprintf(posix_tz, sizeof(posix_tz), "UTC%c%d",
                         sign > 0 ? '+' : '-', h);
            setenv("TZ", posix_tz, 1);
            tzset();
            Serial.printf("GB timezone: %.1f h → TZ=%s\n", tz_hours, posix_tz);
        }
        if (epoch > 0) {
            struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            /* Now get local time for the callback */
            struct tm t;
            time_t tt = (time_t)epoch;
            localtime_r(&tt, &t);
            if (cbs.on_time_set)
                cbs.on_time_set(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                                t.tm_hour, t.tm_min, t.tm_sec);
        }
        return;
    }

    /* GB({json}) */
    if (strncmp(line, "GB(", 3) == 0) {
        const char *json = line + 3;
        /* find closing ) */
        char json_buf[480];
        const char *end = strrchr(json, ')');
        int len = end ? (int)(end - json) : (int)strlen(json);
        if (len >= (int)sizeof(json_buf)) len = sizeof(json_buf) - 1;
        memcpy(json_buf, json, len);
        json_buf[len] = '\0';

        char type[32] = "";
        json_get_string(json_buf, "t", type, sizeof(type));

        if (strcmp(type, "notify") == 0) {
            gb_notification_t notif = {};
            int id = 0;
            json_get_int(json_buf, "id", &id);
            notif.id = (uint32_t)id;
            json_get_string(json_buf, "title", notif.title, GB_NOTIF_TITLE_MAX);
            json_get_string(json_buf, "body", notif.body, GB_NOTIF_BODY_MAX);
            json_get_string(json_buf, "src", notif.src, GB_NOTIF_SRC_MAX);
            if (cbs.on_notification) cbs.on_notification(&notif);
        }
        else if (strcmp(type, "notify-") == 0) {
            int id = 0;
            json_get_int(json_buf, "id", &id);
            if (cbs.on_notif_dismiss) cbs.on_notif_dismiss((uint32_t)id);
        }
        else if (strcmp(type, "musicinfo") == 0) {
            gb_music_info_t music = {};
            json_get_string(json_buf, "artist", music.artist, GB_MUSIC_MAX);
            json_get_string(json_buf, "track", music.track, GB_MUSIC_MAX);
            json_get_string(json_buf, "album", music.album, GB_MUSIC_MAX);
            if (cbs.on_music) cbs.on_music(&music);
        }
        else if (strcmp(type, "musicstate") == 0) {
            char state[16] = "";
            json_get_string(json_buf, "state", state, sizeof(state));
            gb_music_info_t music = {};
            music.playing = (strcmp(state, "play") == 0);
            if (cbs.on_music) cbs.on_music(&music);
        }
        else if (strcmp(type, "call") == 0) {
            char cmd[16] = "", name[64] = "", number[32] = "";
            json_get_string(json_buf, "cmd", cmd, sizeof(cmd));
            json_get_string(json_buf, "name", name, sizeof(name));
            json_get_string(json_buf, "number", number, sizeof(number));
            if (cbs.on_call) cbs.on_call(cmd, name, number);
        }
        else if (strcmp(type, "find") == 0) {
            bool start = false;
            json_get_bool(json_buf, "n", &start);
            if (cbs.on_find) cbs.on_find(start);
        }
        else if (strcmp(type, "weather") == 0) {
            gb_weather_info_t w = {};
            w.sunrise = -1; w.sunset = -1;
            json_get_int(json_buf, "temp", &w.temp);
            json_get_int(json_buf, "hum", &w.humidity);
            json_get_int(json_buf, "code", &w.code);
            json_get_string(json_buf, "txt", w.txt, GB_WEATHER_TXT_MAX);
            json_get_int(json_buf, "wind", &w.wind);
            json_get_int(json_buf, "wdir", &w.wind_dir);
            json_get_string(json_buf, "loc", w.location, GB_WEATHER_LOC_MAX);
            /* Gadgetbridge sends "hi"/"lo", not "tmax"/"tmin" */
            json_get_int(json_buf, "hi", &w.temp_max);
            json_get_int(json_buf, "lo", &w.temp_min);
            json_get_int(json_buf, "srise", &w.sunrise);
            json_get_int(json_buf, "sset", &w.sunset);
            /* Gadgetbridge sends temperatures in Kelvin – convert to °C */
            w.temp     -= 273;
            w.temp_min -= 273;
            w.temp_max -= 273;
            Serial.printf("GB: weather=%s %d°C (%d/%d) %s\n",
                          w.location, w.temp, w.temp_min, w.temp_max, w.txt);
            if (cbs.on_weather) cbs.on_weather(&w);
        }
        else if (strcmp(type, "nav") == 0) {
            gb_nav_info_t nav = {};
            nav.active = true;
            json_get_string(json_buf, "instr", nav.instr, GB_NAV_INSTR_MAX);
            json_get_string(json_buf, "distance", nav.distance, GB_NAV_DIST_MAX);
            json_get_string(json_buf, "action", nav.action, GB_NAV_ACTION_MAX);
            json_get_string(json_buf, "eta", nav.eta, GB_NAV_ETA_MAX);
            if (cbs.on_nav) cbs.on_nav(&nav);
        }
        else if (strcmp(type, "nav-") == 0) {
            gb_nav_info_t nav = {};
            nav.active = false;
            if (cbs.on_nav) cbs.on_nav(&nav);
        }
        else if (type[0]) {
            Serial.printf("GB: unhandled type '%s'\n", type);
        }
        return;
    }
}

/* ── NimBLE Server Callbacks ─────────────────────────────────── */
static bool fw_info_sent = false;

class ServerCB : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, ble_gap_conn_desc *desc) override {
        connected = true;
        conn_id   = desc->conn_handle;
        connect_ms = millis();
        Serial.printf("Gadgetbridge: BLE connected (conn %d)\n", conn_id);
        /* Firmware info sent from gb_loop after MTU exchange settles */
    }
    void onDisconnect(NimBLEServer *s) override {
        connected = false;
        tx_buf_head = tx_buf_tail = 0;
        connect_ms = 0;
        fw_info_sent = false;
        Serial.println("Gadgetbridge: BLE disconnected");
        NimBLEDevice::startAdvertising();
    }
};

/* ── NimBLE Characteristic Callbacks (phone writes to TX) ──── */
class TxCharCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string val = c->getValue();
        for (size_t i = 0; i < val.length(); i++) {
            char ch = val[i];
            if (ch == '\n' || ch == '\r') {
                if (rx_buf_len > 0) {
                    rx_buf[rx_buf_len] = '\0';
                    process_line(rx_buf);
                    rx_buf_len = 0;
                }
            } else {
                if (rx_buf_len < (int)sizeof(rx_buf) - 1)
                    rx_buf[rx_buf_len++] = ch;
            }
        }
        /* Some messages arrive without newline */
        if (rx_buf_len > 0 && val.length() > 0) {
            rx_buf[rx_buf_len] = '\0';
            /* Check if it looks complete (ends with ) for GB(...) ) */
            if (rx_buf[rx_buf_len - 1] == ')' ||
                rx_buf[rx_buf_len - 1] == ';') {
                process_line(rx_buf);
                rx_buf_len = 0;
            }
        }
    }
};

static ServerCB server_cb;
static TxCharCB tx_char_cb;

/* ── Append a message to the outgoing stream buffer ── */
static void ble_enqueue(const char *str) {
    if (!connected || !rx_char) return;
    int len = (int)strlen(str);
    int used = tx_buf_head - tx_buf_tail;
    if (len > TX_BUF_SIZE - used) {
        Serial.println("BLE TX buffer full, dropping");
        return;
    }
    /* Compact buffer if needed */
    if (tx_buf_head + len > TX_BUF_SIZE) {
        memmove(tx_buf, tx_buf + tx_buf_tail, used);
        tx_buf_head = used;
        tx_buf_tail = 0;
    }
    memcpy(tx_buf + tx_buf_head, str, len);
    tx_buf_head += len;
}

/* ── Public API ──────────────────────────────────────────────── */

void gb_init(const char *device_name, const gb_callbacks_t *callbacks) {
    if (callbacks) cbs = *callbacks;
    rx_buf_len = 0;

    NimBLEDevice::init(device_name);
    NimBLEDevice::setMTU(256);  /* request larger MTU so JSON fits in one packet */
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(false, false, false);

    ble_server = NimBLEDevice::createServer();
    ble_server->setCallbacks(&server_cb);

    NimBLEService *nus = ble_server->createService(NUS_SERVICE_UUID);

    /* TX characteristic – phone writes data here */
    tx_char = nus->createCharacteristic(
        NUS_TX_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    tx_char->setCallbacks(&tx_char_cb);

    /* RX characteristic – watch sends data here (notify) */
    rx_char = nus->createCharacteristic(
        NUS_RX_CHAR_UUID,
        NIMBLE_PROPERTY::NOTIFY);

    nus->start();

    advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(NUS_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->start();

    Serial.printf("Gadgetbridge BLE started: %s\n", device_name);
}


void gb_loop(void) {
    if (!connected || !ble_server) return;
    /* Wait for MTU exchange to complete after connect */
    if (millis() - connect_ms < CONNECT_DELAY_MS) return;

    /* Send firmware info once after MTU settles */
    if (!fw_info_sent) {
        fw_info_sent = true;
        uint16_t mtu = ble_server->getPeerMTU(conn_id);
        Serial.printf("BLE MTU: %d, sending firmware info\n", mtu);
        ble_enqueue("{\"t\":\"ver\",\"fw\":\"1.0.0\",\"hw\":\"ESP32-S3\"}\r\n");
    }

    /* Send one chunk from the stream buffer, paced by TX_INTERVAL_MS */
    if (tx_buf_tail < tx_buf_head) {
        uint32_t now = millis();
        if (now - tx_last_ms >= TX_INTERVAL_MS) {
            tx_last_ms = now;
            uint16_t peer_mtu = ble_server->getPeerMTU(conn_id);
            size_t max_payload = (peer_mtu > 3) ? (peer_mtu - 3) : 20;
            if (max_payload < 20) max_payload = 20;
            size_t avail = (size_t)(tx_buf_head - tx_buf_tail);
            size_t len = (avail > max_payload) ? max_payload : avail;
            rx_char->notify((const uint8_t *)(tx_buf + tx_buf_tail), len);
            tx_buf_tail += (int)len;
            /* Reset buffer when fully drained */
            if (tx_buf_tail >= tx_buf_head) {
                tx_buf_tail = tx_buf_head = 0;
            }
        }
    }
}

void gb_send_battery(int percent, float voltage) {
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"status\",\"bat\":%d,\"volt\":%.2f}\r\n",
             percent, voltage);
    ble_enqueue(buf);
}

void gb_send_steps(uint32_t steps) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"t\":\"act\",\"stp\":%u}\r\n",
             (unsigned)steps);
    ble_enqueue(buf);
}

void gb_send_firmware_info(const char *fw_version, const char *hw_version) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"ver\",\"fw\":\"%s\",\"hw\":\"%s\"}\r\n",
             fw_version, hw_version);
    ble_enqueue(buf);
}

void gb_send_music_cmd(const char *cmd) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"t\":\"music\",\"n\":\"%s\"}\r\n", cmd);
    ble_enqueue(buf);
}

bool gb_is_connected(void) {
    return connected;
}

void gb_deinit(void) {
    if (ble_server) {
        NimBLEDevice::stopAdvertising();
        /* Disconnect every active connection by its real handle */
        auto peers = ble_server->getPeerDevices();
        for (auto &connId : peers) {
            ble_server->disconnect(connId);
        }
        delay(150);   /* let the stack finish disconnect events */
    }
    /* Pass false: our callbacks are static objects, not heap-allocated.
       deinit(true) would try to delete them and crash. */
    NimBLEDevice::deinit(false);
    ble_server   = nullptr;
    rx_char      = nullptr;
    tx_char      = nullptr;
    advertising  = nullptr;
    connected    = false;
    rx_buf_len   = 0;
    tx_buf_head = tx_buf_tail = 0;
    fw_info_sent = false;
    Serial.println("Gadgetbridge BLE stopped");
}

#endif /* !SIMULATOR */
