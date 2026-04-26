#include "clock.h"
#include "globals.h"
#include "config.h"
#include <WiFi.h>

// ============================================================
//  clock.cpp — NTP sync + RTC DS3231 helpers
// ============================================================

RTC_DS3231 rtc;

bool getRealTime(struct tm* info) {
  if (rtcOk) {
    DateTime now = rtc.now();
    info->tm_mday  = now.day();
    info->tm_mon   = now.month() - 1;
    info->tm_year  = now.year() - 1900;
    info->tm_hour  = now.hour();
    info->tm_min   = now.minute();
    info->tm_sec   = now.second();
    info->tm_isdst = -1;
    return true;
  }
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

  if (!getLocalTime(&info)) {
    Serial.println("[NTP] Failed!");
    return;
  }

  ntpSynced = true;

  if (rtcOk) {
    rtc.adjust(DateTime(
      info.tm_year + 1900, info.tm_mon + 1, info.tm_mday,
      info.tm_hour, info.tm_min, info.tm_sec
    ));
    Serial.println("[RTC] Updated from NTP");
  }

  char dateStr[12], timeStr[12];
  snprintf(dateStr, 12, "%02d.%02d.%04d",
           info.tm_mday, info.tm_mon + 1, info.tm_year + 1900);
  snprintf(timeStr, 12, "%02d:%02d:%02d",
           info.tm_hour, info.tm_min, info.tm_sec);
  Serial.printf("[NTP] OK! %s %s\n", dateStr, timeStr);

  if (strcmp(todayDate, "00.00.0000") == 0)
    strncpy(todayDate, dateStr, 12);
}
