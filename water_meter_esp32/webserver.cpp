#include "webserver.h"
#include "globals.h"
#include "analytics.h"
#include "clock.h"
#include "storage.h"

// ============================================================
//  webserver.cpp — captive portal + web dashboard
// ============================================================

WiFiServer webServer(80);

bool connectHomeWifi() {
  if (strlen(homeSSID) == 0) return false;
  Serial.printf("[WiFi] Connecting to %s...\n", homeSSID);
  WiFi.begin(homeSSID, homePass);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500); attempts++;
  }
  bool ok = (WiFi.status() == WL_CONNECTED);
  Serial.println(ok ? "[WiFi] Connected!" : "[WiFi] Failed!");
  return ok;
}

// --- HTML page builder ---

static String buildPage() {
  char webDate[12] = "N/A";
  char webTime[12] = "N/A";
  getCurrentDateTimeStr(webDate, webTime);

  String html =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta http-equiv='refresh' content='5'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Water Meter</title><style>"
    "body{font-family:Arial,sans-serif;background:#0a0f1e;color:#e0f0ff;margin:0;padding:16px;}"
    "h1{color:#00d4ff;text-align:center;border-bottom:2px solid #00d4ff;padding-bottom:8px;}"
    "h2{color:#00d4ff;font-size:1em;margin:16px 0 6px;}"
    ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin:10px 0;}"
    ".card{background:#0d1b2e;border:1px solid #00d4ff33;border-radius:10px;padding:14px;}"
    ".lbl{color:#7bafc0;font-size:.8em;margin-bottom:4px;}"
    ".val{font-size:1.6em;color:#00ff9f;font-weight:bold;}"
    ".sub{color:#aaa;font-size:.8em;margin-top:3px;}"
    ".warn{color:#ff6b6b;}"
    ".dt{text-align:center;color:#7bafc0;font-size:.85em;margin-bottom:10px;}"
    "table{width:100%;border-collapse:collapse;font-size:.82em;}"
    "th{color:#7bafc0;text-align:left;padding:4px 6px;border-bottom:1px solid #00d4ff44;}"
    "td{padding:4px 6px;border-bottom:1px solid #ffffff0a;}"
    ".today{color:#00ff9f;}"
    ".cold{color:#7ed6df;} .warm{color:#f9ca24;} .hot{color:#e55039;}"
    ".btn{border-radius:8px;text-decoration:none;display:inline-block;"
    "padding:10px 20px;margin:4px;font-size:.9em;}"
    ".btn-dl{background:#00d4ff22;border:1px solid #00d4ff;color:#00d4ff;}"
    ".btn-rst{background:#ff444422;border:1px solid #ff4444;color:#ff4444;}"
    ".note{color:#555;font-size:.75em;text-align:center;margin-top:8px;}"
    ".ntp-ok{color:#00ff9f;} .ntp-no{color:#ff6b6b;}"
    "</style></head><body>"
    "<h1>Water Meter</h1>";

  // Time/date header
  html += "<div class='dt'>" + String(webDate) + " &nbsp; <b>" + webTime + "</b>";
  if (rtcOk) {
    char rtcBuf[32];
    snprintf(rtcBuf, sizeof(rtcBuf), "DS3231 (%.1fC)", rtc.getTemperature());
    html += " &nbsp;<span class='ntp-ok'>&#10004; RTC " + String(rtcBuf) + "</span>";
    if (ntpSynced) html += " &nbsp;<span class='ntp-ok'>&#10004; NTP</span>";
  } else if (ntpSynced) {
    html += " &nbsp;<span class='ntp-ok'>&#10004; NTP</span>";
  } else {
    html += " &nbsp;<span class='ntp-no'>&#10008; No time source</span>";
  }
  html += "</div>";

  char buf[64];
  html += "<div class='grid'>";

  snprintf(buf, sizeof(buf), "%.2f L/min", flowLPM);
  html += String("<div class='card'><div class='lbl'>Flow rate</div><div class='val'>") + buf + "</div></div>";

  snprintf(buf, sizeof(buf), "%.3f L", totalVol);
  html += String("<div class='card'><div class='lbl'>Total volume</div><div class='val'>") + buf + "</div></div>";

  snprintf(buf, sizeof(buf), "%.2f UAH", totalCost);
  html += String("<div class='card'><div class='lbl'>Total cost</div><div class='val'>") + buf + "</div></div>";

  if (waterTemp <= -50.0f) {
    html += "<div class='card'><div class='lbl'>Temperature</div>"
            "<div class='val warn'>N/A</div>"
            "<div class='sub'>Sensor not connected</div></div>";
  } else {
    snprintf(buf, sizeof(buf), "%.1f C [%s]", waterTemp, getTempCat());
    html += String("<div class='card'><div class='lbl'>Temperature</div>"
                   "<div class='val' style='font-size:1.2em;'>") + buf +
            "</div><div class='sub'>" + String(getCurrentTariff(), 3) + " UAH/L</div></div>";
  }
  html += "</div>";

  // Tariffs
  html += "<div class='card'><h2>Tariffs</h2><div class='grid'>";
  snprintf(buf, sizeof(buf), "%.3f UAH/L", tarCold);
  html += String("<div><div class='lbl'>Cold &lt;20C</div><div class='cold' style='font-weight:bold;'>") + buf + "</div></div>";
  snprintf(buf, sizeof(buf), "%.3f UAH/L", tarWarm);
  html += String("<div><div class='lbl'>Warm 20-45C</div><div class='warm' style='font-weight:bold;'>") + buf + "</div></div>";
  snprintf(buf, sizeof(buf), "%.3f UAH/L", tarHot);
  html += String("<div><div class='lbl'>Hot &gt;45C</div><div class='hot' style='font-weight:bold;'>") + buf + "</div></div>";
  html += String("<div><div class='lbl'>Active tariff</div>"
                 "<div style='color:#00ff9f;font-weight:bold;'>") +
          String(getCurrentTariff(), 3) + " UAH/L</div></div>";
  html += "</div></div>";

  // Temp threshold (if enabled)
  if (tempThreshEnabled) {
    html += "<div class='card'><h2>Temp threshold</h2><div class='grid'>";
    char buf2[32];
    snprintf(buf2, sizeof(buf2), "%.1f C", tempThreshMin);
    html += String("<div><div class='lbl'>Min (below = skip)</div><div style='color:#7ed6df;font-weight:bold;'>") + buf2 + "</div></div>";
    snprintf(buf2, sizeof(buf2), "%.1f C", tempThreshMax);
    html += String("<div><div class='lbl'>Max (above = skip)</div><div style='color:#e55039;font-weight:bold;'>") + buf2 + "</div></div>";
    html += "</div><div style='color:#aaa;font-size:.8em;margin-top:6px;'>"
            "Water outside this range is NOT counted</div></div>";
  }

  // Daily log table
  html += "<div class='card'><h2>Daily log</h2>"
          "<table><tr>"
          "<th>Date</th>"
          "<th>Cold L</th><th>Cold UAH</th>"
          "<th>Warm L</th><th>Warm UAH</th>"
          "<th>Hot L</th><th>Hot UAH</th>"
          "<th>Total L</th><th>Total UAH</th>"
          "</tr>";

  float todayTot = todayCold + todayWarm + todayHot;
  char row[256];
  snprintf(row, sizeof(row),
    "<tr class='today'><td>%s &#9733;</td>"
    "<td class='cold'>%.3f</td><td class='cold'>%.2f</td>"
    "<td class='warm'>%.3f</td><td class='warm'>%.2f</td>"
    "<td class='hot'>%.3f</td><td class='hot'>%.2f</td>"
    "<td>%.3f</td><td>%.2f</td></tr>",
    todayDate,
    todayCold, todayCold * tarCold,
    todayWarm, todayWarm * tarWarm,
    todayHot,  todayHot  * tarHot,
    todayTot,  todayCost);
  html += row;

  for (int i = dayLogCount - 1; i >= 0; i--) {
    if (strcmp(dayLog[i].date, todayDate) == 0) continue;
    DayRecord& r = dayLog[i];
    float tot = r.volCold + r.volWarm + r.volHot;
    snprintf(row, sizeof(row),
      "<tr><td>%s</td>"
      "<td class='cold'>%.3f</td><td class='cold'>%.2f</td>"
      "<td class='warm'>%.3f</td><td class='warm'>%.2f</td>"
      "<td class='hot'>%.3f</td><td class='hot'>%.2f</td>"
      "<td>%.3f</td><td>%.2f</td></tr>",
      r.date,
      r.volCold, r.volCold * tarCold,
      r.volWarm, r.volWarm * tarWarm,
      r.volHot,  r.volHot  * tarHot,
      tot, r.cost);
    html += row;
  }
  html += "</table></div>";

  html += "<div style='text-align:center;margin-top:12px;'>"
          "<a href='/report.csv' class='btn btn-dl'>&#11015; Download CSV</a>"
          "<a href='/reset' class='btn btn-rst'>&#9888; Reset counter</a>"
          "</div>"
          "<div class='note'>Auto-refresh every 5s</div>"
          "</body></html>";

  return html;
}

static String buildCSV() {
  String csv = "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/csv; charset=utf-8\r\n"
               "Content-Disposition: attachment; filename=water_report.csv\r\n\r\n"
               "Date,Cold(L),Warm(L),Hot(L),Total(L),Cost(UAH)\r\n";

  for (int i = 0; i < dayLogCount; i++) {
    if (strcmp(dayLog[i].date, todayDate) == 0) continue;
    DayRecord& r = dayLog[i];
    float tot = r.volCold + r.volWarm + r.volHot;
    char line[80];
    snprintf(line, sizeof(line), "%s,%.3f,%.3f,%.3f,%.3f,%.2f\r\n",
             r.date, r.volCold, r.volWarm, r.volHot, tot, r.cost);
    csv += line;
  }
  float todayTot = todayCold + todayWarm + todayHot;
  char line[80];
  snprintf(line, sizeof(line), "%s(today),%.3f,%.3f,%.3f,%.3f,%.2f\r\n",
           todayDate, todayCold, todayWarm, todayHot, todayTot, todayCost);
  csv += line;
  return csv;
}

// --- Public handler ---

void handleWifi() {
  if (!wifiOn) return;
  WiFiClient client = webServer.available();
  if (!client) return;

  uint32_t timeout = millis() + 1500;
  String req = "";
  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      req += (char)client.read();
      if (req.endsWith("\r\n\r\n")) break;
    }
  }

  if (req.indexOf("GET /reset") >= 0) {
    totalVol = totalCost = 0.0f;
    todayCold = todayWarm = todayHot = todayCost = 0.0f;
    dayLogCount = 0;
    saveData(); saveDayLog();
  }

  if (req.indexOf("GET /report.csv") >= 0) {
    client.print(buildCSV());
    client.stop();
    return;
  }

  // Captive portal redirect
  bool isCaptive = (req.indexOf("/generate_204")   >= 0 ||
                    req.indexOf("/hotspot-detect")  >= 0 ||
                    req.indexOf("/ncsi.txt")        >= 0 ||
                    req.indexOf("/connecttest.txt") >= 0 ||
                    req.indexOf("/redirect")        >= 0 ||
                    req.indexOf("/canonical.html")  >= 0 ||
                    req.indexOf("/success.txt")     >= 0 ||
                    req.indexOf("/library/test")    >= 0);
  if (isCaptive) {
    client.print("HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1/\r\n\r\n");
    client.stop();
    return;
  }

  client.print(buildPage());
  client.stop();
}