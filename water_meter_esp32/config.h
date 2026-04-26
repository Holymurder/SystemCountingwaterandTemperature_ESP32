#pragma once

// ============================================================
//  WATER METER — config.h
//  Pins, constants, shared types
// ============================================================

// --- Hardware pins ---
#define FLOW_PIN   15
#define TEMP_PIN    4

// Keypad
#define KP_ROWS 4
#define KP_COLS 4

// --- Flow sensor ---
#define PULSES_PER_LITER 700.0f

// --- NTP ---
#define NTP_SERVER1      "pool.ntp.org"
#define NTP_SERVER2      "time.google.com"
#define GMT_OFFSET_SEC   7200
#define DAYLIGHT_SEC     3600

// --- WiFi AP ---
#define AP_SSID "WaterMeter"
#define AP_PASS "12345678"
#define DNS_PORT 53

// --- Timings (ms) ---
#define I_FLOW  1000
#define I_DISP   500
#define I_SAVE 30000
#define I_WIFI   200
#define TEMP_INTERVAL 2000
#define NTP_RETRY     30000

// --- Storage ---
#define MAX_DAYS    31
#define MAX_NETWORKS 10

// --- Daily record ---
struct DayRecord {
  char  date[12];
  float volCold;
  float volWarm;
  float volHot;
  float cost;
};
