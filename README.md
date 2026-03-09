# ESP32 Pocket Watch

A feature-rich smartwatch firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.75C** development board, built with LVGL and Arduino.

![ESP32-S3](https://img.shields.io/badge/ESP32--S3-240MHz-blue)
![Display](https://img.shields.io/badge/Display-466×466_AMOLED-green)
![LVGL](https://img.shields.io/badge/LVGL-8.4-orange)

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-S3 (240 MHz, PSRAM) |
| Display | CO5300 466×466 QSPI AMOLED |
| Touch | CST9217 capacitive |
| IMU | QMI8658 6-axis (accel + gyro) |
| PMIC | AXP2101 (battery, charging) |
| Audio | ES8311 I2S codec |
| Connectivity | WiFi + BLE (NimBLE) |

## Screens

The watch has **11 swipeable screens** with smooth horizontal transitions:

1. **Clock Face** — Analogue watch with orrery animation, battery arc, step counter, date, and notification popup
2. **Stopwatch** — Start/stop/reset with seg7 display
3. **Timer** — Configurable countdown with preset memory
4. **Alarm** — Daily alarm with hour/minute setting
5. **Weather** — Current conditions from Gadgetbridge (temp, humidity, wind, forecast)
6. **Radar** — WiFi network scanner with animated sweep
7. **Compass** — 3D ball compass with gyro-integrated heading
8. **Media** — Music playback control via BLE
9. **Level** — Spirit level / inclinometer with calibration (long-press to zero)
10. **System Info** — Battery, WiFi, BLE status, memory, uptime
11. **Navigation** — Turn-by-turn directions from Gadgetbridge

## Features

- **Gadgetbridge BLE integration** — Time sync, weather, notifications, music control, navigation via [Gadgetbridge](https://gadgetbridge.org/) (BangleJS protocol)
- **WiFi config portal** — Web-based settings at the watch's IP; configure WiFi, NTP, screen order, and more
- **NTP time sync** — Automatic time from the internet when WiFi is connected
- **Power management** — Screen on/off toggle via power button; CPU drops to 80 MHz when display is off
- **Screen timeout** — Configurable auto-off (15s / 30s / 60s / always on)
- **Charging indicator** — Green arc + lightning bolt icon when USB-charging
- **Software pedometer** — Step counting from accelerometer data
- **Compass calibration** — Gyro-integrated yaw with gravity projection
- **Level calibration** — Long-press to set current orientation as flat
- **Settings persistence** — All preferences saved to NVS flash
- **Sound** — Startup chime, notification ding, alarm via I2S audio
- **12/24 hour mode** — Configurable via settings menu
- **Metric / Imperial** — Distance unit toggle for navigation

## Building

### Prerequisites

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- USB-C cable connected to the Waveshare board

### Hardware Build

```bash
# Build
pio run -e esp32s3

# Build and upload
pio run -e esp32s3 --target upload --upload-port COM3

# Serial monitor
pio device monitor -b 115200
```

### Desktop Simulator

A native SDL2 simulator lets you develop/test the UI without hardware:

```bash
# Requires SDL2 development libraries (e.g. via MSYS2 on Windows)
pio run -e simulator
```

## Project Structure

```
├── include/            # Header files
│   ├── pin_config.h    # GPIO pin definitions
│   ├── lv_conf.h       # LVGL configuration
│   └── *.h             # Screen and module headers
├── src/
│   ├── main.cpp        # Arduino entry point, hardware init, main loop
│   ├── clock_face.c    # Clock face with orrery, menu, notifications
│   ├── screen_manager.c# Horizontal swipe navigation, watch frame
│   ├── gadgetbridge.cpp# BLE Gadgetbridge protocol (NUS)
│   ├── config_portal.cpp# WiFi web config portal
│   ├── compass_screen.c# 3D compass with gyro integration
│   ├── level_screen.c  # Spirit level with calibration
│   ├── weather_screen.c# Weather display
│   ├── nav_screen.c    # Turn-by-turn navigation
│   ├── radar_screen.c  # WiFi radar scanner
│   ├── media_screen.c  # Music remote control
│   ├── alarm_screen.c  # Alarm clock
│   ├── timer_screen.c  # Countdown timer
│   ├── stopwatch.c     # Stopwatch
│   ├── sysinfo_screen.c# System information
│   ├── seg7.c          # 7-segment display widget
│   ├── sound.cpp       # I2S audio playback
│   ├── sim_main.cpp    # SDL simulator entry point
│   └── lv_font_*.c     # Custom icon fonts (MDI)
├── platformio.ini      # PlatformIO build configuration
└── .gitignore
```

## Configuration

On first boot, the watch creates a WiFi access point. Connect to it and open the config portal in a browser to:

- Connect to your home WiFi network
- Set NTP server and timezone
- Reorder or hide screens
- Set time manually

## Acknowledgements

### Fonts

- **[Montserrat](https://github.com/JulietaUla/Montserrat)** — Julieta Ulanovsky, Sol Matas, Juan Pablo del Peral, Jacques Le Bailly. Licensed under the [SIL Open Font License 1.1](https://scripts.sil.org/OFL). Bundled with LVGL.
- **[Material Design Icons](https://github.com/Templarian/MaterialDesign-Webfont)** — Austin Andrews & contributors. Licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) (icons) and [SIL OFL 1.1](https://scripts.sil.org/OFL) (font). Used for navigation screen icons.
- **[Weather Icons](https://github.com/erikflowers/weather-icons)** — Erik Flowers & Lukas Bischoff. Licensed under the [SIL Open Font License 1.1](https://scripts.sil.org/OFL). Used for weather condition glyphs.
- **LVGL built-in symbols** — Subset of [FontAwesome](https://fontawesome.com/), included in LVGL. Licensed under the [SIL OFL 1.1](https://scripts.sil.org/OFL) (font) and [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) (icons).

### Libraries

- **[LVGL](https://github.com/lvgl/lvgl)** — Light and Versatile Graphics Library (MIT)
- **[Arduino_GFX](https://github.com/moononournation/Arduino_GFX)** — GFX Library for Arduino (LGPL-2.1)
- **[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)** — BLE stack for ESP32 (Apache-2.0)
- **[SensorLib](https://github.com/lewisxhe/SensorsLib)** — QMI8658 IMU driver (MIT)
- **[XPowersLib](https://github.com/lewisxhe/XPowersLib)** — AXP2101 PMIC driver (MIT)

## License

This project is licensed under the [GNU General Public License v2.0](LICENSE).
