#pragma once

#include <Arduino.h>

// CFace configuration.
// This is the only file to edit on the ESP32.
// Pins, I2C address, Wi-Fi, boot face, and how long the alert face stays up live here.
//
// If a password or SSID contains a backslash or double quote, escape it
// (\" and \\). An empty password is fine for an open network.

// OLED wiring. 4-pin SSD1306 I2C module, 128x64.
//   OLED VCC -> 3V3
//   OLED GND -> GND
//   OLED SDA -> GPIO 21
//   OLED SCL -> GPIO 22
static constexpr int OLED_SDA_PIN = 21;
static constexpr int OLED_SCL_PIN = 22;
static constexpr int OLED_WIDTH = 128;
static constexpr int OLED_HEIGHT = 64;

// Most 0.96" modules answer at 0x3C. Some answer at 0x3D.
// The firmware probes the primary address, then the fallback.
static constexpr uint8_t OLED_I2C_ADDR = 0x3C;
static constexpr uint8_t OLED_I2C_ADDR_FALLBACK = 0x3D;
static constexpr uint32_t OLED_I2C_HZ = 400000;

// Replace both placeholders before flashing. The board will not join Wi-Fi
// while the SSID is still "YOUR_WIFI_SSID"; the face still animates.
static constexpr char WIFI_SSID[] = "YOUR_WIFI_SSID";
static constexpr char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";

// POST http://<board-ip>:<HTTP_PORT>/notify
// Body: {"event":"prompt_complete","text":"New reply"}
// Optional "face" is "idle", "smoker", or "yesman". Any other value keeps the current face.
static constexpr uint16_t HTTP_PORT = 80;

// How long the eyes stay wide after a prompt completion, in milliseconds.
static constexpr uint32_t ALERT_HOLD_MS = 6500;

// How long the IP stays on the bottom line after Wi-Fi connects.
static constexpr uint32_t BOOT_IP_SHOW_MS = 6000;

// Bottom line used when a notify POST omits "text" on the idle face.
static constexpr char DEFAULT_ALERT_TEXT[] = "New reply";

// Stable face ids. BOOT_FACE is the one drawn from power-on.
// "idle" is the original face. "smoker" is the hi-tech cigarette face.
// "yesman" is the screen-headed grin. POST /notify can switch later with "face".
// Unknown values are ignored. Boot stays on BOOT_FACE (idle unless you change it).
static constexpr char FACE_IDLE[] = "idle";
static constexpr char FACE_SMOKER[] = "smoker";
static constexpr char FACE_YESMAN[] = "yesman";
static constexpr char BOOT_FACE[] = "idle";

// mDNS hostname. Some LANs resolve http://cface.local — the OLED IP is authoritative.
static constexpr char DEVICE_HOSTNAME[] = "cface";

// Notify LED on D25 (GPIO 25). A successful POST /notify — the same
// event that alerts the face — blinks this pin from loop(): 500 ms on,
// 500 ms off, for 3 seconds (three on pulses), then leaves it off.
static constexpr int NOTIFY_LED_PIN = 25;
static constexpr uint32_t NOTIFY_LED_ON_MS = 500;
static constexpr uint32_t NOTIFY_LED_OFF_MS = 500;
static constexpr uint32_t NOTIFY_LED_BLINK_MS = 3000;
static_assert(NOTIFY_LED_BLINK_MS == 3 * (NOTIFY_LED_ON_MS + NOTIFY_LED_OFF_MS),
              "notify LED is three 500 ms on/off pulses");

// Face potentiometer. 3-pin pot, any value from 5k to 100k.
//   Outer leg 1 -> 3V3
//   Outer leg 2 -> GND
//   Wiper (middle) -> GPIO 33
// GPIO 33 is on ADC1, which stays readable with Wi-Fi on (ADC2 pins such as
// GPIO 4 and 2 do not). The 12-bit range 0..4095 is split evenly across the
// registered faces. Swap the outer legs to reverse the direction.
// Momentary push button: one leg to BUTTON_PIN (INPUT_PULLUP), other leg to GND.
// A press clears the unread count.
static constexpr int BUTTON_PIN = 32;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

#define POT_PIN 33
// Time between samples, and how many recent samples are averaged.
static constexpr uint32_t POT_SAMPLE_MS = 20;
static constexpr int POT_AVG_SAMPLES = 8;
// Dead band in ADC counts. The knob must pass a face boundary by this much
// before the face changes, so a wiper resting on a boundary cannot flicker.
static constexpr int POT_DEADBAND = 120;
