#pragma once
#include "config.h"
#include <Arduino.h>

// ============================================================
//  globals.h — all shared variables declared extern
// ============================================================

// --- Flow & volume ---
extern volatile uint32_t pulseCount;
extern float flowLPM;
extern float totalVol;
extern float totalCost;

// --- Temperature ---
extern float waterTemp;

// --- Tariffs ---
extern float tarCold;
extern float tarWarm;
extern float tarHot;

// --- Temp threshold ---
extern float tempThreshMin;
extern float tempThreshMax;
extern bool  tempThreshEnabled;

// --- Daily analytics ---
extern DayRecord dayLog[MAX_DAYS];
extern int       dayLogCount;
extern char      todayDate[12];
extern float     todayCold;
extern float     todayWarm;
extern float     todayHot;
extern float     todayCost;

// --- WiFi / NTP ---
extern bool wifiOn;
extern bool ntpSynced;
extern bool rtcOk;

// --- Settings ---
extern char settingsPass[8];
extern char homeSSID[32];
extern char homePass[64];
extern char defaultSSID[32];
extern char defaultPass[64];
extern bool settingsUnlocked;

// --- WiFi scan ---
extern String foundNetworks[MAX_NETWORKS];
extern int    foundNetworkCount;
extern int    selectedNetwork;
extern int    wifiListScroll;
