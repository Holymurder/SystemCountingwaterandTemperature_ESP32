#pragma once
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;

// Menu states
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

extern MenuState menu;
extern int       menuIdx;
extern String    inputBuf;
extern int       analyticsPage;
extern int       tarSelectIdx;
extern char      passInputBuf[8];
extern int       passInputLen;

void updateDisplay();

// Shared draw helpers
void drawTitle(const char* t);
void drawFooter(const char* t);
