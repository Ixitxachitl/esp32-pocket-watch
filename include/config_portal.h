#pragma once
/*
 *  config_portal.h
 *  WiFi + Web configuration portal for the pocket-watch.
 *
 *  Features:
 *    • AP mode (fallback) or STA mode (connects to saved WiFi)
 *    • Web UI to configure WiFi SSID/password
 *    • NTP server & POSIX timezone string
 *    • Manual time entry → sets ESP32 system clock
 *    • All settings persisted in NVS via Preferences
 */

#include <Arduino.h>

// Call once from setup()
void configPortalBegin();

// Apply saved screen order (call after screen_manager_init)
void configPortalApplyScreenOrder();

// Call from loop() – services the web server (non-blocking)
void configPortalLoop();

// Returns true when WiFi STA is connected
bool configWiFiConnected();

// Returns true when "Manual Time Only" is enabled (skip NTP & BT sync)
bool configIsManualTimeOnly();
