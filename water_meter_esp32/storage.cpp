#include "storage.h"
#include "globals.h"
#include <Preferences.h>

// ============================================================
//  storage.cpp — NVS read/write via Preferences
// ============================================================

static Preferences prefs;

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
  prefs.putString("hssid", homeSSID);
  prefs.putString("hpass", homePass);
  prefs.putString("dssid", defaultSSID);
  prefs.putString("dpass", defaultPass);
  prefs.putString("spass", settingsPass);
  prefs.putFloat("tmin",  tempThreshMin);
  prefs.putFloat("tmax",  tempThreshMax);
  prefs.putBool("ten",    tempThreshEnabled);
  prefs.end();
}

void loadData() {
  prefs.begin("wm", true);
  totalVol          = prefs.getFloat("vol",   0.0f);
  totalCost         = prefs.getFloat("cost",  0.0f);
  tarCold           = prefs.getFloat("tc",    0.08f);
  tarWarm           = prefs.getFloat("tw",    0.15f);
  tarHot            = prefs.getFloat("th",    0.35f);
  todayCold         = prefs.getFloat("tdc",   0.0f);
  todayWarm         = prefs.getFloat("tdw",   0.0f);
  todayHot          = prefs.getFloat("tdh",   0.0f);
  todayCost         = prefs.getFloat("tdk",   0.0f);
  tempThreshMin     = prefs.getFloat("tmin",  0.0f);
  tempThreshMax     = prefs.getFloat("tmax",  100.0f);
  tempThreshEnabled = prefs.getBool("ten",    false);

  String saved = prefs.getString("tdd", "00.00.0000");
  strncpy(todayDate, saved.c_str(), 12);

  String hs = prefs.getString("hssid", "");
  String hp = prefs.getString("hpass", "");
  String ds = prefs.getString("dssid", "");
  String dp = prefs.getString("dpass", "");
  String sp = prefs.getString("spass", "1234");
  strncpy(homeSSID,     hs.c_str(), 32);
  strncpy(homePass,     hp.c_str(), 64);
  strncpy(defaultSSID,  ds.c_str(), 32);
  strncpy(defaultPass,  dp.c_str(), 64);
  strncpy(settingsPass, sp.c_str(), 8);
  prefs.end();
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
