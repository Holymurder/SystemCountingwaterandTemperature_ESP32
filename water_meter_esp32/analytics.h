#pragma once

void commitTodayToLog();
void checkDayChange();
void resetAllData();

// Tariff / temperature helpers
float       getCurrentTariff();
const char* getTempCat();
bool        isTempValid();
