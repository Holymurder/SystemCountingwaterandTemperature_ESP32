#include "flow.h"
#include "globals.h"
#include "config.h"
#include "analytics.h"
#include <Arduino.h>

// ============================================================
//  flow.cpp — YF-S201 pulse counting & flow calculation
// ============================================================

void IRAM_ATTR onPulse() { pulseCount++; }

void flowSetup() {
  pinMode(FLOW_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), onPulse, FALLING);
}

void calcFlow() {
  noInterrupts();
  uint32_t p = pulseCount;
  pulseCount  = 0;
  interrupts();

  float vol  = p / PULSES_PER_LITER;
  flowLPM    = vol * (60000.0f / I_FLOW);

  if (vol <= 0.0f) return;

  if (!isTempValid()) {
    Serial.printf("[THRESH] Temp %.1fC out of [%.1f-%.1f] — skipped\n",
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
