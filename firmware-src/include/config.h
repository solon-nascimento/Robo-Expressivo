#pragma once

#define FW_VERSION "0.3.0"
#define DEVICE_NAME "Kapibatchi"

#define PIN_SDA 8
#define PIN_SCL 9
#define PIN_TOUCH 7
#define PIN_BUZZER 5
#define PIN_VIBRATION 10
#define PIN_BATTERY 3

#define OLED_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#define SETUP_AP_SSID "Kapibatchi-local"
#define SETUP_IP_A 192
#define SETUP_IP_B 168
#define SETUP_IP_C 4
#define SETUP_IP_D 199

#define WIFI_SETUP_TIMEOUT_MS 300000UL

#define BATTERY_R1 100000.0f
#define BATTERY_R2 100000.0f
#define BATTERY_MIN_V 3.20f
#define BATTERY_MAX_V 4.20f
#define BATTERY_LOW_PERCENT 15
