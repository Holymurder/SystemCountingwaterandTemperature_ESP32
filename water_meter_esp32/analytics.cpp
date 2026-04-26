#include "analytics.h"
#include "globals.h"
#include "clock.h"
#include "storage.h"

// ============================================================
//  analytics.cpp — daily log, tariff, temp helpers
// ============================================================

// --- Tariff / temperature helpers ---

float getCurrentTariff() {
  if (waterTemp <= -50.0f) return tarCold;
  if (waterTemp <  20.0f)  return tarCold;
  if (waterTemp <= 45.0f)  return tarWarm;
  return tarHot;
}

const char* getTempCat() {
  if (waterTemp <= -50.0f) return "N/A ";
  if (waterTemp <  20.0f)  return "COLD";
  if (waterTemp <= 45.0f)  return "WARM";
  return "HOT ";
}

bool isTempValid() {
  if (!tempThreshEnabled)  return true;
  if (waterTemp <= -50.0f) return true;
  return (waterTemp >= tempThreshMin && waterTemp <= tempThreshMax);
}

// --- Day log helpers ---

static void purgeOldest() {
  for (int i = 0; i < dayLogCount - 1; i++) dayLog[i] = dayLog[i + 1];
  dayLogCount--;
}

static int findDayRecord(const char* date) {
  for (int i = 0; i < dayLogCount; i++)
    if (strcmp(dayLog[i].date, date) == 0) return i;
  return -1;
}

void commitTodayToLog() {
  int idx = findDayRecord(todayDate);
  if (idx < 0) {
    if (dayLogCount >= MAX_DAYS) purgeOldest();
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
  if (!ntpSynced && !rtcOk) return;
  char currentDate[12];
  if (!getCurrentDateStr(currentDate)) return;
  if (strcmp(currentDate, todayDate) == 0) return;

  Serial.printf("[DAY] %s -> %s\n", todayDate, currentDate);
  commitTodayToLog();
  strncpy(todayDate, currentDate, 12);
  todayCold = todayWarm = todayHot = todayCost = 0.0f;
  saveData();
}

void resetAllData() {
  totalVol = totalCost = 0.0f;
  todayCold = todayWarm = todayHot = todayCost = 0.0f;
  dayLogCount = 0;
  saveData();
  saveDayLog();
}
