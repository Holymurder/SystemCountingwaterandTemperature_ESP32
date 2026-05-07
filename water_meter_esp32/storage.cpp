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
  // Erase any leftover keys beyond current count (prevents stale phantom records)
  for (int i = dayLogCount; i < MAX_DAYS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "d%d", i);
    if (prefs.isKey(key)) prefs.remove(key);
    else break;
  }
  prefs.end();
}

// ============================================================
//  purgePhantomRecords — keep only records within [minMon/minYear .. maxMon/maxYear]
//  Example: purgePhantomRecords(3, 2026, 4, 2026) keeps March–April 2026
// ============================================================

int purgePhantomRecords(int minMon, int minYear, int maxMon, int maxYear) {
  int removed = 0;
  int write = 0;
  for (int i = 0; i < dayLogCount; i++) {
    const char* d = dayLog[i].date; // "DD.MM.YYYY"
    int mon  = (d[3]-'0')*10 + (d[4]-'0');
    int year = (d[6]-'0')*1000 + (d[7]-'0')*100 + (d[8]-'0')*10 + (d[9]-'0');

    // Convert to comparable integer: YYYYMM
    int val    = year * 100 + mon;
    int valMin = minYear * 100 + minMon;
    int valMax = maxYear * 100 + maxMon;

    if (val >= valMin && val <= valMax) {
      if (write != i) dayLog[write] = dayLog[i];
      write++;
    } else {
      Serial.printf("[PURGE] Removed: %s (out of range)\n", d);
      removed++;
    }
  }
  dayLogCount = write;
  if (removed > 0) saveDayLog();
  Serial.printf("[PURGE] Done: removed %d, kept %d records\n", removed, write);
  return removed;
}

// Validate date string format DD.MM.YYYY
static bool isValidDateStr(const char* d) {
  if (strlen(d) != 10) return false;
  // Check digit positions: 0,1,3,4,6,7,8,9 must be digits
  for (int p : {0,1,3,4,6,7,8,9}) {
    if (d[p] < '0' || d[p] > '9') return false;
  }
  if (d[2] != '.' || d[5] != '.') return false;
  int day  = (d[0]-'0')*10 + (d[1]-'0');
  int mon  = (d[3]-'0')*10 + (d[4]-'0');
  int year = (d[6]-'0')*1000 + (d[7]-'0')*100 + (d[8]-'0')*10 + (d[9]-'0');
  return (day >= 1 && day <= 31) &&
         (mon >= 1 && mon <= 12) &&
         (year >= 2020 && year <= 2099);
}

void loadDayLog() {
  prefs.begin("wlog", true);
  int rawCount = prefs.getInt("cnt", 0);
  if (rawCount < 0 || rawCount > MAX_DAYS) rawCount = 0;
  dayLogCount = 0;
  for (int i = 0; i < rawCount; i++) {
    char key[8];
    snprintf(key, sizeof(key), "d%d", i);
    DayRecord tmp;
    memset(&tmp, 0, sizeof(tmp));
    size_t got = prefs.getBytes(key, &tmp, sizeof(DayRecord));
    // Skip corrupt records: wrong size or invalid date string
    if (got != sizeof(DayRecord)) continue;
    tmp.date[11] = '\0'; // ensure null-terminated
    if (!isValidDateStr(tmp.date)) {
      Serial.printf("[LOG] Skipping corrupt record d%d: '%s'\n", i, tmp.date);
      continue;
    }
    dayLog[dayLogCount++] = tmp;
  }
  prefs.end();
  Serial.printf("[LOG] Loaded %d valid records\n", dayLogCount);
}
