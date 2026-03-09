/*
 *  sim_gadgetbridge.cpp – Simulator Gadgetbridge mock (TCP server)
 *
 *  Listens on localhost:9001 for the same BangleJS-protocol JSON
 *  messages that Gadgetbridge sends over BLE.
 *
 *  Test with:
 *    echo 'GB({"t":"notify","id":1,"src":"WhatsApp","title":"Alice","body":"Hey!"})' | nc localhost 9001
 *    echo 'setTime(1740000000)' | nc localhost 9001
 *    echo 'GB({"t":"call","cmd":"start","name":"Bob","number":"555-1234"})' | nc localhost 9001
 *    echo 'GB({"t":"musicinfo","artist":"Daft Punk","track":"Get Lucky","album":"RAM"})' | nc localhost 9001
 */

#ifdef SIMULATOR

#include "gadgetbridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctime>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
#define sock_errno WSAGetLastError()
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
typedef int socket_t;
#define SOCK_INVALID (-1)
#define sock_close close
#define sock_errno errno
#endif

#define GB_TCP_PORT 9001

/* ── State ───────────────────────────────────────────────────── */
static gb_callbacks_t cbs = {};
static socket_t listen_sock = SOCK_INVALID;
static socket_t client_sock = SOCK_INVALID;
static bool sim_connected = false;
static char rx_buf[512];
static int  rx_buf_len = 0;

/* ── Helpers: simple JSON extraction (duplicated from ESP32 ver) ── */
static bool json_get_string(const char *json, const char *key,
                            char *out, int max_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end) return false;
    int len = (int)(end - start);
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

/* ── Process a complete line ─────────────────────────────────── */
static void process_line(const char *line) {
    /* Skip leading whitespace, quotes, and UTF-8 BOM (PowerShell adds these) */
    while (*line == ' ' || *line == '\'' || *line == '"' ||
           ((unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)) {
        if ((unsigned char)line[0] == 0xEF) line += 3;  /* skip 3-byte BOM */
        else line++;
    }
    /* Trim trailing quotes/whitespace */
    static char trimmed[512];
    int len = (int)strlen(line);
    if (len >= (int)sizeof(trimmed)) len = (int)sizeof(trimmed) - 1;
    memcpy(trimmed, line, len);
    trimmed[len] = '\0';
    while (len > 0 && (trimmed[len-1] == '\'' || trimmed[len-1] == '"' ||
                       trimmed[len-1] == ' ')) {
        trimmed[--len] = '\0';
    }
    line = trimmed;

    printf("GB rx: [%s]\n", line);
    fflush(stdout);

    /* setTime(epoch) */
    if (strncmp(line, "setTime(", 8) == 0) {
        long epoch = atol(line + 8);
        if (epoch > 0 && cbs.on_time_set) {
            struct tm *t;
            time_t tt = (time_t)epoch;
            t = localtime(&tt);
            if (t) {
                cbs.on_time_set(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                                t->tm_hour, t->tm_min, t->tm_sec);
            }
        }
        return;
    }

    /* GB({json}) */
    fprintf(stderr, "  checking GB( prefix... line[0..2]=%02x %02x %02x\n",
            (unsigned char)line[0], (unsigned char)line[1], (unsigned char)line[2]);
    if (strncmp(line, "GB(", 3) == 0) {
        fprintf(stderr, "  GB( matched!\n");
        const char *json = line + 3;
        char json_buf[480];
        const char *end = strrchr(json, ')');
        int len = end ? (int)(end - json) : (int)strlen(json);
        fprintf(stderr, "  json len=%d\n", len);
        if (len >= (int)sizeof(json_buf)) len = (int)sizeof(json_buf) - 1;
        memcpy(json_buf, json, len);
        json_buf[len] = '\0';

        fprintf(stderr, "  GB json_buf: [%s]\n", json_buf);

        char type[32] = "";
        json_get_string(json_buf, "t", type, sizeof(type));

        fprintf(stderr, "  GB type: [%s]\n", type);

        if (strcmp(type, "notify") == 0) {
            gb_notification_t notif = {};
            int id = 0;
            json_get_int(json_buf, "id", &id);
            notif.id = (uint32_t)id;
            json_get_string(json_buf, "title", notif.title, GB_NOTIF_TITLE_MAX);
            json_get_string(json_buf, "body", notif.body, GB_NOTIF_BODY_MAX);
            json_get_string(json_buf, "src", notif.src, GB_NOTIF_SRC_MAX);
            printf("GB parsed notify: id=%d src=%s title=%s body=%s cb=%p\n",
                   id, notif.src, notif.title, notif.body, (void*)cbs.on_notification);
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
            json_get_int(json_buf, "tmin", &w.temp_min);
            json_get_int(json_buf, "tmax", &w.temp_max);
            json_get_int(json_buf, "srise", &w.sunrise);
            json_get_int(json_buf, "sset", &w.sunset);
            if (cbs.on_weather) cbs.on_weather(&w);
        }
        return;
    } else {
        fprintf(stderr, "  GB( NOT matched\n");
    }
}

/* ── Make socket non-blocking ────────────────────────────────── */
static void make_nonblocking(socket_t s) {
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
#else
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
#endif
}

/* ── Send to client ──────────────────────────────────────────── */
static void tcp_send(const char *str) {
    if (client_sock == SOCK_INVALID) return;
    send(client_sock, str, (int)strlen(str), 0);
}

/* ── Public API ──────────────────────────────────────────────── */

void gb_init(const char *device_name, const gb_callbacks_t *callbacks) {
    if (callbacks) cbs = *callbacks;
    rx_buf_len = 0;

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == SOCK_INVALID) {
        printf("GB: socket() failed\n");
        return;
    }

    /* Allow address reuse */
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(GB_TCP_PORT);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        printf("GB: bind() failed on port %d (errno=%d)\n", GB_TCP_PORT, sock_errno);
        sock_close(listen_sock);
        listen_sock = SOCK_INVALID;
        return;
    }

    listen(listen_sock, 1);
    make_nonblocking(listen_sock);
}

void gb_loop(void) {
    if (listen_sock == SOCK_INVALID) return;

    /* Accept new connections */
    if (client_sock == SOCK_INVALID) {
        struct sockaddr_in ca;
        int ca_len = sizeof(ca);
        socket_t s = accept(listen_sock, (struct sockaddr *)&ca, (socklen_t *)&ca_len);
        if (s != SOCK_INVALID) {
            client_sock = s;
            sim_connected = true;
            make_nonblocking(client_sock);
            printf("GB: client connected\n");
            gb_send_firmware_info("1.0-sim", "PocketWatch-SIM");
        }
    }

    /* Read from client */
    if (client_sock != SOCK_INVALID) {
        char tmp[256];
        int n = recv(client_sock, tmp, sizeof(tmp) - 1, 0);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                char ch = tmp[i];
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
            /* Process incomplete lines that look complete */
            if (rx_buf_len > 0) {
                rx_buf[rx_buf_len] = '\0';
                if (rx_buf[rx_buf_len - 1] == ')' || rx_buf[rx_buf_len - 1] == ';') {
                    process_line(rx_buf);
                    rx_buf_len = 0;
                }
            }
        } else if (n == 0) {
            /* Connection closed */
            printf("GB: client disconnected\n");
            sock_close(client_sock);
            client_sock = SOCK_INVALID;
            sim_connected = false;
        }
        /* n < 0 with EWOULDBLOCK/EAGAIN = no data, that's fine */
    }
}

void gb_send_battery(int percent, float voltage) {
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"status\",\"bat\":%d,\"volt\":%.2f}\n",
             percent, (double)voltage);
    tcp_send(buf);
}

void gb_send_steps(uint32_t steps) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"t\":\"act\",\"stp\":%u}\n",
             (unsigned)steps);
    tcp_send(buf);
}

void gb_send_firmware_info(const char *fw_version, const char *hw_version) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"ver\",\"fw\":\"%s\",\"hw\":\"%s\"}\n",
             fw_version, hw_version);
    tcp_send(buf);
}

void gb_send_music_cmd(const char *cmd) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"t\":\"music\",\"n\":\"%s\"}\n", cmd);
    tcp_send(buf);
}

bool gb_is_connected(void) {
    return sim_connected;
}

void gb_deinit(void) {
    if (client_sock != SOCK_INVALID) {
        sock_close(client_sock);
        client_sock = SOCK_INVALID;
    }
    if (listen_sock != SOCK_INVALID) {
        sock_close(listen_sock);
        listen_sock = SOCK_INVALID;
    }
    sim_connected = false;
    rx_buf_len = 0;
    printf("Gadgetbridge TCP mock stopped\n");
}

#endif /* SIMULATOR */
