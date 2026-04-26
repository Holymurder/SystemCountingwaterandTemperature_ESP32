/*
 * ============================================================
 *  WATER METER SYSTEM — ESP32 v5.0
 *  YF-S201 + DS18B20 + OLED SSD1306 + Keypad 4x4 + WiFi
 *  + Captive Portal + Daily Analytics + NTP + DS3231 RTC
 * ============================================================
 *  Libraries required (Library Manager):
 *  - U8g2 by oliver
 *  - Keypad by Mark Stanley
 *  - DallasTemperature by Miles Burton
 *  - OneWire by Jim Studt
 *  - RTClib by Adafruit
 *  DNSServer, WiFi, Preferences — built-in ESP32 core
 *
 *  WIRING:
 *  YF-S201:  RED->5V, BLACK->GND, YELLOW->GPIO15
 *  DS18B20:  VCC->3.3V, GND->GND, DATA->GPIO4 (+4.7kΩ to 3.3V)
 *  OLED:     VCC->3.3V, GND->GND, SCL->GPIO22, SDA->GPIO21
 *  Keypad rows:    R1->13, R2->12, R3->14, R4->27
 *  Keypad columns: C1->26, C2->25, C3->33, C4->32
 *  DS3231:   VCC->3.3V, GND->GND, SCL->GPIO22, SDA->GPIO21
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "config.h"
#include "globals.h"
#include "clock.h"
#include "storage.h"
#include "analytics.h"
#include "flow.h"
#include "display.h"
#include "input.h"
#include "webserver.h"

// ============================================================
//  Global variable definitions (declared extern in globals.h)
// ============================================================

volatile uint32_t pulseCount = 0;
float flowLPM   = 0.0f;
float totalVol  = 0.0f;
float totalCost = 0.0f;

float waterTemp = -127.0f;

float tarCold = 0.08f;
float tarWarm = 0.15f;
float tarHot  = 0.35f;

float tempThreshMin     = 0.0f;
float tempThreshMax     = 100.0f;
bool  tempThreshEnabled = false;

DayRecord dayLog[MAX_DAYS];
int       dayLogCount = 0;
char      todayDate[12] = "00.00.0000";
float     todayCold = 0.0f;
float     todayWarm = 0.0f;
float     todayHot  = 0.0f;
float     todayCost = 0.0f;

bool wifiOn      = true;
bool ntpSynced   = false;
bool rtcOk       = false;

char settingsPass[8]  = "1234";
char homeSSID[32]     = "";
char homePass[64]     = "";
char defaultSSID[32]  = "";
char defaultPass[64]  = "";
bool settingsUnlocked = false;

String foundNetworks[MAX_NETWORKS];
int    foundNetworkCount = 0;
int    selectedNetwork   = 0;
int    wifiListScroll    = 0;

// ============================================================
//  Hardware objects
// ============================================================

DNSServer       dnsServer;
OneWire         oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

// ============================================================
//  Timers
// ============================================================

static uint32_t tFlow = 0, tDisp = 0, tSave = 0, tWifi = 0;
static uint32_t lastTempRead  = 0;
static uint32_t lastNtpCheck  = 0;
static uint32_t lastReconnect = 0;

// ============================================================
//  setup()
// ============================================================

void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] Water Meter v5.0");

  Wire.begin(21, 22);

  // DS3231 RTC
  if (rtc.begin()) {
    rtcOk = true;
    if (rtc.lostPower()) {
      Serial.println("[RTC] Lost power — setting compile-time fallback");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    } else {
      ntpSynced = true; // RTC has valid time
      DateTime now = rtc.now();
      Serial.printf("[RTC] OK: %02d.%02d.%04d %02d:%02d:%02d\n",
                    now.day(), now.month(), now.year(),
                    now.hour(), now.minute(), now.second());
    }
  } else {
    Serial.println("[RTC] DS3231 NOT found!");
  }

  // OLED splash
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(15, 20, "WATER METER v5.0");
  u8g2.drawStr(20, 35, "Starting...");
  u8g2.sendBuffer();

  loadData();
  loadDayLog();

  // DS18B20 initial read
  tempSensor.begin();
  tempSensor.setResolution(12);
  tempSensor.requestTemperatures();
  delay(800);
  float t = tempSensor.getTempCByIndex(0);
  waterTemp = (t == DEVICE_DISCONNECTED_C || t <= -50.0f) ? -127.0f : t;
  Serial.printf("[DS18B20] %.2f C\n", waterTemp);

  // Flow sensor
  flowSetup();

  // WiFi AP + home connect
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(500);

  bool homeConnected = false;
  if (strlen(defaultSSID) > 0) {
    strncpy(homeSSID, defaultSSID, 32);
    strncpy(homePass, defaultPass, 64);
    Serial.printf("[WiFi] Auto-connect: %s\n", defaultSSID);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 20, "Auto-connecting...");
    u8g2.drawStr(0, 32, defaultSSID);
    u8g2.sendBuffer();

    homeConnected = connectHomeWifi();
  } else {
    Serial.println("[WiFi] No default WiFi — set in Settings>2");
  }

  if (homeConnected) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 20, "Syncing time...");
    u8g2.sendBuffer();
    syncNTP();
  }

  // Resolve today's date
  if (ntpSynced || rtcOk) {
    char realDate[12];
    getCurrentDateStr(realDate);
    if (strcmp(todayDate, "00.00.0000") != 0 &&
        strcmp(todayDate, realDate) != 0) {
      Serial.printf("[BOOT] Date changed: %s -> %s\n", todayDate, realDate);
      commitTodayToLog();
      strncpy(todayDate, realDate, 12);
      todayCold = todayWarm = todayHot = todayCost = 0.0f;
      saveData();
    } else {
      strncpy(todayDate, realDate, 12);
    }
  }

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  webServer.begin();

  // Boot summary on OLED
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(10, 12, "WATER METER v5.0");
  u8g2.drawHLine(0, 14, 128);
  u8g2.drawStr(0, 26, "AP:  WaterMeter");

  char timeBuf[12], dateBuf[12];
  if (getCurrentDateTimeStr(dateBuf, timeBuf)) {
    u8g2.drawStr(0, 36, dateBuf);
    u8g2.drawStr(70, 36, timeBuf);
    u8g2.drawStr(0, 46, "NTP/RTC: OK");
  } else {
    u8g2.drawStr(0, 36, todayDate);
    u8g2.drawStr(0, 46, "NTP: no sync");
  }

  if (WiFi.status() == WL_CONNECTED) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Home: %s", WiFi.SSID().c_str());
    u8g2.drawStr(0, 56, buf);
  } else {
    u8g2.drawStr(0, 56, "Set WiFi in Settings");
  }
  u8g2.sendBuffer();
  delay(3000);

  menu = M_MAIN;
  Serial.println("[READY]");
}

// ============================================================
//  loop()
// ============================================================

void loop() {
  uint32_t now = millis();

  // Flow calculation
  if (now - tFlow >= I_FLOW) { tFlow = now; calcFlow(); }

  // Temperature async read
  if (now - lastTempRead >= TEMP_INTERVAL) {
    lastTempRead = now;
    float t = tempSensor.getTempCByIndex(0);
    waterTemp = (t == DEVICE_DISCONNECTED_C || t <= -50.0f) ? -127.0f : t;
    tempSensor.requestTemperatures();
  }

  // Display refresh
  if (now - tDisp >= I_DISP) { tDisp = now; updateDisplay(); }

  // Periodic NVS save + log commit
  if (now - tSave >= I_SAVE) {
    tSave = now;
    saveData();
    commitTodayToLog();
    Serial.printf("[NVS] %.3fL / %.2fUAH / %s\n", totalVol, totalCost, todayDate);
  }

  // Web server & DNS
  if (now - tWifi >= I_WIFI) {
    tWifi = now;
    dnsServer.processNextRequest();
    handleWifi();
  }

  // NTP retry
  if (!ntpSynced && WiFi.status() == WL_CONNECTED && now - lastNtpCheck >= NTP_RETRY) {
    lastNtpCheck = now;
    syncNTP();
  }

  // WiFi reconnect
  if (WiFi.status() != WL_CONNECTED && strlen(homeSSID) > 0 && now - lastReconnect >= 60000) {
    lastReconnect = now;
    Serial.println("[WiFi] Reconnecting...");
    WiFi.begin(homeSSID, homePass);
  }

  // Day rollover check
  checkDayChange();

  // Keypad
  char key = keypad.getKey();
  if (key) {
    Serial.printf("[KEY] %c\n", key);
    handleKey(key);
  }
}
