/*
 * ============================================================
 *  WATER METER SYSTEM — ESP32 v5.0
 *  YF-S201 + DS18B20 + OLED SSD1306 + Keypad 4x4 + WiFi
 *  + Captive Portal + Daily Analytics + NTP + Settings Password
 * ============================================================
 *  Libraries (Library Manager):
 *  - U8g2 by oliver
 *  - Keypad by Mark Stanley
 *  - DallasTemperature by Miles Burton
 *  - OneWire by Jim Studt
 *  DNSServer, WiFi, Preferences, time.h — вбудовані в ESP32 core
 *
 *  WIRING:
 *  YF-S201:  RED->5V, BLACK->GND, YELLOW->GPIO27
 *  DS18B20:  VCC->3.3V, GND->GND, DATA->GPIO4 (+4.7kOhm to 3.3V)
 *  OLED:     VCC->3.3V, GND->GND, SCL->GPIO22, SDA->GPIO21
 *  Keypad rows:    R1->13, R2->12, R3->14, R4->26
 *  Keypad columns: C1->25, C2->33, C3->32, C4->15
 * ============================================================
 */

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Keypad.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>

#define FLOW_PIN  27
#define TEMP_PIN  4

const byte ROWS = 4;
const byte COLS = 4;
byte rowPins[ROWS] = {13, 12, 14, 26};
byte colPins[COLS] = {25, 33, 32, 15};
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

OneWire           oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);
float    waterTemp    = -127.0f;
uint32_t lastTempRead = 0;
const uint32_t TEMP_INTERVAL = 2000;

const char* AP_SSID = "WaterMeter";
const char* AP_PASS = "12345678";
WiFiServer  webServer(80);
DNSServer   dnsServer;
const byte  DNS_PORT = 53;
bool        wifiOn   = true;

bool        ntpSynced    = false;
uint32_t    lastNtpCheck = 0;
const uint32_t NTP_RETRY = 30000;
#define NTP_SERVER1    "pool.ntp.org"
#define NTP_SERVER2    "time.google.com"
#define GMT_OFFSET_SEC  7200
#define DAYLIGHT_SEC    3600


char settingsPass[8]  = "1234";
char homeSSID[32]     = "";
char homePass[64]     = "";
char defaultSSID[32]  = "";
char defaultPass[64]  = "";
bool settingsUnlocked = false;

#define MAX_NETWORKS 10
String foundNetworks[MAX_NETWORKS];
int    foundNetworkCount = 0;
int    selectedNetwork   = 0;
int    wifiListScroll    = 0;


float tarCold = 0.08f;
float tarWarm = 0.15f;
float tarHot  = 0.35f;

float getCurrentTariff() {
  if      (waterTemp <= -50.0f) return tarCold;
  else if (waterTemp < 20.0f)   return tarCold;
  else if (waterTemp <= 45.0f)  return tarWarm;
  else                          return tarHot;
}

const char* getTempCat() {
  if      (waterTemp <= -50.0f) return "N/A ";
  else if (waterTemp < 20.0f)   return "COLD";
  else if (waterTemp <= 45.0f)  return "WARM";
  else                          return "HOT ";
}

float tempThreshMin = 0.0f;
float tempThreshMax = 100.0f;
bool  tempThreshEnabled = false; 

bool isTempValid() {
  if (!tempThreshEnabled) return true;
  if (waterTemp <= -50.0f) return true;
  return (waterTemp >= tempThreshMin && waterTemp <= tempThreshMax);
}


volatile uint32_t pulseCount = 0;
float flowLPM   = 0.0f;
float totalVol  = 0.0f;
float totalCost = 0.0f;

struct DayRecord {
  char  date[12];
  float volCold;
  float volWarm;
  float volHot;
  float cost;
};

#define MAX_DAYS 31
DayRecord dayLog[MAX_DAYS];
int       dayLogCount = 0;

char  todayDate[12] = "00.00.0000";
float todayCold     = 0.0f;
float todayWarm     = 0.0f;
float todayHot      = 0.0f;
float todayCost     = 0.0f;

int fakeDayBackup   = 1;
int fakeMonthBackup = 1;
int fakeYearBackup  = 2026;

uint32_t tFlow = 0, tDisp = 0, tSave = 0, tWifi = 0;
const uint32_t I_FLOW = 1000;
const uint32_t I_DISP = 500;
const uint32_t I_SAVE = 30000;
const uint32_t I_WIFI = 200;

Preferences prefs;

bool getRealTime(struct tm* info) {
  if (!ntpSynced) return false;
  return getLocalTime(info);
}

bool getCurrentDateStr(char* out) {
  struct tm info;
  if (!getRealTime(&info)) return false;
  snprintf(out, 12, "%02d.%02d.%04d",
           info.tm_mday, info.tm_mon + 1, info.tm_year + 1900);
  return true;
}

bool getCurrentDateTimeStr(char* dateOut, char* timeOut) {
  struct tm info;
  if (!getRealTime(&info)) return false;
  snprintf(dateOut, 12, "%02d.%02d.%04d",
           info.tm_mday, info.tm_mon + 1, info.tm_year + 1900);
  snprintf(timeOut, 12, "%02d:%02d:%02d",
           info.tm_hour, info.tm_min, info.tm_sec);
  return true;
}

void syncNTP() {
  if (WiFi.status() != WL_CONNECTED) return;
  Serial.println("[NTP] Syncing...");
  configTime(GMT_OFFSET_SEC, DAYLIGHT_SEC, NTP_SERVER1, NTP_SERVER2);
  struct tm info;
  int attempts = 0;
  while (!getLocalTime(&info) && attempts < 10) {
    delay(500); attempts++;
  }
  if (getLocalTime(&info)) {
    ntpSynced = true;
    char dateStr[12], timeStr[12];
    snprintf(dateStr, 12, "%02d.%02d.%04d",
             info.tm_mday, info.tm_mon + 1, info.tm_year + 1900);
    snprintf(timeStr, 12, "%02d:%02d:%02d",
             info.tm_hour, info.tm_min, info.tm_sec);
    Serial.printf("[NTP] OK! %s %s\n", dateStr, timeStr);
    if (strcmp(todayDate, "00.00.0000") == 0)
      strncpy(todayDate, dateStr, 12);
  } else {
    Serial.println("[NTP] Failed!");
  }
}

bool connectHomeWifi() {
  if (strlen(homeSSID) == 0) return false;
  Serial.printf("[WiFi] Connecting to %s...\n", homeSSID);
  WiFi.begin(homeSSID, homePass);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); attempts++; Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected! IP: %s\n",
                  WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("[WiFi] Failed!");
  return false;
}

void saveDayLog() {
  prefs.begin("wlog", false);
  prefs.putInt("cnt", dayLogCount);
  for (int i = 0; i < dayLogCount; i++) {
    char key[8];
    snprintf(key, sizeof(key), "d%d", i);
    prefs.putBytes(key, &dayLog[i], sizeof(DayRecord));
  }
  prefs.end();
}

void loadDayLog() {
  prefs.begin("wlog", true);
  dayLogCount = prefs.getInt("cnt", 0);
  if (dayLogCount > MAX_DAYS) dayLogCount = 0;
  for (int i = 0; i < dayLogCount; i++) {
    char key[8];
    snprintf(key, sizeof(key), "d%d", i);
    prefs.getBytes(key, &dayLog[i], sizeof(DayRecord));
  }
  prefs.end();
}

void saveData() {
  prefs.begin("wm", false);
  prefs.putFloat("vol",   totalVol);
  prefs.putFloat("cost",  totalCost);
  prefs.putFloat("tc",    tarCold);
  prefs.putFloat("tw",    tarWarm);
  prefs.putFloat("th",    tarHot);
  prefs.putFloat("tdc",   todayCold);
  prefs.putFloat("tdw",   todayWarm);
  prefs.putFloat("tdh",   todayHot);
  prefs.putFloat("tdk",   todayCost);
  prefs.putString("tdd",  todayDate);
  prefs.putInt("fbday",   fakeDayBackup);
  prefs.putInt("fbmon",   fakeMonthBackup);
  prefs.putInt("fbyr",    fakeYearBackup);
  prefs.putString("hssid", homeSSID);
  prefs.putString("hpass", homePass);
  prefs.putString("dssid", defaultSSID);
  prefs.putString("dpass", defaultPass);
  prefs.putString("spass", settingsPass);
  prefs.putFloat("tmin", tempThreshMin);
  prefs.putFloat("tmax", tempThreshMax);
  prefs.putBool("ten",   tempThreshEnabled);
  prefs.end();
}

void loadData() {
  prefs.begin("wm", true);
  totalVol        = prefs.getFloat("vol",   0.0f);
  totalCost       = prefs.getFloat("cost",  0.0f);
  tarCold         = prefs.getFloat("tc",    0.08f);
  tarWarm         = prefs.getFloat("tw",    0.15f);
  tarHot          = prefs.getFloat("th",    0.35f);
  todayCold       = prefs.getFloat("tdc",   0.0f);
  todayWarm       = prefs.getFloat("tdw",   0.0f);
  todayHot        = prefs.getFloat("tdh",   0.0f);
  todayCost       = prefs.getFloat("tdk",   0.0f);
  fakeDayBackup   = prefs.getInt("fbday",   1);
  fakeMonthBackup = prefs.getInt("fbmon",   1);
  fakeYearBackup  = prefs.getInt("fbyr",    2026);
  String saved    = prefs.getString("tdd",  "00.00.0000");
  strncpy(todayDate, saved.c_str(), 12);
  String hs = prefs.getString("hssid", "");
  String hp = prefs.getString("hpass", "");
  String ds = prefs.getString("dssid", "");
  String dp = prefs.getString("dpass", "");
  String sp = prefs.getString("spass", "1234");
  strncpy(homeSSID,    hs.c_str(), 32);
  strncpy(homePass,    hp.c_str(), 64);
  strncpy(defaultSSID, ds.c_str(), 32);
  strncpy(defaultPass, dp.c_str(), 64);
  strncpy(settingsPass, sp.c_str(), 8);
  tempThreshMin     = prefs.getFloat("tmin", 0.0f);
  tempThreshMax     = prefs.getFloat("tmax", 100.0f);
  tempThreshEnabled = prefs.getBool("ten",   false);
  prefs.end();
}

void purgeOldRecords() {
  if (dayLogCount <= MAX_DAYS) return;
  for (int i = 0; i < dayLogCount - 1; i++) dayLog[i] = dayLog[i + 1];
  dayLogCount--;
}

int findDayRecord(const char* date) {
  for (int i = 0; i < dayLogCount; i++)
    if (strcmp(dayLog[i].date, date) == 0) return i;
  return -1;
}

void commitTodayToLog() {
  int idx = findDayRecord(todayDate);
  if (idx < 0) {
    if (dayLogCount >= MAX_DAYS) purgeOldRecords();
    idx = dayLogCount++;
  }
  strncpy(dayLog[idx].date, todayDate, 12);
  dayLog[idx].volCold = todayCold;
  dayLog[idx].volWarm = todayWarm;
  dayLog[idx].volHot  = todayHot;
  dayLog[idx].cost    = todayCost;
  saveDayLog();
}

void checkDayChange() {
  if (!ntpSynced) return;
  char currentDate[12];
  if (!getCurrentDateStr(currentDate)) return;
  if (strcmp(currentDate, todayDate) == 0) return;

  Serial.printf("[DAY] %s -> %s\n", todayDate, currentDate);
  commitTodayToLog();
  strncpy(todayDate, currentDate, 12);
  todayCold = 0.0f;
  todayWarm = 0.0f;
  todayHot  = 0.0f;
  todayCost = 0.0f;
  while (dayLogCount > MAX_DAYS) purgeOldRecords();
  saveData();
}

void IRAM_ATTR onPulse() { pulseCount++; }

#define PULSES_PER_LITER 700.0f

void calcFlow() {
  noInterrupts();
  uint32_t p = pulseCount;
  pulseCount  = 0;
  interrupts();

  float vol = p / PULSES_PER_LITER;
  flowLPM = vol * (60000.0f / I_FLOW);

  if (vol > 0.0f) {
    if (!isTempValid()) {
      Serial.printf("[THRESH] Temp %.1fC out of range [%.1f-%.1f] — skipped\n",
                    waterTemp, tempThreshMin, tempThreshMax);
      return;
    }
    float cost = vol * getCurrentTariff();
    if      (waterTemp <= -50.0f || waterTemp < 20.0f) todayCold += vol;
    else if (waterTemp <= 45.0f)                        todayWarm += vol;
    else                                                todayHot  += vol;
    todayCost += cost;
    totalVol  += vol;
    totalCost += cost;
  }
}

enum MenuState {
  M_MAIN,
  M_METER,
  M_ANALYTICS,
  M_SETTINGS,
  M_SETTINGS_PASS,
  M_SET_TAR_MENU,
  M_SET_TAR_VAL,
  M_WIFI_SCAN,
  M_WIFI_PASS,
  M_WIFI_DEFAULT,
  M_RESET,
  M_WIFI,
  M_CLOCK,
  M_SET_THRESH,     
  M_SET_THRESH_MIN, 
  M_SET_THRESH_MAX 
};
MenuState menu      = M_MAIN;
int       menuIdx   = 0;
String    inputBuf  = "";
int       inputStep = 0;
int       analyticsPage  = 0;
int       tarSelectIdx   = 0;
char      passInputBuf[8] = "";
int       passInputLen    = 0;


void drawTitle(const char* t) {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, t);
  u8g2.drawHLine(0, 12, 128);
}

void drawFooter(const char* t) {
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawHLine(0, 56, 128);
  u8g2.drawStr(0, 64, t);
}

const char* menuItems[] = {
  "1. Meter",
  "2. Analytics",
  "3. Settings",
  "4. WiFi info",
  "5. Clock",
  "6. Reset"
};
const int MENU_COUNT = 6;

void drawMainMenu() {
  u8g2.clearBuffer();
  drawTitle("=== MAIN MENU ===");
  u8g2.setFont(u8g2_font_6x10_tf);

  int startIdx = 0;
  if (menuIdx >= 4) startIdx = menuIdx - 3;

  for (int i = startIdx; i < MENU_COUNT && i < startIdx + 4; i++) {
    int y = 24 + (i - startIdx) * 10;
    if (i == menuIdx) {
      u8g2.drawBox(0, y - 8, 128, 10);
      u8g2.setDrawColor(0);
      u8g2.drawStr(2, y, menuItems[i]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(2, y, menuItems[i]);
    }
  }

  if (MENU_COUNT > 4) {
    u8g2.setFont(u8g2_font_5x7_tf);
    char pg[8];
    snprintf(pg, sizeof(pg), "%d/%d", menuIdx + 1, MENU_COUNT);
    u8g2.drawStr(100, 64, pg);
  }
  u8g2.sendBuffer();
}

void drawMeter() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  drawTitle("=== METER ===");
  char buf[32];

  snprintf(buf, sizeof(buf), "Flow: %.2f L/min", flowLPM);
  u8g2.drawStr(0, 24, buf);

  int barW = (int)((flowLPM / 5.0f) * 120.0f);
  if (barW > 120) barW = 120;
  u8g2.drawFrame(0, 26, 122, 5);
  if (barW > 0) u8g2.drawBox(1, 27, barW, 3);

  snprintf(buf, sizeof(buf), "Total: %.3f L", totalVol);
  u8g2.drawStr(0, 38, buf);

  snprintf(buf, sizeof(buf), "Cost:  %.2f UAH", totalCost);
  u8g2.drawStr(0, 48, buf);

  if (waterTemp <= -50.0f)
    u8g2.drawStr(0, 58, "Temp: -127C [N/A]");
  else {
    snprintf(buf, sizeof(buf), "Temp: %.1fC [%s]", waterTemp, getTempCat());
    u8g2.drawStr(0, 58, buf);
  }
  u8g2.sendBuffer();
}

void drawAnalytics() {
  u8g2.clearBuffer();
  drawTitle("=== ANALYTICS ===");
  u8g2.setFont(u8g2_font_5x7_tf);

  int totalPages = 1;
  for (int i = 0; i < dayLogCount; i++)
    if (strcmp(dayLog[i].date, todayDate) != 0) totalPages++;

  if (analyticsPage >= totalPages) analyticsPage = totalPages - 1;
  if (analyticsPage < 0) analyticsPage = 0;

  char buf[34];

  if (analyticsPage == 0) {
    snprintf(buf, sizeof(buf), "%s [TODAY]", todayDate);
    u8g2.drawStr(0, 22, buf);
    snprintf(buf, sizeof(buf), "Cold: %.3f L", todayCold);
    u8g2.drawStr(0, 31, buf);
    snprintf(buf, sizeof(buf), "Warm: %.3f L", todayWarm);
    u8g2.drawStr(0, 39, buf);
    snprintf(buf, sizeof(buf), "Hot:  %.3f L", todayHot);
    u8g2.drawStr(0, 47, buf);
    float tot = todayCold + todayWarm + todayHot;
    snprintf(buf, sizeof(buf), "Tot:%.3fL  %.2fUAH", tot, todayCost);
    u8g2.drawStr(0, 55, buf);
  } else {
    int page = 0;
    for (int i = dayLogCount - 1; i >= 0; i--) {
      if (strcmp(dayLog[i].date, todayDate) == 0) continue;
      page++;
      if (page != analyticsPage) continue;
      DayRecord& r = dayLog[i];
      u8g2.drawStr(0, 22, r.date);
      snprintf(buf, sizeof(buf), "Cold: %.3f L", r.volCold);
      u8g2.drawStr(0, 31, buf);
      snprintf(buf, sizeof(buf), "Warm: %.3f L", r.volWarm);
      u8g2.drawStr(0, 39, buf);
      snprintf(buf, sizeof(buf), "Hot:  %.3f L", r.volHot);
      u8g2.drawStr(0, 47, buf);
      float tot = r.volCold + r.volWarm + r.volHot;
      snprintf(buf, sizeof(buf), "Tot:%.3fL  %.2fUAH", tot, r.cost);
      u8g2.drawStr(0, 55, buf);
      break;
    }
  }

  snprintf(buf, sizeof(buf), "[2]next [8]prev %d/%d", analyticsPage+1, totalPages);
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(0, 63, buf);
  u8g2.sendBuffer();
}

void drawSettingsPass() {
  u8g2.clearBuffer();
  drawTitle("=== SETTINGS ===");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(20, 26, "Enter password:");

  String stars = "";
  for (int i = 0; i < passInputLen; i++) stars += "*";
  stars += "_";
  int sw = u8g2.getStrWidth(stars.c_str());
  u8g2.drawStr((128 - sw) / 2, 40, stars.c_str());

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 52, "[0-9] digit");
  u8g2.drawStr(0, 60, "[D] del  [#] OK  [C] cancel");
  u8g2.sendBuffer();
}

void drawSettings() {
  u8g2.clearBuffer();
  drawTitle("=== SETTINGS ===");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 22, "1. Change tariffs");
  u8g2.drawStr(0, 32, "2. WiFi for NTP");
  u8g2.drawStr(0, 42, "3. WiFi toggle");
  u8g2.drawStr(0, 52, "4. Temp threshold");

  char buf[24];
  u8g2.setFont(u8g2_font_5x7_tf);
  snprintf(buf, sizeof(buf), "   WiFi:%s Thr:%s",
           wifiOn ? "ON" : "OFF",
           tempThreshEnabled ? "ON" : "OFF");
  u8g2.drawStr(0, 62, buf);

  u8g2.sendBuffer();
}

void drawTarMenu() {
  u8g2.clearBuffer();
  drawTitle("=== TARIFFS ===");
  u8g2.setFont(u8g2_font_6x10_tf);

  char buf[28];
  snprintf(buf, sizeof(buf), "1. Cold:  %.3f UAH/L", tarCold);
  u8g2.drawStr(0, 24, buf);
  snprintf(buf, sizeof(buf), "2. Warm:  %.3f UAH/L", tarWarm);
  u8g2.drawStr(0, 34, buf);
  snprintf(buf, sizeof(buf), "3. Hot:   %.3f UAH/L", tarHot);
  u8g2.drawStr(0, 44, buf);

  int y = 24 + tarSelectIdx * 10;
  u8g2.drawFrame(0, y - 8, 128, 10);

  drawFooter("[1-3] select  [#] back");
  u8g2.sendBuffer();
}

void drawTarVal() {
  u8g2.clearBuffer();
  const char* names[] = {"COLD TARIFF", "WARM TARIFF", "HOT  TARIFF"};
  drawTitle(names[tarSelectIdx]);
  u8g2.setFont(u8g2_font_6x10_tf);

  char buf[32];
  float cur = (tarSelectIdx == 0) ? tarCold :
              (tarSelectIdx == 1) ? tarWarm : tarHot;
  snprintf(buf, sizeof(buf), "Current: %.3f UAH/L", cur);
  u8g2.drawStr(0, 24, buf);
  u8g2.drawStr(0, 34, "New value:");

  String disp = "> " + inputBuf + "_";
  u8g2.drawStr(0, 46, disp.c_str());

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 56, "[*] dot  [D] del");
  u8g2.drawStr(0, 63, "[#] OK   [C] cancel");
  u8g2.sendBuffer();
}


void drawThreshMenu() {
  u8g2.clearBuffer();
  drawTitle("=== TEMP THRESH ===");
  u8g2.setFont(u8g2_font_6x10_tf);

  char buf[32];
  snprintf(buf, sizeof(buf), "Status: %s",
           tempThreshEnabled ? "ENABLED" : "DISABLED");
  u8g2.drawStr(0, 24, buf);

  snprintf(buf, sizeof(buf), "1. Min: %.1f C", tempThreshMin);
  u8g2.drawStr(0, 35, buf);

  snprintf(buf, sizeof(buf), "2. Max: %.1f C", tempThreshMax);
  u8g2.drawStr(0, 45, buf);

  u8g2.drawStr(0, 55, "3. Toggle ON/OFF");

  drawFooter("[#] Back");
  u8g2.sendBuffer();
}


void drawThreshMin() {
  u8g2.clearBuffer();
  drawTitle("MIN TEMP THRESH");
  u8g2.setFont(u8g2_font_6x10_tf);

  char buf[32];
  snprintf(buf, sizeof(buf), "Current: %.1f C", tempThreshMin);
  u8g2.drawStr(0, 24, buf);
  u8g2.drawStr(0, 34, "New min (C):");

  String disp = "> " + inputBuf + "_";
  u8g2.drawStr(0, 46, disp.c_str());

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 56, "[*] dot  [D] del");
  u8g2.drawStr(0, 63, "[#] OK   [C] cancel");
  u8g2.sendBuffer();
}


void drawThreshMax() {
  u8g2.clearBuffer();
  drawTitle("MAX TEMP THRESH");
  u8g2.setFont(u8g2_font_6x10_tf);

  char buf[32];
  snprintf(buf, sizeof(buf), "Current: %.1f C", tempThreshMax);
  u8g2.drawStr(0, 24, buf);
  u8g2.drawStr(0, 34, "New max (C):");

  String disp = "> " + inputBuf + "_";
  u8g2.drawStr(0, 46, disp.c_str());

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 56, "[*] dot  [D] del");
  u8g2.drawStr(0, 63, "[#] OK   [C] cancel");
  u8g2.sendBuffer();
}


void drawWifiScan() {
  u8g2.clearBuffer();
  drawTitle("=== WiFi SCAN ===");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (foundNetworkCount == 0) {
    u8g2.drawStr(10, 35, "Scanning...");
    u8g2.sendBuffer();
    return;
  }

  for (int i = wifiListScroll;
       i < foundNetworkCount && i < wifiListScroll + 4; i++) {
    int y = 22 + (i - wifiListScroll) * 10;
    String name = foundNetworks[i];
    if (name.length() > 18) name = name.substring(0, 17) + "~";

    if (i == selectedNetwork) {
      u8g2.drawBox(0, y - 7, 128, 9);
      u8g2.setDrawColor(0);
      u8g2.drawStr(2, y, name.c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(2, y, name.c_str());
    }
  }

  u8g2.setFont(u8g2_font_4x6_tf);
  char pg[28];
  snprintf(pg, sizeof(pg), "[2/8]scroll [#]select %d/%d",
           selectedNetwork+1, foundNetworkCount);
  u8g2.drawStr(0, 63, pg);
  u8g2.sendBuffer();
}

void drawWifiPass() {
  u8g2.clearBuffer();
  drawTitle("=== WiFi PASS ===");
  u8g2.setFont(u8g2_font_5x7_tf);

  String netName = foundNetworks[selectedNetwork];
  if (netName.length() > 20) netName = netName.substring(0, 19) + "~";
  u8g2.drawStr(0, 22, netName.c_str());
  u8g2.drawStr(0, 31, "Password:");

  String disp = inputBuf + "_";
  if (disp.length() > 16) disp = "..." + disp.substring(disp.length() - 13);
  u8g2.drawStr(0, 41, disp.c_str());

  u8g2.drawStr(0, 52, "[*]=dot [0-9]=digit");
  u8g2.drawStr(0, 60, "[D]del [#]OK [C]cancel");
  u8g2.sendBuffer();
}

void drawWifiDefault() {
  u8g2.clearBuffer();
  drawTitle("=== DEFAULT WiFi ===");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 24, "Set as default?");
  String netName = foundNetworks[selectedNetwork];
  if (netName.length() > 20) netName = netName.substring(0, 19);
  u8g2.drawStr(0, 36, netName.c_str());
  u8g2.drawStr(0, 48, "Auto-connect on boot");
  drawFooter("[A]=YES   [#]=NO");
  u8g2.sendBuffer();
}

void drawReset() {
  u8g2.clearBuffer();
  drawTitle("=== RESET ===");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(10, 26, "Reset all data?");
  char buf[32];
  snprintf(buf, sizeof(buf), "Vol:  %.3f L", totalVol);
  u8g2.drawStr(10, 38, buf);
  snprintf(buf, sizeof(buf), "Cost: %.2f UAH", totalCost);
  u8g2.drawStr(10, 48, buf);
  drawFooter("[A]=YES reset   [#]=NO");
  u8g2.sendBuffer();
}

void drawWifi() {
  u8g2.clearBuffer();
  drawTitle("=== WiFi INFO ===");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (wifiOn) {
    u8g2.drawStr(0, 22, "AP: WaterMeter");
    u8g2.drawStr(0, 30, "Pass: 12345678");
    u8g2.drawStr(0, 38, "IP: 192.168.4.1");
    u8g2.drawHLine(0, 41, 128);

    if (WiFi.status() == WL_CONNECTED) {
      char buf[32];
      snprintf(buf, sizeof(buf), "Home: %s", WiFi.SSID().c_str());
      u8g2.drawStr(0, 50, buf);
      snprintf(buf, sizeof(buf), "NTP: %s", ntpSynced ? "Synced" : "Waiting");
      u8g2.drawStr(0, 58, buf);
    } else {
      u8g2.drawStr(0, 50, "Home WiFi: offline");
      u8g2.drawStr(0, 58, "NTP: no sync");
    }
  } else {
    u8g2.drawStr(20, 35, "WiFi DISABLED");
    u8g2.drawStr(5,  48, "Enable in Settings>3");
  }
  drawFooter("[#] Back");
  u8g2.sendBuffer();
}

void drawClock() {
  u8g2.clearBuffer();
  drawTitle("=== CLOCK ===");

  char dateStr[12] = "";
  char timeStr[12] = "";
  bool synced = getCurrentDateTimeStr(dateStr, timeStr);

  if (synced) {
    u8g2.setFont(u8g2_font_10x20_tf);
    int tw = u8g2.getStrWidth(timeStr);
    u8g2.drawStr((128 - tw) / 2, 38, timeStr);

    u8g2.setFont(u8g2_font_6x10_tf);
    int dw = u8g2.getStrWidth(dateStr);
    u8g2.drawStr((128 - dw) / 2, 52, dateStr);

    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(0, 64, "NTP: OK");
    if (WiFi.status() == WL_CONNECTED) {
      String ssid = "via " + WiFi.SSID();
      u8g2.drawStr(128 - u8g2.getStrWidth(ssid.c_str()), 64, ssid.c_str());
    }
  } else {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(10, 28, "NTP not synced!");
    u8g2.setFont(u8g2_font_5x7_tf);
    if (WiFi.status() == WL_CONNECTED) {
      u8g2.drawStr(0, 40, "Connected to WiFi,");
      u8g2.drawStr(0, 49, "waiting for NTP...");
    } else {
      u8g2.drawStr(0, 40, "No home WiFi!");
      u8g2.drawStr(0, 49, "Set WiFi in Settings");
    }
  }
  drawFooter("[#] Back");
  u8g2.sendBuffer();
}

bool handleNumeric(char key) {
  if (key >= '0' && key <= '9') {
    if (inputBuf.length() < 8) inputBuf += key;
    return false;
  }
  if (key == '*') {
    if (inputBuf.indexOf('.') < 0) inputBuf += '.';
    return false;
  }
  if (key == 'D') {
    if (inputBuf.length() > 0) inputBuf.remove(inputBuf.length() - 1);
    return false;
  }
  if (key == '#') return true;
  if (key == 'C') { inputBuf = ""; menu = M_SETTINGS; }
  return false;
}

void handleKey(char key) {

  if (menu == M_MAIN) {
    if (key == '8') menuIdx = max(menuIdx - 1, 0);
    if (key == '2') menuIdx = min(menuIdx + 1, MENU_COUNT - 1);
    if (key >= '1' && key <= '6') menuIdx = key - '1';
    if (key == '#' || key == 'A') {
      switch (menuIdx) {
        case 0: menu = M_METER;                          break;
        case 1: menu = M_ANALYTICS; analyticsPage = 0;  break;
        case 2:
          if (settingsUnlocked) {
            menu = M_SETTINGS;
          } else {
            menu = M_SETTINGS_PASS;
            passInputLen = 0;
            memset(passInputBuf, 0, sizeof(passInputBuf));
          }
          break;
        case 3: menu = M_WIFI;                           break;
        case 4: menu = M_CLOCK;                          break;
        case 5: menu = M_RESET;                          break;
      }
      menuIdx = 0;
    }
    return;
  }

  if (menu == M_SETTINGS_PASS) {
    if (key >= '0' && key <= '9') {
      if (passInputLen < 7) {
        passInputBuf[passInputLen++] = key;
        passInputBuf[passInputLen]   = '\0';
      }
      return;
    }
    if (key == 'D') {
      if (passInputLen > 0) passInputBuf[--passInputLen] = '\0';
      return;
    }
    if (key == '#') {
      if (strcmp(passInputBuf, settingsPass) == 0) {
        settingsUnlocked = true;
        menu = M_SETTINGS;
        Serial.println("[SETTINGS] Unlocked");
      } else {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(20, 32, "Wrong password!");
        u8g2.sendBuffer();
        delay(1200);
        passInputLen = 0;
        memset(passInputBuf, 0, sizeof(passInputBuf));
      }
      return;
    }
    if (key == 'C') {
      menu = M_MAIN;
      passInputLen = 0;
      memset(passInputBuf, 0, sizeof(passInputBuf));
    }
    return;
  }


  if (menu == M_METER)  { if (key == '#') menu = M_MAIN; return; }
  if (menu == M_CLOCK)  { if (key == '#') menu = M_MAIN; return; }
  if (menu == M_WIFI)   { if (key == '#') menu = M_MAIN; return; }


  if (menu == M_ANALYTICS) {
    if (key == '#') { menu = M_MAIN; analyticsPage = 0; }
    if (key == '2') {
      int total = 1;
      for (int i = 0; i < dayLogCount; i++)
        if (strcmp(dayLog[i].date, todayDate) != 0) total++;
      if (analyticsPage < total - 1) analyticsPage++;
    }
    if (key == '8') { if (analyticsPage > 0) analyticsPage--; }
    return;
  }


  if (menu == M_RESET) {
    if (key == 'A') {
      totalVol = 0; totalCost = 0;
      todayCold = 0; todayWarm = 0; todayHot = 0; todayCost = 0;
      dayLogCount = 0;
      saveData(); saveDayLog();
      menu = M_MAIN;
    }
    if (key == '#') menu = M_MAIN;
    return;
  }

  if (menu == M_SETTINGS) {
    if (key == '1') {
      menu = M_SET_TAR_MENU;
      tarSelectIdx = 0;
    }
    if (key == '2') {
      menu = M_WIFI_SCAN;
      foundNetworkCount = 0;
      selectedNetwork   = 0;
      wifiListScroll    = 0;
      updateDisplay();
      Serial.println("[WiFi] Scanning...");
      int n = WiFi.scanNetworks();
      foundNetworkCount = min(n, MAX_NETWORKS);
      for (int i = 0; i < foundNetworkCount; i++)
        foundNetworks[i] = WiFi.SSID(i);
      Serial.printf("[WiFi] Found %d\n", foundNetworkCount);
    }
    if (key == '3') {
      wifiOn = !wifiOn;
      if (wifiOn) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID, AP_PASS);
        delay(500);
        if (strlen(homeSSID) > 0) {
          WiFi.begin(homeSSID, homePass);
          int att = 0;
          while (WiFi.status() != WL_CONNECTED && att < 20) {
            delay(500); att++;
          }
          if (WiFi.status() == WL_CONNECTED) syncNTP();
        }
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        webServer.begin();
      } else {
        dnsServer.stop();
        webServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.disconnect();
        ntpSynced = false;
      }
    }
    if (key == '4') {
      menu = M_SET_THRESH;
    }
    if (key == '#') {
      settingsUnlocked = false;
      menu = M_MAIN;
    }
    return;
  }

  if (menu == M_SET_TAR_MENU) {
    if (key >= '1' && key <= '3') {
      tarSelectIdx = key - '1';
      menu = M_SET_TAR_VAL;
      inputBuf = "";
    }
    if (key == '2') tarSelectIdx = min(tarSelectIdx + 1, 2);
    if (key == '8') tarSelectIdx = max(tarSelectIdx - 1, 0);
    if (key == '#') menu = M_SETTINGS;
    return;
  }

  if (menu == M_SET_TAR_VAL) {
    if (handleNumeric(key) && inputBuf.length() > 0) {
      float v = inputBuf.toFloat();
      if (v > 0.0f) {
        if      (tarSelectIdx == 0) tarCold = v;
        else if (tarSelectIdx == 1) tarWarm = v;
        else                        tarHot  = v;
        saveData();
        Serial.printf("[TAR] %s = %.3f UAH/L\n",
          tarSelectIdx==0 ? "Cold" : tarSelectIdx==1 ? "Warm" : "Hot", v);
      }
      inputBuf = "";
      menu = M_SET_TAR_MENU;
    }
    if (key == 'C') { inputBuf = ""; menu = M_SET_TAR_MENU; }
    return;
  }

  if (menu == M_WIFI_SCAN) {
    if (foundNetworkCount == 0) return;
    if (key == '2') {
      if (selectedNetwork < foundNetworkCount - 1) {
        selectedNetwork++;
        if (selectedNetwork >= wifiListScroll + 4)
          wifiListScroll = selectedNetwork - 3;
      }
    }
    if (key == '8') {
      if (selectedNetwork > 0) {
        selectedNetwork--;
        if (selectedNetwork < wifiListScroll)
          wifiListScroll = selectedNetwork;
      }
    }
    if (key == '#' || key == 'A') {
      menu = M_WIFI_PASS;
      inputBuf = "";
    }
    if (key == 'C') menu = M_SETTINGS;
    return;
  }

  if (menu == M_WIFI_PASS) {
    if (key >= '0' && key <= '9') {
      if (inputBuf.length() < 32) inputBuf += key;
      return;
    }
    if (key == '*') { if (inputBuf.length() < 32) inputBuf += '.'; return; }
    if (key == 'D') {
      if (inputBuf.length() > 0) inputBuf.remove(inputBuf.length() - 1);
      return;
    }
    if (key == 'C') { inputBuf = ""; menu = M_WIFI_SCAN; return; }

    if (key == '#') {
      String selSSID = foundNetworks[selectedNetwork];
      String selPass = inputBuf;

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(0, 20, "Connecting...");
      u8g2.drawStr(0, 32, selSSID.c_str());
      u8g2.sendBuffer();

      WiFi.begin(selSSID.c_str(), selPass.c_str());
      int att = 0;
      while (WiFi.status() != WL_CONNECTED && att < 20) {
        delay(500); att++;
      }

      if (WiFi.status() == WL_CONNECTED) {
        strncpy(homeSSID, selSSID.c_str(), 32);
        strncpy(homePass, selPass.c_str(), 64);
        saveData();
        syncNTP();

        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(10, 25, "Connected!");
        u8g2.drawStr(0,  37, selSSID.c_str());
        if (ntpSynced) u8g2.drawStr(0, 49, "NTP: OK");
        u8g2.sendBuffer();
        delay(1500);

        inputBuf = "";
        menu = M_WIFI_DEFAULT;
      } else {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(25, 25, "Failed!");
        u8g2.drawStr(0,  37, "Wrong password?");
        u8g2.sendBuffer();
        delay(1500);
        inputBuf = "";
        menu = M_WIFI_SCAN;
      }
    }
    return;
  }

  if (menu == M_WIFI_DEFAULT) {
    if (key == 'A') {
      strncpy(defaultSSID, homeSSID, 32);
      strncpy(defaultPass, homePass, 64);
      saveData();
      Serial.printf("[WiFi] Default: %s\n", defaultSSID);

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(5,  30, "Saved as default!");
      u8g2.drawStr(0,  45, "Auto-connect on boot");
      u8g2.sendBuffer();
      delay(1500);
    }
    menu = M_SETTINGS;
    return;
  }

  if (menu == M_SET_THRESH) {
    if (key == '1') {
      menu = M_SET_THRESH_MIN;
      inputBuf = "";
    }
    if (key == '2') {
      menu = M_SET_THRESH_MAX;
      inputBuf = "";
    }
    if (key == '3') {
      tempThreshEnabled = !tempThreshEnabled;
      saveData();
      Serial.printf("[THRESH] %s, range: %.1f-%.1fC\n",
                    tempThreshEnabled ? "ENABLED" : "DISABLED",
                    tempThreshMin, tempThreshMax);


      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(10, 25, tempThreshEnabled ?
                   "Threshold ENABLED" : "Threshold DISABLED");
      char buf[32];
      if (tempThreshEnabled) {
        snprintf(buf, sizeof(buf), "%.1fC  to  %.1fC",
                 tempThreshMin, tempThreshMax);
        u8g2.drawStr(15, 40, buf);
      }
      u8g2.sendBuffer();
      delay(1200);
    }
    if (key == '#') menu = M_SETTINGS;
    return;
  }


  if (menu == M_SET_THRESH_MIN) {
    if (handleNumeric(key) && inputBuf.length() > 0) {
      float v = inputBuf.toFloat();
      if (v >= 0.0f && v < tempThreshMax) {
        tempThreshMin = v;
        saveData();
        Serial.printf("[THRESH] Min = %.1fC\n", tempThreshMin);
      } else {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(5, 25, "Error: min >= max!");
        char buf[32];
        snprintf(buf, sizeof(buf), "Max is %.1fC", tempThreshMax);
        u8g2.drawStr(15, 40, buf);
        u8g2.sendBuffer();
        delay(1200);
        inputBuf = "";
        return;
      }
      inputBuf = "";
      menu = M_SET_THRESH;
    }
    if (key == 'C') { inputBuf = ""; menu = M_SET_THRESH; }
    return;
  }

  if (menu == M_SET_THRESH_MAX) {
    if (handleNumeric(key) && inputBuf.length() > 0) {
      float v = inputBuf.toFloat();
      if (v > tempThreshMin && v <= 100.0f) {
        tempThreshMax = v;
        saveData();
        Serial.printf("[THRESH] Max = %.1fC\n", tempThreshMax);
      } else {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawStr(5, 25, "Error: max <= min!");
        char buf[32];
        snprintf(buf, sizeof(buf), "Min is %.1fC", tempThreshMin);
        u8g2.drawStr(15, 40, buf);
        u8g2.sendBuffer();
        delay(1200);
        inputBuf = "";
        return;
      }
      inputBuf = "";
      menu = M_SET_THRESH;
    }
    if (key == 'C') { inputBuf = ""; menu = M_SET_THRESH; }
    return;
  }

  
}
void updateDisplay() {
  switch (menu) {
    case M_MAIN:          drawMainMenu();      break;
    case M_METER:         drawMeter();         break;
    case M_ANALYTICS:     drawAnalytics();     break;
    case M_SETTINGS:      drawSettings();      break;
    case M_SETTINGS_PASS: drawSettingsPass();  break;
    case M_SET_TAR_MENU:  drawTarMenu();       break;
    case M_SET_TAR_VAL:   drawTarVal();        break;
    case M_WIFI_SCAN:     drawWifiScan();      break;
    case M_WIFI_PASS:     drawWifiPass();      break;
    case M_WIFI_DEFAULT:  drawWifiDefault();   break;
    case M_WIFI:          drawWifi();          break;
    case M_RESET:         drawReset();         break;
    case M_CLOCK:         drawClock();         break;
    case M_SET_THRESH:     drawThreshMenu();  break;
    case M_SET_THRESH_MIN: drawThreshMin();   break;
    case M_SET_THRESH_MAX: drawThreshMax();   break;
  }
}

void handleWifi() {
  if (!wifiOn) return;
  WiFiClient client = webServer.available();
  if (!client) return;

  uint32_t t = millis() + 1500;
  String req = "";
  while (client.connected() && millis() < t) {
    if (client.available()) {
      req += (char)client.read();
      if (req.endsWith("\r\n\r\n")) break;
    }
  }

  if (req.indexOf("GET /reset") >= 0) {
    totalVol = 0; totalCost = 0;
    todayCold = 0; todayWarm = 0; todayHot = 0; todayCost = 0;
    dayLogCount = 0;
    saveData(); saveDayLog();
  }

  if (req.indexOf("GET /report.csv") >= 0) {
    String csv = "HTTP/1.1 200 OK\r\n";
    csv += "Content-Type: text/csv; charset=utf-8\r\n";
    csv += "Content-Disposition: attachment; filename=water_report.csv\r\n\r\n";
    csv += "Date,Cold(L),Warm(L),Hot(L),Total(L),Cost(UAH)\r\n";
    for (int i = 0; i < dayLogCount; i++) {
      if (strcmp(dayLog[i].date, todayDate) == 0) continue;
      DayRecord& r = dayLog[i];
      float tot = r.volCold + r.volWarm + r.volHot;
      char line[80];
      snprintf(line, sizeof(line), "%s,%.3f,%.3f,%.3f,%.3f,%.2f\r\n",
               r.date, r.volCold, r.volWarm, r.volHot, tot, r.cost);
      csv += line;
    }
    float todayTot = todayCold + todayWarm + todayHot;
    char line[80];
    snprintf(line, sizeof(line), "%s(today),%.3f,%.3f,%.3f,%.3f,%.2f\r\n",
             todayDate, todayCold, todayWarm, todayHot, todayTot, todayCost);
    csv += line;
    client.print(csv);
    client.stop();
    return;
  }

  bool isCaptive = (req.indexOf("/generate_204")   >= 0 ||
                    req.indexOf("/hotspot-detect")  >= 0 ||
                    req.indexOf("/ncsi.txt")        >= 0 ||
                    req.indexOf("/connecttest.txt") >= 0 ||
                    req.indexOf("/redirect")        >= 0 ||
                    req.indexOf("/canonical.html")  >= 0 ||
                    req.indexOf("/success.txt")     >= 0 ||
                    req.indexOf("/library/test")    >= 0);
  if (isCaptive) {
    client.print("HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1/\r\n\r\n");
    client.stop();
    return;
  }

  char webDate[12] = "N/A";
  char webTime[12] = "N/A";
  getCurrentDateTimeStr(webDate, webTime);

  String html = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n";
  html +=
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='5'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Water Meter</title><style>"
    "body{font-family:Arial,sans-serif;background:#0a0f1e;color:#e0f0ff;margin:0;padding:16px;}"
    "h1{color:#00d4ff;text-align:center;border-bottom:2px solid #00d4ff;padding-bottom:8px;}"
    "h2{color:#00d4ff;font-size:1em;margin:16px 0 6px;}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:10px 0;}"
    ".card{background:#0d1b2e;border:1px solid #00d4ff33;border-radius:10px;padding:14px;}"
    ".lbl{color:#7bafc0;font-size:.8em;margin-bottom:4px;}"
    ".val{font-size:1.6em;color:#00ff9f;font-weight:bold;}"
    ".sub{color:#aaa;font-size:.8em;margin-top:3px;}"
    ".warn{color:#ff6b6b;}"
    ".dt{text-align:center;color:#7bafc0;font-size:.85em;margin-bottom:10px;}"
    "table{width:100%;border-collapse:collapse;font-size:.82em;}"
    "th{color:#7bafc0;text-align:left;padding:4px 6px;border-bottom:1px solid #00d4ff44;}"
    "td{padding:4px 6px;border-bottom:1px solid #ffffff0a;}"
    ".today{color:#00ff9f;}"
    ".cold{color:#7ed6df;} .warm{color:#f9ca24;} .hot{color:#e55039;}"
    ".btn{border-radius:8px;text-decoration:none;display:inline-block;"
    "padding:10px 20px;margin:4px;font-size:.9em;}"
    ".btn-dl{background:#00d4ff22;border:1px solid #00d4ff;color:#00d4ff;}"
    ".btn-rst{background:#ff444422;border:1px solid #ff4444;color:#ff4444;}"
    ".note{color:#555;font-size:.75em;text-align:center;margin-top:8px;}"
    ".ntp-ok{color:#00ff9f;} .ntp-no{color:#ff6b6b;}"
    "</style></head><body>"
    "<h1>Water Meter</h1>";

  html += "<div class='dt'>";
  html += String(webDate) + " &nbsp; <b>" + webTime + "</b>";
  html += ntpSynced ? " &nbsp;<span class='ntp-ok'>&#10004; NTP</span>"
                    : " &nbsp;<span class='ntp-no'>&#10008; NTP</span>";
  html += "</div>";

  char buf[64];
  html += "<div class='grid'>";

  snprintf(buf, sizeof(buf), "%.2f L/min", flowLPM);
  html += String("<div class='card'><div class='lbl'>Flow rate</div>"
                 "<div class='val'>") + buf + "</div></div>";

  snprintf(buf, sizeof(buf), "%.3f L", totalVol);
  html += String("<div class='card'><div class='lbl'>Total volume</div>"
                 "<div class='val'>") + buf + "</div></div>";

  snprintf(buf, sizeof(buf), "%.2f UAH", totalCost);
  html += String("<div class='card'><div class='lbl'>Total cost</div>"
                 "<div class='val'>") + buf + "</div></div>";

  if (waterTemp <= -50.0f) {
    html += "<div class='card'><div class='lbl'>Temperature</div>"
            "<div class='val warn'>-127C</div>"
            "<div class='sub'>Sensor not connected</div></div>";
  } else {
    snprintf(buf, sizeof(buf), "%.1f C [%s]", waterTemp, getTempCat());
    html += String("<div class='card'><div class='lbl'>Temperature</div>"
                   "<div class='val' style='font-size:1.2em;'>") + buf +
            "</div><div class='sub'>" + String(getCurrentTariff(), 3) +
            " UAH/L</div></div>";
  }
  html += "</div>";

  html += "<div class='card'><h2>Tariffs</h2><div class='grid'>";
  snprintf(buf, sizeof(buf), "%.3f UAH/L", tarCold);
  html += String("<div><div class='lbl'>Cold &lt;20C</div>"
                 "<div class='cold' style='font-weight:bold;'>") + buf + "</div></div>";
  snprintf(buf, sizeof(buf), "%.3f UAH/L", tarWarm);
  html += String("<div><div class='lbl'>Warm 20-45C</div>"
                 "<div class='warm' style='font-weight:bold;'>") + buf + "</div></div>";
  snprintf(buf, sizeof(buf), "%.3f UAH/L", tarHot);
  html += String("<div><div class='lbl'>Hot &gt;45C</div>"
                 "<div class='hot' style='font-weight:bold;'>") + buf + "</div></div>";
  html += "<div><div class='lbl'>Active tariff</div>"
          "<div style='color:#00ff9f;font-weight:bold;'>" +
          String(getCurrentTariff(), 3) + " UAH/L</div></div>";
  html += "</div></div>";

  if (tempThreshEnabled) {
    html += "<div class='card'><h2>Temp threshold</h2><div class='grid'>";
    char buf2[32];
    snprintf(buf2, sizeof(buf2), "%.1f C", tempThreshMin);
    html += String("<div><div class='lbl'>Min (below = skip)</div>"
                   "<div style='color:#7ed6df;font-weight:bold;'>") + buf2 + "</div></div>";
    snprintf(buf2, sizeof(buf2), "%.1f C", tempThreshMax);
    html += String("<div><div class='lbl'>Max (above = skip)</div>"
                   "<div style='color:#e55039;font-weight:bold;'>") + buf2 + "</div></div>";
    html += "</div>";
    html += "<div style='color:#aaa;font-size:.8em;margin-top:6px;'>"
            "Water outside this range is NOT counted</div></div>";
  }
 
  html += "<div class='card'><h2>Daily log</h2>"
          "<table><tr>"
          "<th>Date</th><th>Cold</th><th>Warm</th>"
          "<th>Hot</th><th>Total</th><th>UAH</th></tr>";

  float todayTot = todayCold + todayWarm + todayHot;
  char row[180];
  snprintf(row, sizeof(row),
    "<tr class='today'><td>%s &#9733;</td>"
    "<td class='cold'>%.3f</td><td class='warm'>%.3f</td>"
    "<td class='hot'>%.3f</td><td>%.3f</td><td>%.2f</td></tr>",
    todayDate, todayCold, todayWarm, todayHot, todayTot, todayCost);
  html += row;

  for (int i = dayLogCount - 1; i >= 0; i--) {
    if (strcmp(dayLog[i].date, todayDate) == 0) continue;
    DayRecord& r = dayLog[i];
    float tot = r.volCold + r.volWarm + r.volHot;
    snprintf(row, sizeof(row),
      "<tr><td>%s</td>"
      "<td class='cold'>%.3f</td><td class='warm'>%.3f</td>"
      "<td class='hot'>%.3f</td><td>%.3f</td><td>%.2f</td></tr>",
      r.date, r.volCold, r.volWarm, r.volHot, tot, r.cost);
    html += row;
  }
  html += "</table></div>";

  html += "<div style='text-align:center;margin-top:12px;'>"
          "<a href='/report.csv' class='btn btn-dl'>&#11015; Download CSV</a>"
          "<a href='/reset' class='btn btn-rst'>&#9888; Reset counter</a>"
          "</div>"
          "<div class='note'>Auto-refresh every 5s</div>"
          "</body></html>";

  client.print(html);
  client.stop();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] Water Meter v5.0");

  Wire.begin(21, 22);
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(15, 20, "WATER METER v5.0");
  u8g2.drawStr(20, 35, "Starting...");
  u8g2.sendBuffer();

  loadData();
  loadDayLog();


  tempSensor.begin();
  tempSensor.setResolution(12);
  tempSensor.requestTemperatures();
  delay(800);
  float t = tempSensor.getTempCByIndex(0);
  waterTemp = (t == DEVICE_DISCONNECTED_C || t <= -50.0f) ? -127.0f : t;
  Serial.printf("[DS18B20] %.2f C\n", waterTemp);


  pinMode(FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, FALLING);

  
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

  if (ntpSynced) {
    char realDate[12];
    getCurrentDateStr(realDate);
    if (strcmp(todayDate, "00.00.0000") != 0 &&
        strcmp(todayDate, realDate) != 0) {
      Serial.printf("[BOOT] Date changed: %s -> %s\n", todayDate, realDate);
      commitTodayToLog();
      strncpy(todayDate, realDate, 12);
      todayCold = 0.0f; todayWarm = 0.0f;
      todayHot  = 0.0f; todayCost = 0.0f;
      saveData();
    } else {
      strncpy(todayDate, realDate, 12);
    }
  } else if (strcmp(todayDate, "00.00.0000") == 0) {
    snprintf(todayDate, 12, "%02d.%02d.%04d",
             fakeDayBackup, fakeMonthBackup, fakeYearBackup);
  }

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  webServer.begin();


  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(10, 12, "WATER METER v5.0");
  u8g2.drawHLine(0, 14, 128);
  u8g2.drawStr(0, 26, "AP:  WaterMeter");

  char timeBuf[12], dateBuf[12];
  if (getCurrentDateTimeStr(dateBuf, timeBuf)) {
    u8g2.drawStr(0, 36, dateBuf);
    u8g2.drawStr(70, 36, timeBuf);
    u8g2.drawStr(0, 46, "NTP: OK");
  } else {
    u8g2.drawStr(0, 36, todayDate);
    u8g2.drawStr(0, 46, "NTP: no sync");
  }

  if (WiFi.status() == WL_CONNECTED) {
    char buf2[32];
    snprintf(buf2, sizeof(buf2), "Home: %s", WiFi.SSID().c_str());
    u8g2.drawStr(0, 56, buf2);
  } else {
    u8g2.drawStr(0, 56, "Set WiFi in Settings");
  }
  u8g2.sendBuffer();
  delay(3000);

  menu = M_MAIN;
  Serial.println("[READY]");
}

void loop() {
  uint32_t now = millis();

  if (now - tFlow >= I_FLOW) { tFlow = now; calcFlow(); }

  if (now - lastTempRead >= TEMP_INTERVAL) {
    lastTempRead = now;
    float t = tempSensor.getTempCByIndex(0);
    waterTemp = (t == DEVICE_DISCONNECTED_C || t <= -50.0f) ? -127.0f : t;
    tempSensor.requestTemperatures();
  }

  if (now - tDisp >= I_DISP) { tDisp = now; updateDisplay(); }

  if (now - tSave >= I_SAVE) {
    tSave = now;
    saveData();
    commitTodayToLog();
    Serial.printf("[NVS] %.3fL / %.2fUAH / %s\n",
                  totalVol, totalCost, todayDate);
  }

  if (now - tWifi >= I_WIFI) {
    tWifi = now;
    dnsServer.processNextRequest();
    handleWifi();
  }

  if (!ntpSynced && WiFi.status() == WL_CONNECTED) {
    if (now - lastNtpCheck >= NTP_RETRY) {
      lastNtpCheck = now;
      syncNTP();
    }
  }

  static uint32_t lastReconnect = 0;
  if (WiFi.status() != WL_CONNECTED && strlen(homeSSID) > 0
      && now - lastReconnect >= 60000) {
    lastReconnect = now;
    Serial.println("[WiFi] Reconnecting...");
    WiFi.begin(homeSSID, homePass);
  }

  checkDayChange();

  char key = keypad.getKey();
  if (key) {
    Serial.printf("[KEY] %c\n", key);
    handleKey(key);
  }
}