#include "webserver.h"
#include "globals.h"
#include "analytics.h"
#include "clock.h"
#include "storage.h"

// ============================================================
//  webserver.cpp — AJAX dashboard + settings page
//  Endpoints:
//    GET  /           — main dashboard (AJAX, no meta refresh)
//    GET  /data       — JSON live data (~300 bytes, polled every 2s)
//    GET  /settings   — settings page (WiFi / Tariffs / Threshold)
//    GET  /networks   — JSON list of scanned WiFi networks
//    GET  /wifi       — connect to WiFi (?ssid=&pass=&def=1) → JSON
//    GET  /set-tariff — set tariffs (?cold=&warm=&hot=) → JSON
//    GET  /set-thresh — set threshold (?en=1&min=&max=) → JSON
//    GET  /report.csv — download CSV log
// ============================================================

WiFiServer webServer(80);

// ============================================================
//  Session token for /settings password protection
// ============================================================

static char sessionToken[17] = "";   // 16 hex chars + null
static bool sessionActive    = false;

static void generateToken() {
  const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 16; i++)
    sessionToken[i] = hex[esp_random() & 0xF];
  sessionToken[16] = '\0';
  sessionActive = true;
}

static bool checkSession(const String& req) {
  if (!sessionActive) return false;
  // Look for Cookie: wmtok=<token>
  String cookieHeader = "Cookie: ";
  int ci = req.indexOf(cookieHeader);
  if (ci < 0) return false;
  int cs = ci + cookieHeader.length();
  int ce = req.indexOf("\r\n", cs);
  String cookies = req.substring(cs, ce);
  String needle  = String("wmtok=") + sessionToken;
  return (cookies.indexOf(needle) >= 0);
}

// ---- WiFi connect (called from setup) ----

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

// ---- Helpers ----

static String urlDecode(const String& s) {
  String out = "";
  for (int i = 0; i < (int)s.length(); i++) {
    if (s[i] == '+') { out += ' '; }
    else if (s[i] == '%' && i + 2 < (int)s.length()) {
      char h[3] = {s[i+1], s[i+2], 0};
      out += (char)strtol(h, nullptr, 16);
      i += 2;
    } else { out += s[i]; }
  }
  return out;
}

static String getParam(const String& req, const char* key) {
  String k = String(key) + "=";
  int idx = req.indexOf(k);
  if (idx < 0) return "";
  idx += k.length();
  int end = req.indexOf('&', idx);
  if (end < 0) end = req.indexOf(' ', idx);
  if (end < 0) end = req.length();
  return urlDecode(req.substring(idx, end));
}

// ============================================================
//  /data  — lightweight JSON for AJAX polling
// ============================================================

static String buildJSON() {
  char dateStr[12] = "N/A", timeStr[12] = "N/A";
  getCurrentDateTimeStr(dateStr, timeStr);

  String j = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
             "Access-Control-Allow-Origin: *\r\nCache-Control: no-cache\r\n\r\n{";
  char buf[640];
  snprintf(buf, sizeof(buf),
    "\"flow\":%.2f,\"vol\":%.3f,\"cost\":%.2f,\"temp\":%.1f,"
    "\"tempCat\":\"%s\",\"tariff\":%.3f,"
    "\"date\":\"%s\",\"time\":\"%s\","
    "\"ntp\":%s,\"rtc\":%s,\"wifiHome\":%s,\"wifiSSID\":\"%s\","
    "\"tarCold\":%.3f,\"tarWarm\":%.3f,\"tarHot\":%.3f,"
    "\"threshEn\":%s,\"threshMin\":%.1f,\"threshMax\":%.1f,"
    "\"defaultSSID\":\"%s\","
    "\"todayC\":%.3f,\"todayW\":%.3f,\"todayH\":%.3f,\"todayCost\":%.2f,\"todayDate\":\"%s\"",
    flowLPM, totalVol, totalCost,
    waterTemp <= -50.0f ? 0.0f : waterTemp,
    waterTemp <= -50.0f ? "N/A" : getTempCat(),
    getCurrentTariff(),
    dateStr, timeStr,
    ntpSynced ? "true" : "false",
    rtcOk     ? "true" : "false",
    WiFi.status() == WL_CONNECTED ? "true" : "false",
    WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "",
    tarCold, tarWarm, tarHot,
    tempThreshEnabled ? "true" : "false",
    tempThreshMin, tempThreshMax,
    defaultSSID,
    todayCold, todayWarm, todayHot, todayCost, todayDate
  );
  j += buf;

  // Daily log array
  j += ",\"log\":[";
  char row[128];
  snprintf(row, sizeof(row),
    "{\"d\":\"%s\",\"c\":%.3f,\"w\":%.3f,\"h\":%.3f,\"cost\":%.2f,\"today\":true}",
    todayDate, todayCold, todayWarm, todayHot, todayCost);
  j += row;
  for (int i = dayLogCount - 1; i >= 0; i--) {
    if (strcmp(dayLog[i].date, todayDate) == 0) continue;
    DayRecord& r = dayLog[i];
    snprintf(row, sizeof(row),
      ",{\"d\":\"%s\",\"c\":%.3f,\"w\":%.3f,\"h\":%.3f,\"cost\":%.2f,\"today\":false}",
      r.date, r.volCold, r.volWarm, r.volHot, r.cost);
    j += row;
  }
  j += "]}";
  return j;
}

// ============================================================
//  /networks  — JSON list of scanned WiFi networks
// ============================================================

static String buildNetworksJSON() {
  String j = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
             "Access-Control-Allow-Origin: *\r\nCache-Control: no-cache\r\n\r\n{\"nets\":[";
  int n = WiFi.scanComplete();
  if (n < 0) {
    WiFi.scanNetworks(true);
    j += "],\"scanning\":true}";
    return j;
  }
  bool first = true;
  for (int i = 0; i < n && i < MAX_NETWORKS; i++) {
    if (!first) j += ",";
    first = false;
    char entry[80];
    String ssid = WiFi.SSID(i);
    ssid.replace("\"", "\\\"");
    snprintf(entry, sizeof(entry), "{\"s\":\"%s\",\"r\":%d}", ssid.c_str(), WiFi.RSSI(i));
    j += entry;
  }
  WiFi.scanNetworks(true); // re-trigger async for next call
  j += "],\"scanning\":false,\"current\":\"";
  if (WiFi.status() == WL_CONNECTED) j += WiFi.SSID();
  j += "\"}";
  return j;
}

// ============================================================
//  Main dashboard  /  — static HTML shell, data via AJAX
// ============================================================

static const char MAIN_PAGE[] PROGMEM =
  "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
  "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
  "<meta name='viewport' content='width=device-width,initial-scale=1'>"
  "<title>Water Meter</title><style>"
  "*{box-sizing:border-box;margin:0;padding:0}"
  "body{font-family:Arial,sans-serif;background:#0a0f1e;color:#e0f0ff;padding:12px}"
  "h1{color:#00d4ff;text-align:center;border-bottom:2px solid #00d4ff;padding-bottom:8px;margin-bottom:10px;font-size:1.3em}"
  ".nav{display:flex;gap:8px;margin-bottom:12px}"
  ".nav a{flex:1;text-align:center;padding:8px;border-radius:8px;text-decoration:none;font-size:.85em;border:1px solid #00d4ff44;color:#7bafc0}"
  ".nav a.act{background:#00d4ff22;border-color:#00d4ff;color:#00d4ff}"
  ".grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:10px}"
  ".card{background:#0d1b2e;border:1px solid #00d4ff22;border-radius:10px;padding:12px;margin-bottom:10px}"
  ".lbl{color:#7bafc0;font-size:.75em;margin-bottom:3px}"
  ".val{font-size:1.5em;color:#00ff9f;font-weight:bold;line-height:1.1}"
  ".val.warn{color:#ff6b6b}.val.cold{color:#7ed6df}.val.warm{color:#f9ca24}.val.hot{color:#e55039}"
  ".sub{color:#aaa;font-size:.75em;margin-top:3px}"
  ".dt{text-align:center;color:#7bafc0;font-size:.8em;margin-bottom:10px}"
  ".tag{display:inline-block;padding:2px 7px;border-radius:4px;font-size:.7em;font-weight:bold}"
  ".tok{background:#00ff9f22;color:#00ff9f;border:1px solid #00ff9f44}"
  ".tno{background:#ff6b6b22;color:#ff6b6b;border:1px solid #ff6b6b44}"
  ".dot{display:inline-block;width:8px;height:8px;border-radius:50%;background:#ff6b6b;margin-right:5px;vertical-align:middle}"
  ".dot.ok{background:#00ff9f}"
  ".tar-row{display:flex;gap:8px;margin-top:6px}"
  ".tbox{flex:1;background:#060e1c;border:1px solid #00d4ff22;border-radius:8px;padding:8px;text-align:center}"
  ".tbox .val{font-size:1em}"
  "table{width:100%;border-collapse:collapse;font-size:.78em}"
  "th{color:#7bafc0;text-align:left;padding:5px 6px;border-bottom:1px solid #00d4ff33}"
  "td{padding:4px 6px;border-bottom:1px solid #ffffff08}"
  ".tr-t td{color:#00ff9f}"
  ".c{color:#7ed6df}.w{color:#f9ca24}.h{color:#e55039}"
  ".btn-dl{display:inline-block;margin-top:10px;padding:9px 18px;background:#00d4ff15;border:1px solid #00d4ff;color:#00d4ff;border-radius:8px;text-decoration:none;font-size:.85em}"
  "</style></head><body>"
  "<h1>Water Meter</h1>"
  "<div class='nav'><a href='/' class='act'>Dashboard</a><a href='/settings'>Settings</a></div>"
  "<div class='dt'><span class='dot' id='dot'></span><span id='dt-d'>--.--.----</span> &nbsp;<b id='dt-t'>--:--:--</b> &nbsp;<span id='dt-s'></span></div>"
  "<div class='grid'>"
  "<div class='card'><div class='lbl'>Flow rate</div><div class='val' id='v-flow'>--</div></div>"
  "<div class='card'><div class='lbl'>Temperature</div><div class='val' id='v-temp'>--</div><div class='sub' id='v-tsub'></div></div>"
  "<div class='card'><div class='lbl'>Total volume</div><div class='val' id='v-vol'>--</div></div>"
  "<div class='card'><div class='lbl'>Total cost</div><div class='val' id='v-cost'>--</div></div>"
  "</div>"
  "<div class='card'>"
  "<div class='lbl'>Tariffs &nbsp;<span class='sub'>Active: <b id='v-tar' style='color:#00ff9f'>--</b></span></div>"
  "<div class='tar-row'>"
  "<div class='tbox'><div class='lbl cold'>Cold &lt;20C</div><div class='val cold' id='t-c'>--</div></div>"
  "<div class='tbox'><div class='lbl warm'>Warm 20-45C</div><div class='val warm' id='t-w'>--</div></div>"
  "<div class='tbox'><div class='lbl hot'>Hot &gt;45C</div><div class='val hot' id='t-h'>--</div></div>"
  "</div></div>"
  "<div class='card'>"
  "<div class='lbl' style='margin-bottom:8px'>Daily log</div>"
  "<table><thead><tr><th>Date</th><th class='c'>Cold</th><th class='w'>Warm</th><th class='h'>Hot</th><th>L</th><th>UAH</th></tr></thead>"
  "<tbody id='log'><tr><td colspan='6' style='color:#555;text-align:center'>Loading...</td></tr></tbody></table>"
  "<div style='text-align:center'><a href='/report.csv' class='btn-dl'>&#11015; Download CSV</a></div>"
  "</div>"
  "<script>"
  "function upd(){fetch('/data').then(r=>r.json()).then(function(d){"
  "document.getElementById('dt-d').textContent=d.date;"
  "document.getElementById('dt-t').textContent=d.time;"
  "var dot=document.getElementById('dot');dot.className='dot'+(d.wifiHome?' ok':'');"
  "var s=d.ntp?\"<span class='tag tok'>NTP</span>\":d.rtc?\"<span class='tag tok'>RTC</span>\":\"<span class='tag tno'>no sync</span>\";"
  "document.getElementById('dt-s').innerHTML=s;"
  "document.getElementById('v-flow').textContent=d.flow.toFixed(2)+' L/min';"
  "var te=document.getElementById('v-temp'),ts=document.getElementById('v-tsub');"
  "if(d.temp===0&&d.tempCat==='N/A'){te.className='val warn';te.textContent='N/A';ts.textContent='Sensor not connected';}"
  "else{var cl=d.tempCat==='COLD'?'cold':d.tempCat==='WARM'?'warm':'hot';"
  "te.className='val '+cl;te.textContent=d.temp.toFixed(1)+'\u00b0C';"
  "ts.textContent='['+d.tempCat+'] '+d.tariff.toFixed(3)+' UAH/L';}"
  "document.getElementById('v-vol').textContent=d.vol.toFixed(3)+' L';"
  "document.getElementById('v-cost').textContent=d.cost.toFixed(2)+' UAH';"
  "document.getElementById('t-c').textContent=d.tarCold.toFixed(3);"
  "document.getElementById('t-w').textContent=d.tarWarm.toFixed(3);"
  "document.getElementById('t-h').textContent=d.tarHot.toFixed(3);"
  "document.getElementById('v-tar').textContent=d.tariff.toFixed(3)+' UAH/L';"
  "var tb=document.getElementById('log'),html='';"
  "d.log.forEach(function(r){"
  "var tot=(r.c+r.w+r.h).toFixed(3);"
  "html+=(r.today?\"<tr class='tr-t'><td>\"+r.d+\" \u2605</td>\":\"<tr><td>\"+r.d+\"</td>\");"
  "html+=\"<td class='c'>\"+r.c.toFixed(3)+\"</td><td class='w'>\"+r.w.toFixed(3)+\"</td>\";"
  "html+=\"<td class='h'>\"+r.h.toFixed(3)+\"</td><td>\"+tot+\"</td><td>\"+r.cost.toFixed(2)+\"</td></tr>\";});"
  "tb.innerHTML=html;"
  "}).catch(function(){});}"
  "upd();setInterval(upd,200);"
  "</script></body></html>";

// ============================================================
//  /settings-login  — password entry page
// ============================================================

static String buildLoginPage(bool wrongPass = false) {
  String html =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Settings — Water Meter</title><style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:Arial,sans-serif;background:#0a0f1e;color:#e0f0ff;"
    "display:flex;align-items:center;justify-content:center;min-height:100vh;padding:16px}"
    ".box{background:#0d1b2e;border:1px solid #00d4ff33;border-radius:14px;padding:28px 24px;width:100%;max-width:340px}"
    "h1{color:#00d4ff;text-align:center;font-size:1.2em;margin-bottom:6px}"
    ".sub{color:#7bafc0;text-align:center;font-size:.8em;margin-bottom:20px}"
    ".lbl{color:#7bafc0;font-size:.8em;display:block;margin-bottom:5px}"
    "input[type=password]{background:#060e1c;border:1px solid #00d4ff44;border-radius:8px;"
    "color:#e0f0ff;padding:10px 12px;width:100%;font-size:1em;letter-spacing:.1em}"
    "input[type=password]:focus{outline:none;border-color:#00d4ff}"
    ".btn{display:block;width:100%;padding:11px;margin-top:14px;border-radius:8px;"
    "font-size:.95em;cursor:pointer;border:none;font-weight:bold;"
    "background:#00d4ff20;border:1px solid #00d4ff;color:#00d4ff}"
    ".err{color:#ff6b6b;font-size:.82em;text-align:center;margin-top:10px}"
    ".lock{text-align:center;font-size:2em;margin-bottom:10px}"
    ".back{display:block;text-align:center;margin-top:14px;color:#7bafc0;font-size:.8em;text-decoration:none}"
    "</style></head><body>"
    "<div class='box'>"
    "<div class='lock'>LOCK</div>"
    "<h1>Settings Access</h1>"
    "<div class='sub'>Enter password to continue</div>"
    "<label class='lbl'>Password</label>"
    "<input type='password' id='p' name='p' maxlength='7' autofocus autocomplete='off'>"
    + String(wrongPass ? "<div class='err'>Wrong password. Try again.</div>" : "") +
    "<button class='btn' onclick='doLogin()'>Unlock Settings</button>"
    "<a href='/' class='back'>Back to Dashboard</a>"
    "</div>"
    "<script>"
    "document.getElementById('p').addEventListener('keydown',function(e){if(e.key==='Enter')doLogin();});"
    "function doLogin(){"
    "var p=document.getElementById('p').value;"
    "fetch('/settings-login?p='+encodeURIComponent(p))"
    ".then(r=>r.json()).then(function(d){"
    "if(d.ok){window.location.href='/settings';}"
    "else{document.location.reload();}"  // reload will show err via redirect
    "}).catch(function(){});}"
    "</script></body></html>";
  return html;
}

// ============================================================
//  Settings page  /settings
// ============================================================

static String buildSettingsPage() {
  String html =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
    "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Settings — Water Meter</title><style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:Arial,sans-serif;background:#0a0f1e;color:#e0f0ff;padding:12px}"
    "h1{color:#00d4ff;text-align:center;border-bottom:2px solid #00d4ff;padding-bottom:8px;margin-bottom:10px;font-size:1.3em}"
    ".nav{display:flex;gap:8px;margin-bottom:12px}"
    ".nav a{flex:1;text-align:center;padding:8px;border-radius:8px;text-decoration:none;font-size:.85em;border:1px solid #00d4ff44;color:#7bafc0}"
    ".nav a.act{background:#00d4ff22;border-color:#00d4ff;color:#00d4ff}"
    ".card{background:#0d1b2e;border:1px solid #00d4ff22;border-radius:10px;padding:14px;margin-bottom:10px}"
    "h2{color:#00d4ff;font-size:1em;margin-bottom:10px;border-bottom:1px solid #00d4ff22;padding-bottom:6px}"
    ".lbl{color:#7bafc0;font-size:.8em;margin:8px 0 3px;display:block}"
    "input[type=text],input[type=password],input[type=number],select{"
    "background:#060e1c;border:1px solid #00d4ff44;border-radius:6px;color:#e0f0ff;padding:8px 10px;width:100%;font-size:.9em}"
    "input[type=number]{-moz-appearance:textfield}"
    ".btn{display:block;width:100%;padding:10px;margin-top:10px;border-radius:8px;font-size:.9em;cursor:pointer;border:none;font-weight:bold}"
    ".bp{background:#00d4ff15;border:1px solid #00d4ff!important;color:#00d4ff}"
    ".bs{background:#00ff9f15;border:1px solid #00ff9f!important;color:#00ff9f}"
    ".msg-ok{color:#00ff9f;font-size:.85em;margin-top:8px}"
    ".msg-er{color:#ff6b6b;font-size:.85em;margin-top:8px}"
    ".stat-ok{color:#00ff9f;font-size:.85em;padding:4px 0}"
    ".stat-no{color:#ff6b6b;font-size:.85em;padding:4px 0}"
    ".note{color:#555;font-size:.75em;margin-top:5px}"
    ".toggle{display:flex;align-items:center;gap:10px;margin-top:8px}"
    ".toggle input{width:auto!important;accent-color:#00d4ff;transform:scale(1.3)}"
    ".scan-btn{display:inline-block;padding:6px 14px;margin-top:6px;background:#00d4ff11;"
    "border:1px solid #00d4ff33;color:#7bafc0;border-radius:6px;font-size:.8em;cursor:pointer}"
    "#scan-st{color:#555;font-size:.75em;margin-top:4px}"
    ".row2{display:flex;gap:8px}"
    ".row2>div{flex:1}"
    "</style></head><body>"
    "<h1>Settings</h1>"
    "<div class='nav'><a href='/'>Dashboard</a><a href='/settings' class='act'>Settings</a>"
    "<a href='/settings-logout' style='color:#ff6b6b;border-color:#ff6b6b44'>&#128274; Lock</a></div>";

  // ---- WiFi section ----
  html += "<div class='card'><h2>WiFi Connection</h2>";
  if (WiFi.status() == WL_CONNECTED) {
    html += "<div class='stat-ok'>&#10004; Connected: <b>" + WiFi.SSID() + "</b>";
    if (ntpSynced) html += " | NTP OK";
    html += "</div>";
  } else {
    html += "<div class='stat-no'>&#10008; Not connected to home network</div>";
  }
  if (strlen(defaultSSID) > 0)
    html += "<div class='note'>Auto-connect: " + String(defaultSSID) + "</div>";

  html +=
    "<label class='lbl'>Network</label>"
    "<select id='ssel'><option>-- tap Scan --</option></select>"
    "<span class='scan-btn' onclick='scanNets()'>Scan networks</span>"
    "<div id='scan-st'></div>"
    "<label class='lbl'>Password</label>"
    "<input type='password' id='wpass' placeholder='WiFi password' maxlength='63'>"
    "<div class='toggle'>"
    "<input type='checkbox' id='wdef' checked>"
    "<label for='wdef' style='font-size:.85em;color:#7bafc0'>Set as default (auto-connect on boot)</label>"
    "</div>"
    "<button class='btn bp' id='wbtn' onclick='doWifi()'>Connect</button>"
    "<div id='wmsg'></div>"
    "</div>";

  // ---- Tariffs section ----
  char buf[16];
  snprintf(buf, sizeof(buf), "%.3f", tarCold); String vc = buf;
  snprintf(buf, sizeof(buf), "%.3f", tarWarm); String vw = buf;
  snprintf(buf, sizeof(buf), "%.3f", tarHot);  String vh = buf;

  html +=
    "<div class='card'><h2> Tariffs (UAH / L)</h2>"
    "<div class='row2'>"
    "<div><label class='lbl' style='color:#7ed6df'>Cold (&lt;20°C)</label>"
    "<input type='number' id='tc' step='0.001' min='0.001' value='" + vc + "'></div>"
    "<div><label class='lbl' style='color:#f9ca24'>Warm (20-45°C)</label>"
    "<input type='number' id='tw' step='0.001' min='0.001' value='" + vw + "'></div>"
    "</div>"
    "<label class='lbl' style='color:#e55039'>Hot (&gt;45°C)</label>"
    "<input type='number' id='th' step='0.001' min='0.001' value='" + vh + "'>"
    "<button class='btn bs' onclick='doTariff()'>Save tariffs</button>"
    "<div id='tmsg'></div>"
    "</div>";

  // ---- Threshold section ----
  snprintf(buf, sizeof(buf), "%.1f", tempThreshMin); String vmn = buf;
  snprintf(buf, sizeof(buf), "%.1f", tempThreshMax); String vmx = buf;

  html +=
    "<div class='card'><h2> Temperature Threshold</h2>"
    "<div class='toggle'>"
    "<input type='checkbox' id='ten'" + String(tempThreshEnabled ? " checked" : "") + ">"
    "<label for='ten' style='font-size:.85em;color:#7bafc0'>Enable temperature filter</label>"
    "</div>"
    "<div class='note'>Water outside the range below will NOT be counted</div>"
    "<div class='row2'>"
    "<div><label class='lbl'>Min (°C)</label>"
    "<input type='number' id='tmin' step='0.5' min='-10' max='99' value='" + vmn + "'></div>"
    "<div><label class='lbl'>Max (°C)</label>"
    "<input type='number' id='tmax' step='0.5' min='1' max='100' value='" + vmx + "'></div>"
    "</div>"
    "<button class='btn bs' onclick='doThresh()'>Save threshold</button>"
    "<div id='xmsg'></div>"
    "</div>";

  // ---- JS ----
  html +=
    "<script>"

    "function showMsg(id,ok,txt){"
    "var e=document.getElementById(id);"
    "e.className=ok?'msg-ok':'msg-er';"
    "e.textContent=(ok?'\u2714 ':'\u2718 ')+txt;}"

    // Scan
    "function scanNets(){"
    "document.getElementById('scan-st').textContent='Scanning...';"
    "document.getElementById('ssel').innerHTML='<option>Scanning...</option>';"
    "setTimeout(loadNets,2500);}"

    "function loadNets(){"
    "fetch('/networks').then(r=>r.json()).then(function(d){"
    "if(d.scanning){document.getElementById('scan-st').textContent='Still scanning...';setTimeout(loadNets,2000);return;}"
    "var sel=document.getElementById('ssel');sel.innerHTML='';"
    "if(!d.nets.length){sel.innerHTML='<option>No networks found</option>';document.getElementById('scan-st').textContent='';return;}"
    "d.nets.forEach(function(n){"
    "var o=document.createElement('option');o.value=n.s;"
    "o.textContent=n.s+' ('+n.r+' dBm)';"
    "if(n.s===d.current)o.selected=true;"
    "sel.appendChild(o);});"
    "document.getElementById('scan-st').textContent='Found '+d.nets.length+' networks';"
    "}).catch(function(){document.getElementById('scan-st').textContent='Scan error';});}"

    // WiFi connect
    "function doWifi(){"
    "var ss=document.getElementById('ssel').value;"
    "var ps=document.getElementById('wpass').value;"
    "var df=document.getElementById('wdef').checked?'1':'0';"
    "var btn=document.getElementById('wbtn');"
    "if(!ss||ss.startsWith('--')||ss==='Scanning...'){showMsg('wmsg',false,'Select a network first');return;}"
    "btn.disabled=true;btn.textContent='Connecting...';"
    "showMsg('wmsg',true,'Connecting to '+ss+'...');"
    "fetch('/wifi?ssid='+encodeURIComponent(ss)+'&pass='+encodeURIComponent(ps)+'&def='+df)"
    ".then(r=>r.json()).then(function(d){"
    "btn.disabled=false;btn.textContent='Connect';"
    "if(d.ok)showMsg('wmsg',true,'Connected to '+ss+(d.ntp?' | NTP synced':''));"
    "else showMsg('wmsg',false,'Failed — check password');}"
    ").catch(function(){btn.disabled=false;btn.textContent='Connect';showMsg('wmsg',false,'Request timeout');});}"

    // Tariffs
    "function doTariff(){"
    "var c=parseFloat(document.getElementById('tc').value);"
    "var w=parseFloat(document.getElementById('tw').value);"
    "var h=parseFloat(document.getElementById('th').value);"
    "if([c,w,h].some(function(v){return isNaN(v)||v<=0;})){showMsg('tmsg',false,'Values must be positive');return;}"
    "showMsg('tmsg',true,'Saving...');"
    "fetch('/set-tariff?cold='+c+'&warm='+w+'&hot='+h)"
    ".then(r=>r.json()).then(function(d){showMsg('tmsg',d.ok,'Saved!')})"
    ".catch(function(){showMsg('tmsg',false,'Failed');});}"

    // Threshold
    "function doThresh(){"
    "var en=document.getElementById('ten').checked?'1':'0';"
    "var mn=parseFloat(document.getElementById('tmin').value);"
    "var mx=parseFloat(document.getElementById('tmax').value);"
    "if(isNaN(mn)||isNaN(mx)){showMsg('xmsg',false,'Invalid values');return;}"
    "if(mn>=mx){showMsg('xmsg',false,'Min must be less than Max');return;}"
    "showMsg('xmsg',true,'Saving...');"
    "fetch('/set-thresh?en='+en+'&min='+mn+'&max='+mx)"
    ".then(r=>r.json()).then(function(d){showMsg('xmsg',d.ok,'Saved!')})"
    ".catch(function(){showMsg('xmsg',false,'Failed');});}"

    "scanNets();"
    "</script></body></html>";

  return html;
}

// ============================================================
//  /report.csv
// ============================================================

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

// ============================================================
//  Main request handler — called from loop() via millis()
// ============================================================

void handleWifi() {
  if (!wifiOn) return;

  WiFiClient client = webServer.available();
  if (!client) return;

  uint32_t timeout = millis() + 2000;
  String req = "";
  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      req += (char)client.read();
      if (req.endsWith("\r\n\r\n")) break;
    }
  }

  // Extract path from first line: "GET /path HTTP/1.1"
  String path = "";
  int sp1 = req.indexOf(' ');
  int sp2 = req.indexOf(' ', sp1 + 1);
  if (sp1 >= 0 && sp2 > sp1) path = req.substring(sp1 + 1, sp2);

  // /data — AJAX live data JSON
  if (path.startsWith("/data")) {
    client.print(buildJSON());
    client.stop(); return;
  }

  // /networks — AJAX WiFi scan list JSON
  if (path.startsWith("/networks")) {
    client.print(buildNetworksJSON());
    client.stop(); return;
  }

  // /wifi?ssid=...&pass=...&def=1  — connect, returns JSON
  if (path.startsWith("/wifi") && req.indexOf("ssid=") >= 0) {
    String ssid   = getParam(req, "ssid");
    String pass   = getParam(req, "pass");
    bool   setDef = (getParam(req, "def") == "1");

    Serial.printf("[WiFi-Web] Connecting to: %s\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(100);

    bool ok = (WiFi.status() == WL_CONNECTED);
    if (ok) {
      strncpy(homeSSID, ssid.c_str(), 32);
      strncpy(homePass, pass.c_str(), 64);
      if (setDef) {
        strncpy(defaultSSID, homeSSID, 32);
        strncpy(defaultPass, homePass, 64);
      }
      saveData();
      syncNTP();
      Serial.printf("[WiFi-Web] OK! NTP:%s\n", ntpSynced ? "OK" : "no");
    } else {
      Serial.println("[WiFi-Web] Failed");
    }

    String resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                  "Access-Control-Allow-Origin: *\r\n\r\n{";
    resp += ok ? "\"ok\":true" : "\"ok\":false";
    resp += String(",\"ntp\":") + (ntpSynced ? "true" : "false") + "}";
    client.print(resp);
    client.stop(); return;
  }

  // /set-tariff?cold=&warm=&hot=  — save tariffs, returns JSON
  if (path.startsWith("/set-tariff")) {
    float c = getParam(req, "cold").toFloat();
    float w = getParam(req, "warm").toFloat();
    float h = getParam(req, "hot").toFloat();
    bool ok = (c > 0 && w > 0 && h > 0);
    if (ok) {
      tarCold = c; tarWarm = w; tarHot = h;
      saveData();
      Serial.printf("[TAR] Cold=%.3f Warm=%.3f Hot=%.3f\n", tarCold, tarWarm, tarHot);
    }
    client.print(String("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                        "Access-Control-Allow-Origin: *\r\n\r\n") +
                 (ok ? "{\"ok\":true}" : "{\"ok\":false}"));
    client.stop(); return;
  }

  // /set-thresh?en=1&min=&max=  — save threshold, returns JSON
  if (path.startsWith("/set-thresh")) {
    float mn = getParam(req, "min").toFloat();
    float mx = getParam(req, "max").toFloat();
    bool  en = (getParam(req, "en") == "1");
    bool  ok = (mn < mx && mx <= 100.0f);
    if (ok) {
      tempThreshMin = mn; tempThreshMax = mx; tempThreshEnabled = en;
      saveData();
      Serial.printf("[THRESH] %s %.1f-%.1fC\n", en ? "ON" : "OFF", mn, mx);
    }
    client.print(String("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                        "Access-Control-Allow-Origin: *\r\n\r\n") +
                 (ok ? "{\"ok\":true}" : "{\"ok\":false}"));
    client.stop(); return;
  }

  // /settings-logout  — clear session
  if (path.startsWith("/settings-logout")) {
    sessionActive = false;
    sessionToken[0] = '\0';
    client.print("HTTP/1.1 302 Found\r\nLocation: /\r\n"
                 "Set-Cookie: wmtok=; Path=/; Max-Age=0\r\n\r\n");
    client.stop(); return;
  }

  // /settings-login?p=<password>  — verify password, set cookie
  if (path.startsWith("/settings-login")) {
    String entered = getParam(req, "p");
    bool ok = (entered == String(settingsPass));
    String resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
    if (ok) {
      generateToken();
      resp += String("Set-Cookie: wmtok=") + sessionToken +
              "; Path=/; HttpOnly\r\n";
      resp += "\r\n{\"ok\":true}";
    } else {
      resp += "\r\n{\"ok\":false}";
    }
    client.print(resp);
    client.stop(); return;
  }

  // /settings — require session
  if (path.startsWith("/settings")) {
    if (!checkSession(req)) {
      // Not authenticated — show login page
      client.print(buildLoginPage());
      client.stop(); return;
    }
    client.print(buildSettingsPage());
    client.stop(); return;
  }

  // /report.csv
  if (path.startsWith("/report.csv")) {
    client.print(buildCSV());
    client.stop(); return;
  }

  // Captive portal redirect
  bool isCaptive = (path.indexOf("/generate_204")   >= 0 ||
                    path.indexOf("/hotspot-detect")  >= 0 ||
                    path.indexOf("/ncsi.txt")        >= 0 ||
                    path.indexOf("/connecttest.txt") >= 0 ||
                    path.indexOf("/redirect")        >= 0 ||
                    path.indexOf("/canonical.html")  >= 0 ||
                    path.indexOf("/success.txt")     >= 0 ||
                    path.indexOf("/library/test")    >= 0);
  if (isCaptive) {
    client.print("HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1/\r\n\r\n");
    client.stop(); return;
  }

  // / — main dashboard
  client.print(FPSTR(MAIN_PAGE));
  client.stop();
}