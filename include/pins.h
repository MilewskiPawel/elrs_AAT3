#pragma once
#include <Arduino.h>

// Pin/feature definitions are primarily set via platformio.ini build_flags.
// These fallbacks target NodeMCU V3 (ESP8266 / ESP-12E).

#ifndef PIN_OLED_SDA
#define PIN_OLED_SDA 4   // D2
#endif
#ifndef PIN_OLED_SCL
#define PIN_OLED_SCL 5   // D1
#endif
#ifndef OLED_ADDR
#define OLED_ADDR 0x3C
#endif
#ifndef OLED_MOUNT_ROTATED_180
#define OLED_MOUNT_ROTATED_180 0
#endif

#ifndef PIN_SERVO_AZIM
#define PIN_SERVO_AZIM 14 // D5
#endif
#ifndef PIN_SERVO_ELEV
#define PIN_SERVO_ELEV 12 // D6
#endif

#ifndef PIN_WS2812
#define PIN_WS2812 2      // D4
#endif
#ifndef WS2812_COUNT
#define WS2812_COUNT 4
#endif

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
