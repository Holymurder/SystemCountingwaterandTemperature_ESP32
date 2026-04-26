#include "display.h"
#include "globals.h"
#include "clock.h"
#include "analytics.h"
#include <Wire.h>
#include <WiFi.h>

// ============================================================
//  display.cpp — all OLED draw functions
// ============================================================

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// --- Shared state (owned here, declared extern in display.h) ---
MenuState menu         = M_MAIN;
int       menuIdx      = 0;
String    inputBuf     = "";
int       analyticsPage  = 0;
int       tarSelectIdx   = 0;
char      passInputBuf[8] = "";
int       passInputLen    = 0;

// --- Helpers ---

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

// --- Screens ---

static const char* menuItems[] = {
  "1. Meter", "2. Analytics", "3. Settings",
  "4. WiFi info", "5. Clock", "6. Reset"
};
static const int MENU_COUNT = 6;

static void drawMainMenu() {
  u8g2.clearBuffer();
  drawTitle("=== MAIN MENU ===");
  u8g2.setFont(u8g2_font_6x10_tf);

  int startIdx = (menuIdx >= 4) ? menuIdx - 3 : 0;
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

static void drawMeter() {
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
    u8g2.drawStr(0, 58, "Temp: N/A");
  else {
    snprintf(buf, sizeof(buf), "Temp: %.1fC [%s]", waterTemp, getTempCat());
    u8g2.drawStr(0, 58, buf);
  }
  u8g2.sendBuffer();
}

static void drawAnalytics() {
  u8g2.clearBuffer();
  drawTitle("=== ANALYTICS ===");
  u8g2.setFont(u8g2_font_5x7_tf);

  // Count total pages: today + past days (excluding today duplicate)
  int totalPages = 1;
  for (int i = 0; i < dayLogCount; i++)
    if (strcmp(dayLog[i].date, todayDate) != 0) totalPages++;

  analyticsPage = constrain(analyticsPage, 0, totalPages - 1);

  char buf[34];
  char right[16];

  // Helper lambda-style: draw one tariff row
  // left col: label + volume, right col: cost
  auto drawRow = [&](int y, const char* label, float vol, float costPerL) {
  snprintf(buf, sizeof(buf), "%s%.3fL", label, vol);
  u8g2.drawStr(0, y, buf);
  // right-align cost with currency
  snprintf(right, sizeof(right), "%.2fUAH", vol * costPerL);
  int rw = u8g2.getStrWidth(right);
  u8g2.drawStr(128 - rw, y, right);
};

  if (analyticsPage == 0) {
    snprintf(buf, sizeof(buf), "%s [TODAY]", todayDate);
    u8g2.drawStr(0, 22, buf);

    drawRow(31, "Cold:", todayCold, tarCold);
    drawRow(39, "Warm:", todayWarm, tarWarm);
    drawRow(47, "Hot:", todayHot,  tarHot);

    float tot = todayCold + todayWarm + todayHot;
    snprintf(buf, sizeof(buf), "Tot:%.3fL", tot);
    u8g2.drawStr(0, 55, buf);
    snprintf(right, sizeof(right), "%.2f UAH", todayCost);
    int rw = u8g2.getStrWidth(right);
    u8g2.drawStr(128 - rw, 55, right);

  } else {
    int page = 0;
    for (int i = dayLogCount - 1; i >= 0; i--) {
      if (strcmp(dayLog[i].date, todayDate) == 0) continue;
      if (++page != analyticsPage) continue;
      DayRecord& r = dayLog[i];

      u8g2.drawStr(0, 22, r.date);

      drawRow(31, "Cold:", r.volCold, tarCold);
      drawRow(39, "Warm:", r.volWarm, tarWarm);
      drawRow(47, "Hot:", r.volHot,  tarHot);

      float tot = r.volCold + r.volWarm + r.volHot;
      snprintf(buf, sizeof(buf), "Tot:%.3fL", tot);
      u8g2.drawStr(0, 55, buf);
      snprintf(right, sizeof(right), "%.2f UAH", r.cost);
      int rw = u8g2.getStrWidth(right);
      u8g2.drawStr(128 - rw, 55, right);
      break;
    }
  }

  snprintf(buf, sizeof(buf), "[A/8]prev [B/2]next %d/%d", analyticsPage + 1, totalPages);
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(0, 63, buf);
  u8g2.sendBuffer();
}

static void drawSettingsPass() {
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

static void drawSettings() {
  u8g2.clearBuffer();
  drawTitle("=== SETTINGS ===");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 22, "1. Change tariffs");
  u8g2.drawStr(0, 32, "2. WiFi for NTP");
  u8g2.drawStr(0, 42, "3. WiFi toggle");
  u8g2.drawStr(0, 52, "4. Temp threshold");

  char buf[28];
  u8g2.setFont(u8g2_font_5x7_tf);
  snprintf(buf, sizeof(buf), "   WiFi:%s Thr:%s",
           wifiOn ? "ON" : "OFF", tempThreshEnabled ? "ON" : "OFF");
  u8g2.drawStr(0, 62, buf);
  u8g2.sendBuffer();
}

static void drawTarMenu() {
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
  u8g2.drawFrame(0, 16 + tarSelectIdx * 10, 128, 10);

  drawFooter("[1-3] select  [#] back");
  u8g2.sendBuffer();
}

static void drawTarVal() {
  u8g2.clearBuffer();
  const char* names[] = {"COLD TARIFF", "WARM TARIFF", "HOT  TARIFF"};
  drawTitle(names[tarSelectIdx]);
  u8g2.setFont(u8g2_font_6x10_tf);

  float cur = (tarSelectIdx == 0) ? tarCold :
              (tarSelectIdx == 1) ? tarWarm : tarHot;
  char buf[32];
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

static void drawThreshMenu() {
  u8g2.clearBuffer();
  drawTitle("=== TEMP THRESH ===");
  u8g2.setFont(u8g2_font_6x10_tf);

  char buf[32];
  snprintf(buf, sizeof(buf), "Status: %s", tempThreshEnabled ? "ENABLED" : "DISABLED");
  u8g2.drawStr(0, 24, buf);
  snprintf(buf, sizeof(buf), "1. Min: %.1f C", tempThreshMin);
  u8g2.drawStr(0, 35, buf);
  snprintf(buf, sizeof(buf), "2. Max: %.1f C", tempThreshMax);
  u8g2.drawStr(0, 45, buf);
  u8g2.drawStr(0, 55, "3. Toggle ON/OFF");

  drawFooter("[#] Back");
  u8g2.sendBuffer();
}

static void drawThreshInput(const char* title, float current) {
  u8g2.clearBuffer();
  drawTitle(title);
  u8g2.setFont(u8g2_font_6x10_tf);

  char buf[32];
  snprintf(buf, sizeof(buf), "Current: %.1f C", current);
  u8g2.drawStr(0, 24, buf);
  u8g2.drawStr(0, 34, "New value (C):");

  String disp = "> " + inputBuf + "_";
  u8g2.drawStr(0, 46, disp.c_str());

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(0, 56, "[*] dot  [D] del");
  u8g2.drawStr(0, 63, "[#] OK   [C] cancel");
  u8g2.sendBuffer();
}

static void drawWifiScan() {
  u8g2.clearBuffer();
  drawTitle("=== WiFi SCAN ===");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (foundNetworkCount == 0) {
    u8g2.drawStr(10, 35, "Scanning...");
    u8g2.sendBuffer();
    return;
  }

  for (int i = wifiListScroll; i < foundNetworkCount && i < wifiListScroll + 4; i++) {
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
  snprintf(pg, sizeof(pg), "[A/8]up [B/2]dn [#]sel %d/%d",
           selectedNetwork + 1, foundNetworkCount);
  u8g2.drawStr(0, 63, pg);
  u8g2.sendBuffer();
}

static void drawWifiPass() {
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

static void drawWifiDefault() {
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

static void drawReset() {
  u8g2.clearBuffer();
  drawTitle("=== RESET ===");
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(10, 26, "Reset all data?");

  char buf[32];
  snprintf(buf, sizeof(buf), "Vol:  %.3f L", totalVol);
  u8g2.drawStr(10, 38, buf);
  snprintf(buf, sizeof(buf), "Cost: %.2f UAH", totalCost);
  u8g2.drawStr(10, 48, buf);
  drawFooter("[*]=YES reset   [#]=NO");
  u8g2.sendBuffer();
}

static void drawWifi() {
  u8g2.clearBuffer();
  drawTitle("=== WiFi INFO ===");
  u8g2.setFont(u8g2_font_5x7_tf);

  if (wifiOn) {
    u8g2.drawStr(0, 22, "AP: WaterMeter");
    u8g2.drawStr(0, 30, "Pass: 12345678");
    u8g2.drawStr(0, 38, "IP: 192.168.4.1");
    u8g2.drawHLine(0, 41, 128);

    char buf[32];
    if (WiFi.status() == WL_CONNECTED) {
      snprintf(buf, sizeof(buf), "Home: %s", WiFi.SSID().c_str());
      u8g2.drawStr(0, 50, buf);
      snprintf(buf, sizeof(buf), "NTP: %s  RTC: %s",
               ntpSynced ? "OK" : "no", rtcOk ? "OK" : "no");
      u8g2.drawStr(0, 58, buf);
    } else {
      snprintf(buf, sizeof(buf), "RTC: %s", rtcOk ? "DS3231 OK" : "not found");
      u8g2.drawStr(0, 50, buf);
      u8g2.drawStr(0, 58, "NTP: no WiFi");
    }
  } else {
    u8g2.drawStr(20, 35, "WiFi DISABLED");
    u8g2.drawStr(5,  48, "Enable in Settings>3");
  }
  drawFooter("[#] Back");
  u8g2.sendBuffer();
}

static void drawClock() {
  u8g2.clearBuffer();
  drawTitle("=== CLOCK ===");

  char dateStr[12] = "";
  char timeStr[12] = "";
  bool synced = getCurrentDateTimeStr(dateStr, timeStr);

  if (synced) {
    u8g2.setFont(u8g2_font_10x20_tf);
    int tw = u8g2.getStrWidth(timeStr);
    u8g2.drawStr((128 - tw) / 2, 36, timeStr);

    u8g2.setFont(u8g2_font_6x10_tf);
    int dw = u8g2.getStrWidth(dateStr);
    u8g2.drawStr((128 - dw) / 2, 50, dateStr);

    u8g2.setFont(u8g2_font_5x7_tf);
    if (rtcOk) {
      char rtcBuf[24];
      snprintf(rtcBuf, sizeof(rtcBuf), "RTC %.1fC", rtc.getTemperature());
      u8g2.drawStr(0, 64, rtcBuf);
      u8g2.drawStr(80, 64, "DS3231 OK");
    } else if (ntpSynced) {
      u8g2.drawStr(0, 64, "Source: NTP");
    } else {
      u8g2.drawStr(0, 64, "Source: MANUAL");
    }
  } else {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(10, 28, "NTP not synced!");
    u8g2.setFont(u8g2_font_5x7_tf);

    if (rtcOk) {
      u8g2.drawStr(0, 40, "DS3231: connected");
      if (rtc.lostPower()) {
        u8g2.drawStr(0, 50, "WARNING: lost power!");
        u8g2.drawStr(0, 60, "Set time via NTP");
      }
    } else {
      u8g2.drawStr(0, 40, "DS3231: NOT found");
      u8g2.drawStr(0, 50, "Set WiFi in Settings");
    }
  }
  drawFooter("[#] Back");
  u8g2.sendBuffer();
}

// --- Public dispatcher ---

void updateDisplay() {
  switch (menu) {
    case M_MAIN:           drawMainMenu();    break;
    case M_METER:          drawMeter();       break;
    case M_ANALYTICS:      drawAnalytics();   break;
    case M_SETTINGS:       drawSettings();    break;
    case M_SETTINGS_PASS:  drawSettingsPass(); break;
    case M_SET_TAR_MENU:   drawTarMenu();     break;
    case M_SET_TAR_VAL:    drawTarVal();      break;
    case M_WIFI_SCAN:      drawWifiScan();    break;
    case M_WIFI_PASS:      drawWifiPass();    break;
    case M_WIFI_DEFAULT:   drawWifiDefault(); break;
    case M_WIFI:           drawWifi();        break;
    case M_RESET:          drawReset();       break;
    case M_CLOCK:          drawClock();       break;
    case M_SET_THRESH:     drawThreshMenu();  break;
    case M_SET_THRESH_MIN: drawThreshInput("MIN TEMP THRESH", tempThreshMin); break;
    case M_SET_THRESH_MAX: drawThreshInput("MAX TEMP THRESH", tempThreshMax); break;
  }
}