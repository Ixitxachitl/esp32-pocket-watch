/*
 *  config_portal.cpp
 *  WiFi AP/STA + web configuration portal + NTP for the pocket-watch.
 */

#include "config_portal.h"
#include "screen_manager.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>

// ── Defaults ────────────────────────────────────────────────────────────────
static const char *AP_SSID     = "PocketWatch";
static const char *AP_PASS     = "12345678";    // min 8 chars for WPA2
static const char *DEFAULT_NTP = "pool.ntp.org";
static const char *DEFAULT_TZ  = "GMT0";        // POSIX TZ string

// ── Persistent storage ─────────────────────────────────────────────────────
static Preferences prefs;
static String wifi_ssid;
static String wifi_pass;
static String ntp_server;
static String tz_string;
static String screen_order_str;   // Comma-separated physical IDs of enabled screens
static bool   wifi_ap_only = false;  // true = AP-only, false = STA+AP

// ── State ───────────────────────────────────────────────────────────────────
static WebServer server(80);
static DNSServer dnsServer;
static bool      sta_connected    = false;
static bool      ntp_synced       = false;
static bool      manual_time_only = false;  // skip NTP & BT time sync

// ── Forward declarations ────────────────────────────────────────────────────
static void loadSettings();
static void saveSettings();
static void startWiFi();
static void startNTP();
static void setupRoutes();
static void handleRoot();
static void handleSave();
static void handleSetTime();
static void handleStatus();
static void handleGetScreens();
static void handleSaveScreens();
static String buildHTML();

// ─────────────────────────────────────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────────────────────────────────────

void configPortalBegin() {
    loadSettings();
    startWiFi();
    if (!manual_time_only) startNTP();

    /* Captive portal: redirect all DNS queries to our AP IP */
    dnsServer.start(53, "*", WiFi.softAPIP());

    setupRoutes();
    server.begin();

    Serial.println("Config portal started on port 80");
}

void configPortalApplyScreenOrder() {
    // Apply saved screen order (must be called after screen_manager_init)
    if (screen_order_str.length() > 0) {
        int ids[SCR_COUNT];
        int count = 0;
        int start = 0;
        for (int i = 0; i <= (int)screen_order_str.length() && count < SCR_COUNT; i++) {
            if (i == (int)screen_order_str.length() || screen_order_str[i] == ',') {
                if (i > start) ids[count++] = screen_order_str.substring(start, i).toInt();
                start = i + 1;
            }
        }
        screen_manager_set_screen_order(ids, count);
    }
}

void configPortalLoop() {
    dnsServer.processNextRequest();
    server.handleClient();

    // If STA mode and not yet connected, keep checking
    if (!sta_connected && WiFi.status() == WL_CONNECTED) {
        sta_connected = true;
        Serial.print("WiFi STA connected – IP: ");
        Serial.println(WiFi.localIP());
        /* Turn off AP once STA is connected */
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        dnsServer.stop();
        Serial.println("WiFi: AP disabled (STA connected)");
    }
    /* If STA was connected but lost connection, re-enable AP */
    if (sta_connected && WiFi.status() != WL_CONNECTED) {
        sta_connected = false;
        Serial.println("WiFi: STA disconnected, re-enabling AP");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAPConfig(IPAddress(192,168,4,1),
                          IPAddress(192,168,4,1),
                          IPAddress(255,255,255,0));
        WiFi.softAP(AP_SSID, AP_PASS);
        dnsServer.start(53, "*", WiFi.softAPIP());
        WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    }

    // Try NTP sync once we're connected (skip if manual-only)
    if (!manual_time_only && sta_connected && !ntp_synced) {
        struct tm ti;
        if (getLocalTime(&ti, 10)) {
            ntp_synced = true;
            Serial.printf("NTP synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                          ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                          ti.tm_hour, ti.tm_min, ti.tm_sec);
            // NTP synced – system time is already set by configTime()
            Serial.println("NTP synced – system clock updated");
        }
    }
}

bool configWiFiConnected() {
    return sta_connected;
}

bool configIsManualTimeOnly() {
    return manual_time_only;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Settings persistence
// ─────────────────────────────────────────────────────────────────────────────

static void loadSettings() {
    prefs.begin("clock", true);  // read-only (creates namespace if missing)
    wifi_ssid        = prefs.isKey("ssid")    ? prefs.getString("ssid")    : "";
    wifi_pass        = prefs.isKey("pass")    ? prefs.getString("pass")    : "";
    ntp_server       = prefs.isKey("ntp")     ? prefs.getString("ntp")     : String(DEFAULT_NTP);
    tz_string        = prefs.isKey("tz")      ? prefs.getString("tz")      : String(DEFAULT_TZ);
    manual_time_only = prefs.isKey("manual_t") ? prefs.getBool("manual_t") : false;
    screen_order_str = prefs.isKey("scr_ord") ? prefs.getString("scr_ord") : "";
    wifi_ap_only     = prefs.isKey("ap_only") ? prefs.getBool("ap_only")  : false;
    prefs.end();

    Serial.printf("Settings: SSID='%s' NTP='%s' TZ='%s'\n",
                  wifi_ssid.c_str(), ntp_server.c_str(), tz_string.c_str());
}

static void saveSettings() {
    prefs.begin("clock", false);  // read-write
    prefs.putString("ssid", wifi_ssid);
    prefs.putString("pass", wifi_pass);
    prefs.putString("ntp",  ntp_server);
    prefs.putString("tz",   tz_string);
    prefs.putBool("manual_t", manual_time_only);
    prefs.putBool("ap_only", wifi_ap_only);
    prefs.putString("scr_ord", screen_order_str);
    prefs.end();
    Serial.println("Settings saved to NVS");
}

// ─────────────────────────────────────────────────────────────────────────────
//  WiFi
// ─────────────────────────────────────────────────────────────────────────────

static void startWiFi() {
    /* Set mode first, then configure AP */
    if (!wifi_ap_only && wifi_ssid.length() > 0) {
        WiFi.mode(WIFI_AP_STA);
    } else {
        WiFi.mode(WIFI_AP);
    }

    WiFi.softAPConfig(IPAddress(192,168,4,1),
                      IPAddress(192,168,4,1),
                      IPAddress(255,255,255,0));

    bool ok = WiFi.softAP(AP_SSID, AP_PASS);
    Serial.printf("WiFi: softAP %s  IP=%s\n",
                  ok ? "OK" : "FAIL",
                  WiFi.softAPIP().toString().c_str());

    WiFi.setSleep(false);    // keep AP responsive
    delay(500);              // let DHCP server stabilise

    if (!wifi_ap_only && wifi_ssid.length() > 0) {
        WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
        Serial.printf("WiFi: STA connecting to '%s'\n", wifi_ssid.c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  NTP
// ─────────────────────────────────────────────────────────────────────────────

static void startNTP() {
    configTzTime(tz_string.c_str(), ntp_server.c_str());
    Serial.printf("NTP configured: server='%s' tz='%s'\n",
                  ntp_server.c_str(), tz_string.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
//  Web routes
// ─────────────────────────────────────────────────────────────────────────────

static void setupRoutes() {
    server.on("/",        HTTP_GET,  handleRoot);
    server.on("/save",    HTTP_POST, handleSave);
    server.on("/settime", HTTP_POST, handleSetTime);
    server.on("/status",  HTTP_GET,  handleStatus);
    server.on("/screens", HTTP_GET,  handleGetScreens);
    server.on("/screens", HTTP_POST, handleSaveScreens);
    server.on("/scan",    HTTP_GET,  []() {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED) {
            WiFi.scanNetworks(true);  /* start async scan */
            server.send(200, "application/json", "{\"scanning\":true}");
            return;
        }
        if (n == WIFI_SCAN_RUNNING) {
            server.send(200, "application/json", "{\"scanning\":true}");
            return;
        }
        String json = "{\"networks\":[";
        for (int i = 0; i < n; i++) {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"";
            String ssid = WiFi.SSID(i);
            ssid.replace("\\", "\\\\"); ssid.replace("\"", "\\\"");
            json += ssid;
            json += "\",\"rssi\":" + String(WiFi.RSSI(i));
            json += ",\"enc\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
            json += "}";
        }
        json += "]}";
        WiFi.scanDelete();
        server.send(200, "application/json", json);
    });

    /* Captive-portal detection: return the exact responses each OS
       expects so they conclude "internet is reachable" and do NOT
       pop up a limited captive-portal mini-browser. */
    server.on("/generate_204",     HTTP_GET, []() { server.send(204); });                          /* Android  */
    server.on("/connecttest.txt",  HTTP_GET, []() { server.send(200, "text/plain", "Microsoft Connect Test"); }); /* Win10/11 */
    server.on("/fwlink",           HTTP_GET, []() { server.sendHeader("Location", "http://192.168.4.1/"); server.send(302); }); /* Windows  */
    server.on("/redirect",        HTTP_GET,  []() { server.sendHeader("Location", "http://192.168.4.1/"); server.send(302); }); /* Win NCSI */
    server.on("/canonical.html",   HTTP_GET, []() { server.send(200, "text/html", "<html><body>OK</body></html>"); });          /* Firefox  */
    server.on("/hotspot-detect.html", HTTP_GET, []() {
        server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    }); /* Apple */
    /* Any other unknown path → redirect to config page */
    server.onNotFound([]() {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302);
    });
}

// ── GET / ───────────────────────────────────────────────────────────────────
static void handleRoot() {
    String html = buildHTML();
    Serial.printf("handleRoot: sending %d bytes\n", html.length());
    server.send(200, "text/html", html);
}

// ── POST /save  (WiFi + NTP settings) ───────────────────────────────────────
static void handleSave() {
    wifi_ssid        = server.arg("ssid");
    wifi_pass        = server.arg("pass");
    ntp_server       = server.arg("ntp");
    tz_string        = server.arg("tz");
    manual_time_only = server.hasArg("manual_time");
    wifi_ap_only     = server.hasArg("ap_only");
    if (ntp_server.isEmpty()) ntp_server = DEFAULT_NTP;
    if (tz_string.isEmpty())  tz_string  = DEFAULT_TZ;

    saveSettings();

    server.send(200, "text/html",
        "<!DOCTYPE html><html><body style='background:#111;color:#eee;font-family:sans-serif;text-align:center;padding:40px'>"
        "<h2>Settings saved!</h2><p>The watch will restart in 3 seconds&hellip;</p>"
        "<script>setTimeout(()=>location='/',5000)</script></body></html>");

    delay(1500);
    ESP.restart();
}

// ── POST /settime  (manual time → system clock) ─────────────────────────────────────
static void handleSetTime() {
    int yr  = server.arg("year").toInt();
    int mo  = server.arg("month").toInt();
    int dy  = server.arg("day").toInt();
    int hr  = server.arg("hour").toInt();
    int mn  = server.arg("minute").toInt();
    int sc  = server.arg("second").toInt();

    String msg;
    if (yr >= 2024) {
        struct tm t = {};
        t.tm_year = yr - 1900; t.tm_mon = mo - 1; t.tm_mday = dy;
        t.tm_hour = hr; t.tm_min = mn; t.tm_sec = sc;
        t.tm_isdst = 0;  /* no DST – user enters exact local time */
        time_t epoch = mktime(&t);
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        Serial.printf("Time set manually: %04d-%02d-%02d %02d:%02d:%02d\n",
                      yr, mo, dy, hr, mn, sc);
        msg = "Time updated to " + String(yr) + "-" + String(mo) + "-" + String(dy)
            + " " + String(hr) + ":" + String(mn) + ":" + String(sc);
    } else {
        msg = "Error: invalid year";
    }

    server.send(200, "text/html",
        "<!DOCTYPE html><html><body style='background:#111;color:#eee;font-family:sans-serif;text-align:center;padding:40px'>"
        "<h2>" + msg + "</h2><p><a href='/' style='color:#4fc3f7'>Back</a></p></body></html>");
}

// ── GET /status (JSON) ──────────────────────────────────────────────────────
static void handleStatus() {
    String json = "{";
    bool ap_active = (WiFi.getMode() & WIFI_AP) != 0;
    json += "\"wifi_connected\":" + String(sta_connected ? "true" : "false");
    json += ",\"ntp_synced\":"    + String(ntp_synced ? "true" : "false");
    json += ",\"ip\":\""          + WiFi.localIP().toString() + "\"";
    json += ",\"ap_active\":"     + String(ap_active ? "true" : "false");
    json += ",\"ap_ip\":\""       + (ap_active ? WiFi.softAPIP().toString() : String("--")) + "\"";
    json += ",\"manual_time\":"   + String(manual_time_only ? "true" : "false");
    json += ",\"ap_only\":"       + String(wifi_ap_only ? "true" : "false");

    struct tm ti;
    if (getLocalTime(&ti, 5)) {
        char tbuf[32];
        snprintf(tbuf, sizeof(tbuf), "%04d-%02d-%02d %02d:%02d:%02d",
                 ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
        json += ",\"sys_time\":\"" + String(tbuf) + "\"";
    }

    json += "}";
    server.send(200, "application/json", json);
}

// ── GET /screens (JSON) ─────────────────────────────────────────────────────
static void handleGetScreens() {
    int order[SCR_COUNT];
    int count = screen_manager_get_screen_order(order, SCR_COUNT);

    String json = "{\"all\":[";
    for (int id = 0; id < SCR_COUNT; id++) {
        if (id > 0) json += ",";
        json += "{\"id\":" + String(id);
        json += ",\"name\":\"" + String(screen_manager_get_screen_name(id)) + "\"";
        json += ",\"locked\":" + String(id == SCR_CLOCK ? "true" : "false");
        // Check if enabled
        bool enabled = false;
        for (int i = 0; i < count; i++) { if (order[i] == id) { enabled = true; break; } }
        json += ",\"enabled\":" + String(enabled ? "true" : "false");
        json += "}";
    }
    json += "],\"order\":[";
    for (int i = 0; i < count; i++) {
        if (i > 0) json += ",";
        json += String(order[i]);
    }
    json += "]}";
    server.send(200, "application/json", json);
}

// ── POST /screens ───────────────────────────────────────────────────────────
static void handleSaveScreens() {
    String order_arg = server.arg("order");
    // Parse comma-separated physical IDs
    int ids[SCR_COUNT];
    int id_count = 0;
    int spos = 0;
    for (int i = 0; i <= (int)order_arg.length() && id_count < SCR_COUNT; i++) {
        if (i == (int)order_arg.length() || order_arg[i] == ',') {
            if (i > spos) ids[id_count++] = order_arg.substring(spos, i).toInt();
            spos = i + 1;
        }
    }
    screen_manager_set_screen_order(ids, id_count);

    // Persist
    screen_order_str = order_arg;
    prefs.begin("clock", false);
    prefs.putString("scr_ord", screen_order_str);
    prefs.end();

    server.send(200, "application/json", "{\"ok\":true}");
}

// ─────────────────────────────────────────────────────────────────────────────
//  HTML page (embedded)
// ─────────────────────────────────────────────────────────────────────────────

static String buildHTML() {
    // Read current RTC time for the manual-set defaults
    int rYear = 2026, rMon = 1, rDay = 1, rHour = 0, rMin = 0, rSec = 0;
    {
        struct tm ti;
        if (getLocalTime(&ti, 10)) {
            rYear = ti.tm_year + 1900; rMon = ti.tm_mon + 1; rDay = ti.tm_mday;
            rHour = ti.tm_hour; rMin = ti.tm_min; rSec = ti.tm_sec;
        }
    }

    String h = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pocket Watch Config</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#111;color:#eee;font-family:'Segoe UI',system-ui,sans-serif;padding:20px;max-width:480px;margin:auto}
h1{text-align:center;margin-bottom:8px;font-size:1.4em}
.sub{text-align:center;color:#888;margin-bottom:20px;font-size:.85em}
fieldset{border:1px solid #333;border-radius:8px;padding:16px;margin-bottom:16px}
legend{color:#4fc3f7;font-weight:600;padding:0 6px}
label{display:block;margin:8px 0 3px;font-size:.9em;color:#aaa}
input[type=text],input[type=password],input[type=number]{width:100%;padding:8px;border:1px solid #444;border-radius:4px;background:#222;color:#eee;font-size:.95em}
select{width:100%;padding:8px;border:1px solid #444;border-radius:4px;background:#222;color:#eee;font-size:.95em}
button{display:block;width:100%;padding:10px;margin-top:12px;border:none;border-radius:6px;font-size:1em;font-weight:600;cursor:pointer;transition:background .2s}
.btn-blue{background:#1976d2;color:#fff}.btn-blue:hover{background:#1565c0}
.btn-green{background:#2e7d32;color:#fff}.btn-green:hover{background:#1b5e20}
#status{background:#1a1a1a;border-radius:8px;padding:12px;margin-bottom:16px;font-size:.85em;line-height:1.6}
.ok{color:#4caf50}.err{color:#f44336}
.row{display:flex;gap:8px}.row>*{flex:1}
</style>
</head>
<body>
<h1>&#128348; Pocket Watch</h1>
<p class="sub">Configuration Portal</p>

<div id="status">Loading status&hellip;</div>

<form action="/save" method="POST">
<fieldset>
<legend>WiFi</legend>
<label style="display:flex;align-items:center;gap:8px;margin-bottom:10px;cursor:pointer">
<input type="checkbox" name="ap_only" style="width:18px;height:18px;accent-color:#f57c00" )rawhtml";
    if (wifi_ap_only) h += "checked";
    h += R"rawhtml(>
<span style="font-weight:600">AP Mode Only</span>
</label>
<p style="font-size:.78em;color:#777;margin-bottom:8px">
When on, the watch creates its own hotspot (<code>PocketWatch</code>) and won't connect to any router. Turn off to connect to your home WiFi.
</p>
<label>SSID</label>
<div style="display:flex;gap:6px;margin-bottom:4px">
<select id="ssid-sel" style="flex:1" onchange="if(this.value)document.getElementsByName('ssid')[0].value=this.value">
<option value="">&mdash; Select network &mdash;</option>
</select>
<button type="button" onclick="scanWifi()" id="scan-btn" style="width:auto;flex:none;padding:6px 12px;margin:0;font-size:.85em" class="btn-blue">Scan</button>
</div>
<input type="text" name="ssid" value=")rawhtml";
    h += wifi_ssid;
    h += R"rawhtml(" placeholder="Or type network name" autocomplete="off">
<label>Password</label>
<input type="password" name="pass" value=")rawhtml";
    h += wifi_pass;
    h += R"rawhtml(" placeholder="WiFi password" autocomplete="off">
</fieldset>

<fieldset>
<legend>Time Sync (NTP)</legend>
<label>NTP Server</label>
<input type="text" name="ntp" value=")rawhtml";
    h += ntp_server;
    h += R"rawhtml(" placeholder="pool.ntp.org">
<label>Timezone (POSIX TZ string)</label>
<input type="text" name="tz" value=")rawhtml";
    h += tz_string;
    h += R"rawhtml(" placeholder="GMT0">
<p style="margin-top:6px;font-size:.78em;color:#777">
Examples: <code>GMT0</code>, <code>GMT-1</code> (CET),
<code>EST5EDT,M3.2.0,M11.1.0</code> (US Eastern),
<code>AEST-10AEDT,M10.1.0,M4.1.0/3</code> (Sydney).<br>
See <a href="https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv" target="_blank" style="color:#4fc3f7">full list</a>.
</p>
<label style="display:flex;align-items:center;gap:8px;margin-top:10px;cursor:pointer">
<input type="checkbox" name="manual_time" style="width:18px;height:18px;accent-color:#f57c00" )rawhtml";
    if (manual_time_only) h += "checked";
    h += R"rawhtml(>
<span style="color:#f57c00;font-weight:600">Manual Time Only</span>
</label>
<p style="margin-top:4px;font-size:.78em;color:#777">
When enabled, NTP and Bluetooth time sync are disabled.
Use the manual form below to set the time.
Time priority: NTP &rarr; Bluetooth &rarr; Manual.
</p>
</fieldset>
<button type="submit" class="btn-blue">Save &amp; Restart</button>
</form>

<form action="/settime" method="POST" style="margin-top:16px">
<fieldset>
<legend>Set Time Manually</legend>
<div class="row">
  <div><label>Year</label><input type="number" name="year" min="2024" max="2099" value=")rawhtml";
    h += String(rYear);
    h += R"rawhtml("></div>
  <div><label>Month</label><input type="number" name="month" min="1" max="12" value=")rawhtml";
    h += String(rMon);
    h += R"rawhtml("></div>
  <div><label>Day</label><input type="number" name="day" min="1" max="31" value=")rawhtml";
    h += String(rDay);
    h += R"rawhtml("></div>
</div>
<div class="row">
  <div><label>Hour</label><input type="number" name="hour" min="0" max="23" value=")rawhtml";
    h += String(rHour);
    h += R"rawhtml("></div>
  <div><label>Minute</label><input type="number" name="minute" min="0" max="59" value=")rawhtml";
    h += String(rMin);
    h += R"rawhtml("></div>
  <div><label>Second</label><input type="number" name="second" min="0" max="59" value=")rawhtml";
    h += String(rSec);
    h += R"rawhtml("></div>
</div>
<button type="submit" class="btn-green">Set Time</button>
</fieldset>
</form>

<fieldset style="margin-top:16px">
<legend>Screens</legend>
<p style="font-size:.78em;color:#777;margin-bottom:10px">
Reorder and enable/disable screens. Drag to reorder. Clock is always first.
</p>
<div id="scr-list"></div>
<button type="button" class="btn-blue" onclick="saveScr()" style="margin-top:8px">Save Screen Order</button>
<div id="scr-msg" style="display:none;margin-top:8px;padding:8px;border-radius:4px;text-align:center;font-weight:600"></div>
</fieldset>

<script>
function scanWifi(){
  const btn=document.getElementById('scan-btn');
  const sel=document.getElementById('ssid-sel');
  btn.textContent='Scanning...';btn.disabled=true;
  sel.innerHTML='<option value="">Scanning\u2026</option>';
  let tries=0;
  function poll(){
    fetch('/scan').then(r=>r.json()).then(d=>{
      if(d.scanning&&tries++<20){setTimeout(poll,1000);return}
      sel.innerHTML='<option value="">\u2014 Select network \u2014</option>';
      if(d.networks){
        const seen=new Set();
        d.networks.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
          if(!n.ssid||seen.has(n.ssid))return;seen.add(n.ssid);
          const o=document.createElement('option');
          o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)'+(n.enc?' *':'');
          sel.appendChild(o);
        });
      }
      btn.textContent='Scan';btn.disabled=false;
    }).catch(()=>{btn.textContent='Scan';btn.disabled=false});
  }
  poll();
}
function pollStatus(){
  fetch('/status').then(r=>r.json()).then(s=>{
    let h='';
    h+='<b>WiFi:</b> '+(s.wifi_connected?'<span class="ok">Connected</span> ('+s.ip+')':'<span class="err">Not connected</span>')+' &nbsp; ';
    if(s.ap_active) h+='<b>AP:</b> '+s.ap_ip+'<br>'; else h+='<b>AP:</b> <span class="err">Off</span><br>';
    h+='<b>Time:</b> '+(s.sys_time?'<span class="ok">'+s.sys_time+'</span>':'<span class="err">Not set</span>')+' &nbsp; ';
    h+='<b>NTP:</b> '+(s.ntp_synced?'<span class="ok">Synced</span> '+(s.ntp_time||''):'<span class="err">Waiting</span>');
    var mode=s.wifi_connected&&!s.ap_active?'STA':s.ap_active&&!s.wifi_connected?'AP Only':'STA+AP';
    h+='<br><b>Mode:</b> '+mode;
    document.getElementById('status').innerHTML=h;
  }).catch(()=>{});
}
pollStatus(); setInterval(pollStatus,3000);

/* ── Screen order manager (drag & drop) ── */
let scrData=[];
let dragIdx=null;
async function loadScr(){
  try{
    const r=await fetch('/screens');
    const d=await r.json();
    scrData=[];
    const active=d.order.map(id=>d.all.find(s=>s.id===id));
    active.forEach(s=>{s.enabled=true;scrData.push(s)});
    d.all.forEach(s=>{
      if(!scrData.find(x=>x.id===s.id)){s.enabled=false;scrData.push(s)}
    });
    renderScr();
  }catch(e){}
}
function renderScr(){
  const el=document.getElementById('scr-list');
  el.innerHTML='';
  scrData.forEach((s,i)=>{
    const d=document.createElement('div');
    d.dataset.idx=i;
    d.style.cssText='display:flex;align-items:center;gap:8px;padding:10px 12px;margin:3px 0;background:#1a1a1a;border-radius:6px;border:1px solid #333;transition:transform .15s,opacity .15s';
    if(s.locked){
      d.innerHTML='<span style="color:#4fc3f7;font-size:1.1em;min-width:22px">&#128348;</span>'
        +'<span style="flex:1;font-weight:600">'+s.name+'</span>'
        +'<span style="font-size:.75em;color:#666">Always on</span>';
    }else{
      d.draggable=true;
      d.style.cursor='grab';
      d.style.opacity=s.enabled?'1':'.4';
      d.innerHTML='<span style="color:#555;font-size:1.2em;margin-right:2px">\u2630</span>'
        +'<span style="flex:1">'+s.name+'</span>'
        +'<label style="position:relative;display:inline-block;width:40px;height:22px;cursor:pointer" onclick="event.stopPropagation()">'
        +'<input type="checkbox" '+(s.enabled?'checked':'')+' onchange="scrTog('+i+',this.checked)" style="opacity:0;width:0;height:0">'
        +'<span style="position:absolute;inset:0;background:'+(s.enabled?'#2e7d32':'#444')+';border-radius:11px;transition:.3s"></span>'
        +'<span style="position:absolute;top:2px;left:'+(s.enabled?'20':'2')+'px;width:18px;height:18px;background:#eee;border-radius:50%;transition:.3s"></span>'
        +'</label>';
      d.addEventListener('dragstart',e=>{
        dragIdx=i;
        d.style.opacity='.5';
        e.dataTransfer.effectAllowed='move';
      });
      d.addEventListener('dragend',()=>{dragIdx=null;renderScr()});
      d.addEventListener('dragover',e=>{
        e.preventDefault();
        e.dataTransfer.dropEffect='move';
        d.style.borderColor='#4fc3f7';
      });
      d.addEventListener('dragleave',()=>{d.style.borderColor='#333'});
      d.addEventListener('drop',e=>{
        e.preventDefault();
        if(dragIdx!==null&&dragIdx!==i&&dragIdx>=1&&i>=1){
          const item=scrData.splice(dragIdx,1)[0];
          scrData.splice(i,0,item);
        }
        dragIdx=null;
        renderScr();
      });
      /* Touch drag support */
      d.addEventListener('touchstart',e=>{
        dragIdx=i;
        d.style.opacity='.5';
      },{passive:true});
      d.addEventListener('touchmove',e=>{
        const t=e.touches[0];
        const el=document.elementFromPoint(t.clientX,t.clientY);
        if(el){
          const row=el.closest('[data-idx]');
          if(row) row.style.borderColor='#4fc3f7';
        }
      },{passive:true});
      d.addEventListener('touchend',e=>{
        const t=e.changedTouches[0];
        const el=document.elementFromPoint(t.clientX,t.clientY);
        if(el){
          const row=el.closest('[data-idx]');
          if(row){
            const ti=parseInt(row.dataset.idx);
            if(dragIdx!==null&&dragIdx!==ti&&dragIdx>=1&&ti>=1){
              const item=scrData.splice(dragIdx,1)[0];
              scrData.splice(ti,0,item);
            }
          }
        }
        dragIdx=null;
        renderScr();
      });
    }
    el.appendChild(d);
  });
}
function scrTog(i,on){
  scrData[i].enabled=on;
  renderScr();
}
async function saveScr(){
  const ids=scrData.filter(s=>s.enabled&&!s.locked).map(s=>s.id).join(',');
  try{
    const r=await fetch('/screens',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'order='+ids});
    const d=await r.json();
    const m=document.getElementById('scr-msg');
    m.style.display='block';
    m.style.background=d.ok?'#1b5e20':'#b71c1c';
    m.textContent=d.ok?'Screen order saved!':'Error saving';
    setTimeout(()=>m.style.display='none',3000);
  }catch(e){}
}
loadScr();
</script>
</body>
</html>)rawhtml";

    return h;
}
