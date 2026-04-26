#pragma once
#include <WiFi.h>

extern WiFiServer webServer;

void handleWifi();
bool connectHomeWifi();
