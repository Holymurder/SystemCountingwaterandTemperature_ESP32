#include "input.h"
#include "display.h"
#include "globals.h"
#include "analytics.h"
#include "storage.h"
#include "clock.h"
#include <WiFi.h>
#include <DNSServer.h>

// ============================================================
//  input.cpp — keypad matrix + menu navigation logic
// ============================================================

extern DNSServer dnsServer;
extern WiFiServer webServer;

byte rowPins[KP_ROWS] = {13, 12, 14, 27};
byte colPins[KP_COLS] = {26, 25, 33, 32};
char keys[KP_ROWS][KP_COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, KP_ROWS, KP_COLS);

// --- Helpers ---

// Returns true on '#' (confirm). Handles digits, dot(*), backspace(D), cancel(C).
bool handleNumericInput(char key, MenuState cancelTarget) {
  if (key >= '0' && key <= '9') {
    if (inputBuf.length() < 8) inputBuf += key;
    return false;
  }
  if (key == '*') {
    if (inputBuf.indexOf('.') < 0) inputBuf += '.';
    return false;
  }
  if (key == 'D') {
    if (inputBuf.length() > 0) inputBuf.remove(inputBuf.length() - 1);
    return false;
  }
  if (key == 'C') {
    inputBuf = "";
    menu = cancelTarget;
    return false;
  }
  return (key == '#');
}

static void showMsg(const char* line1, const char* line2 = nullptr, int ms = 1200) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(5, 25, line1);
  if (line2) u8g2.drawStr(5, 40, line2);
  u8g2.sendBuffer();
  delay(ms);
}

// --- Main key handler ---

void handleKey(char key) {

  // ---- MAIN MENU ----
  if (menu == M_MAIN) {
    if (key == '8' || key == 'A') menuIdx = max(menuIdx - 1, 0);
    if (key == '2' || key == 'B') menuIdx = min(menuIdx + 1, 5);
    if (key >= '1' && key <= '6') menuIdx = key - '1';

    if (key == '#') {
      switch (menuIdx) {
        case 0: menu = M_METER; break;
        case 1: menu = M_ANALYTICS; analyticsPage = 0; break;
        case 2:
          if (settingsUnlocked) {
            menu = M_SETTINGS;
          } else {
            menu = M_SETTINGS_PASS;
            passInputLen = 0;
            memset(passInputBuf, 0, sizeof(passInputBuf));
          }
          break;
        case 3: menu = M_WIFI;      break;
        case 4: menu = M_CLOCK;     break;
        case 5: menu = M_RESET;     break;
      }
      menuIdx = 0;
    }
    return;
  }

  // ---- PASSWORD SCREEN ----
  if (menu == M_SETTINGS_PASS) {
    if (key >= '0' && key <= '9') {
      if (passInputLen < 7) passInputBuf[passInputLen++] = key;
      return;
    }
    if (key == 'D') {
      if (passInputLen > 0) passInputBuf[--passInputLen] = '\0';
      return;
    }
    if (key == '#') {
      if (strcmp(passInputBuf, settingsPass) == 0) {
        settingsUnlocked = true;
        menu = M_SETTINGS;
      } else {
        showMsg("Wrong password!", nullptr, 1200);
        passInputLen = 0;
        memset(passInputBuf, 0, sizeof(passInputBuf));
      }
      return;
    }
    if (key == 'C') {
      menu = M_MAIN;
      passInputLen = 0;
      memset(passInputBuf, 0, sizeof(passInputBuf));
    }
    return;
  }

  // ---- SIMPLE BACK SCREENS ----
  if (menu == M_METER || menu == M_CLOCK || menu == M_WIFI) {
    if (key == '#') menu = M_MAIN;
    return;
  }

  // ---- ANALYTICS ----
  if (menu == M_ANALYTICS) {
    if (key == '#') { menu = M_MAIN; analyticsPage = 0; return; }

    int total = 1;
    for (int i = 0; i < dayLogCount; i++)
      if (strcmp(dayLog[i].date, todayDate) != 0) total++;

    if ((key == '2' || key == 'B') && analyticsPage < total - 1) analyticsPage++;
    if ((key == '8' || key == 'A') && analyticsPage > 0)         analyticsPage--;
    return;
  }

  // ---- RESET ----
  if (menu == M_RESET) {
    if (key == '*') { resetAllData(); menu = M_MAIN; }  // '*' = confirm reset
    if (key == '#') menu = M_MAIN;
    return;
  }

  // ---- SETTINGS ----
  if (menu == M_SETTINGS) {
    if (key == '1') { menu = M_SET_TAR_MENU; tarSelectIdx = 0; }
    if (key == '2') {
      menu = M_WIFI_SCAN;
      foundNetworkCount = 0; selectedNetwork = 0; wifiListScroll = 0;
      updateDisplay();
      Serial.println("[WiFi] Scanning...");
      int n = WiFi.scanNetworks();
      foundNetworkCount = min(n, MAX_NETWORKS);
      for (int i = 0; i < foundNetworkCount; i++)
        foundNetworks[i] = WiFi.SSID(i);
      Serial.printf("[WiFi] Found %d\n", foundNetworkCount);
    }
    if (key == '3') {
      wifiOn = !wifiOn;
      if (wifiOn) {
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(AP_SSID, AP_PASS);
        delay(500);
        if (strlen(homeSSID) > 0) {
          WiFi.begin(homeSSID, homePass);
          int att = 0;
          while (WiFi.status() != WL_CONNECTED && att < 20) { delay(500); att++; }
          if (WiFi.status() == WL_CONNECTED) syncNTP();
        }
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        webServer.begin();
      } else {
        dnsServer.stop();
        webServer.stop();
        WiFi.softAPdisconnect(true);
        WiFi.disconnect();
        ntpSynced = false;
      }
    }
    if (key == '4') { menu = M_SET_THRESH; }
    if (key == '#') { settingsUnlocked = false; menu = M_MAIN; }
    return;
  }

  // ---- TARIFF MENU ----
  if (menu == M_SET_TAR_MENU) {
    if (key >= '1' && key <= '3') {
      tarSelectIdx = key - '1';
      menu = M_SET_TAR_VAL;
      inputBuf = "";
    }
    if (key == 'A') tarSelectIdx = max(tarSelectIdx - 1, 0);
    if (key == 'B') tarSelectIdx = min(tarSelectIdx + 1, 2);
    if (key == '#') menu = M_SETTINGS;
    return;
  }

  // ---- TARIFF VALUE ----
  if (menu == M_SET_TAR_VAL) {
    if (handleNumericInput(key, M_SET_TAR_MENU) && inputBuf.length() > 0) {
      float v = inputBuf.toFloat();
      if (v > 0.0f) {
        if      (tarSelectIdx == 0) tarCold = v;
        else if (tarSelectIdx == 1) tarWarm = v;
        else                        tarHot  = v;
        saveData();
        Serial.printf("[TAR] %s = %.3f UAH/L\n",
          tarSelectIdx==0 ? "Cold" : tarSelectIdx==1 ? "Warm" : "Hot", v);
      }
      inputBuf = "";
      menu = M_SET_TAR_MENU;
    }
    return;
  }

  // ---- WIFI SCAN ----
  if (menu == M_WIFI_SCAN) {
    if (foundNetworkCount == 0) return;
    if ((key == '2' || key == 'B') && selectedNetwork < foundNetworkCount - 1) {
      selectedNetwork++;
      if (selectedNetwork >= wifiListScroll + 4) wifiListScroll = selectedNetwork - 3;
    }
    if ((key == '8' || key == 'A') && selectedNetwork > 0) {
      selectedNetwork--;
      if (selectedNetwork < wifiListScroll) wifiListScroll = selectedNetwork;
    }
    if (key == '#') { menu = M_WIFI_PASS; inputBuf = ""; }
    if (key == 'C') menu = M_SETTINGS;
    return;
  }

  // ---- WIFI PASSWORD ----
  if (menu == M_WIFI_PASS) {
    if (key >= '0' && key <= '9') { if (inputBuf.length() < 32) inputBuf += key; return; }
    if (key == '*')  { if (inputBuf.length() < 32) inputBuf += '.'; return; }
    if (key == 'D')  { if (inputBuf.length() > 0) inputBuf.remove(inputBuf.length()-1); return; }
    if (key == 'C')  { inputBuf = ""; menu = M_WIFI_SCAN; return; }

    if (key == '#') {
      String selSSID = foundNetworks[selectedNetwork];
      String selPass = inputBuf;

      showMsg("Connecting...", selSSID.c_str(), 0);
      WiFi.begin(selSSID.c_str(), selPass.c_str());
      int att = 0;
      while (WiFi.status() != WL_CONNECTED && att < 20) { delay(500); att++; }

      if (WiFi.status() == WL_CONNECTED) {
        strncpy(homeSSID, selSSID.c_str(), 32);
        strncpy(homePass, selPass.c_str(), 64);
        saveData();
        syncNTP();
        showMsg("Connected!", ntpSynced ? "NTP: OK" : nullptr, 1500);
        inputBuf = "";
        menu = M_WIFI_DEFAULT;
      } else {
        showMsg("Failed!", "Wrong password?", 1500);
        inputBuf = "";
        menu = M_WIFI_SCAN;
      }
    }
    return;
  }

  // ---- WIFI SET DEFAULT ----
  if (menu == M_WIFI_DEFAULT) {
    if (key == 'A') {
      strncpy(defaultSSID, homeSSID, 32);
      strncpy(defaultPass, homePass, 64);
      saveData();
      showMsg("Saved as default!", "Auto-connect on boot", 1500);
    }
    menu = M_SETTINGS;
    return;
  }

  // ---- TEMP THRESHOLD MENU ----
  if (menu == M_SET_THRESH) {
    if (key == '1') { menu = M_SET_THRESH_MIN; inputBuf = ""; }
    if (key == '2') { menu = M_SET_THRESH_MAX; inputBuf = ""; }
    if (key == '3') {
      tempThreshEnabled = !tempThreshEnabled;
      saveData();
      Serial.printf("[THRESH] %s, range: %.1f-%.1fC\n",
                    tempThreshEnabled ? "ENABLED" : "DISABLED",
                    tempThreshMin, tempThreshMax);
      char buf[24];
      if (tempThreshEnabled) snprintf(buf, sizeof(buf), "%.1f to %.1fC", tempThreshMin, tempThreshMax);
      showMsg(tempThreshEnabled ? "Threshold ENABLED" : "Threshold DISABLED",
              tempThreshEnabled ? buf : nullptr, 1200);
    }
    if (key == '#') menu = M_SETTINGS;
    return;
  }

  // ---- THRESH MIN ----
  if (menu == M_SET_THRESH_MIN) {
    if (handleNumericInput(key, M_SET_THRESH) && inputBuf.length() > 0) {
      float v = inputBuf.toFloat();
      if (v >= 0.0f && v < tempThreshMax) {
        tempThreshMin = v;
        saveData();
        Serial.printf("[THRESH] Min = %.1fC\n", tempThreshMin);
      } else {
        char buf[24];
        snprintf(buf, sizeof(buf), "Max is %.1fC", tempThreshMax);
        showMsg("Error: min >= max!", buf, 1200);
        inputBuf = "";
        return;
      }
      inputBuf = "";
      menu = M_SET_THRESH;
    }
    return;
  }

  // ---- THRESH MAX ----
  if (menu == M_SET_THRESH_MAX) {
    if (handleNumericInput(key, M_SET_THRESH) && inputBuf.length() > 0) {
      float v = inputBuf.toFloat();
      if (v > tempThreshMin && v <= 100.0f) {
        tempThreshMax = v;
        saveData();
        Serial.printf("[THRESH] Max = %.1fC\n", tempThreshMax);
      } else {
        char buf[24];
        snprintf(buf, sizeof(buf), "Min is %.1fC", tempThreshMin);
        showMsg("Error: max <= min!", buf, 1200);
        inputBuf = "";
        return;
      }
      inputBuf = "";
      menu = M_SET_THRESH;
    }
    return;
  }
}