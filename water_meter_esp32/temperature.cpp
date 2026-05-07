#include "temperature.h"
#include "globals.h"
#include "config.h"
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================
//  temperature.cpp — DS18B20 temperature sensor
// ============================================================

static OneWire         oneWire(TEMP_PIN);
static DallasTemperature tempSensor(&oneWire);

void tempSetup() {
  tempSensor.begin();
  tempSensor.setResolution(12);
  tempSensor.requestTemperatures();
  delay(800);
  float t = tempSensor.getTempCByIndex(0);
  waterTemp = (t == DEVICE_DISCONNECTED_C || t <= -50.0f) ? -127.0f : t;
  Serial.printf("[DS18B20] %.2f C\n", waterTemp);
}

void tempUpdate() {
  float t = tempSensor.getTempCByIndex(0);
  waterTemp = (t == DEVICE_DISCONNECTED_C || t <= -50.0f) ? -127.0f : t;
  tempSensor.requestTemperatures();
}
