#pragma once
#include <time.h>
#include <RTClib.h>

extern RTC_DS3231 rtc;

void     syncNTP();
bool     getRealTime(struct tm* info);
bool     getCurrentDateStr(char* out);
bool     getCurrentDateTimeStr(char* dateOut, char* timeOut);
