#include <WiFi.h>
#include <Wire.h>
#include <Preferences.h>
#include <Firebase_ESP_Client.h>
#include <addons/TokenHelper.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>

// ── Credentials ──────────────────────────────────────────────
// NOTE: WiFi SSID/password are NO LONGER hardcoded. They are provisioned
// over USB serial from the website and stored in NVS. See ss_wifi
// namespace / wifiProvisioning() below.
#define FIREBASE_API_KEY     "AIzaSyA0wVFI0tE4RNY8WLlqF5QGr0hsTw9hL7Y"
#define FIREBASE_DB_URL      "https://calender-82004-default-rtdb.firebaseio.com/"

// ── Pin / screen ─────────────────────────────────────────────
#define OLED_SDA        8
#define OLED_SCL        9
#define BUZZER_PIN      3
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64

// ── Timing ───────────────────────────────────────────────────
#define POLL_INTERVAL_MS       30000UL
#define HEARTBEAT_INTERVAL_MS  300000UL
#define WIFI_TIMEOUT_MS        15000UL
#define WIFI_MAX_ATTEMPTS      3
#define DEFAULT_ALERT_MINUTES  30
#define DEFAULT_BUZZER_ENABLED true
#define SERIAL_LINE_MAX        256

// ── Global objects ────────────────────────────────────────────
FirebaseData     fbdo;
FirebaseData     fbdoSettings;
FirebaseAuth     fbAuth;
FirebaseConfig   fbConfig;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Preferences      prefs;

// ── Device state ─────────────────────────────────────────────
char ownerUid[64] = "";
char deviceId[32] = "";
bool fbSignedUp   = false;

// ── WiFi credential state (loaded from NVS) ───────────────────
char wifiSsid[64]     = "";
char wifiPassword[64] = "";

// ── Alert dedup ───────────────────────────────────────────────
bool alertedLast          = false;
char lastAlertedTitle[64] = "";

// ── Settings ─────────────────────────────────────────────────
int  alertMinutes  = DEFAULT_ALERT_MINUTES;
bool buzzerEnabled = DEFAULT_BUZZER_ENABLED;

// ── Loop timers ───────────────────────────────────────────────
unsigned long lastPollMs      = 0;
unsigned long lastHeartbeatMs = 0;

// Forward decls
bool connectWiFi();
void wifiProvisioning();
bool tryApplyWifiCreds(const char* ssid, const char* password, char* reasonOut, size_t reasonLen);

// ═════════════════════════════════════════════════════════════
//  OLED
// ═════════════════════════════════════════════════════════════

void oledInit() {
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED init failed")); return;
  }
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay(); display.display();
}

void oledShow(const char* line1, const char* line2 = "", const char* line3 = "",
              const char* line4 = "", const char* line5 = "") {
  display.clearDisplay(); display.setTextSize(1);
  if (line1[0]) { display.setCursor(0,  0); display.println(line1); }
  if (line2[0]) { display.setCursor(0, 12); display.println(line2); }
  if (line3[0]) { display.setCursor(0, 24); display.println(line3); }
  if (line4[0]) { display.setCursor(0, 36); display.println(line4); }
  if (line5[0]) { display.setCursor(0, 50); display.println(line5); }
  display.display();
}

/*
 * Two-event layout (128×64) — textSize=1 (each row ~10px):
 *
 *  ┌──────────────────────────────┐ y=0
 *  │ Mon Jun 16  7:19 AM (invert) │ live clock              h=10
 *  │ Doctor Appointment           │ event 1 title           y=12
 *  │ Jun 16  6:00 PM              │ event 1 date+time       y=22
 *  │ Room 3B                      │ location (if any)       y=32
 *  │ in 55m                       │ countdown               y=42
 *  ├──────────────────────────────┤ divider                 y=52
 *  │ UP:Science Appt  7:00 PM     │ event 2 compact         y=54
 *  └──────────────────────────────┘
 */
void oledTwoEvents(const char* title1, const char* time1, const char* loc1,
                   const char* countdown1, bool /*alert — unused, buzz handles it*/,
                   const char* title2,    const char* time2) {
  display.clearDisplay(); display.setTextSize(1);

  // ── Live clock header (inverted) ─────────────────────────
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK); display.setCursor(2, 1);
  char nowStr[22];
  formatDateTime(time(nullptr), nowStr, sizeof(nowStr));
  display.print(nowStr);
  display.setTextColor(SSD1306_WHITE);

  // ── Event 1 title ────────────────────────────────────────
  char t1[21]; strncpy(t1, title1, 20); t1[20] = '\0';
  display.setCursor(0, 12); display.print(t1);

  // ── Event 1 date + time ──────────────────────────────────
  display.setCursor(0, 22); display.print(time1);

  // ── Location (only if non-empty) ─────────────────────────
  if (loc1 && loc1[0]) {
    char l1[21]; strncpy(l1, loc1, 20); l1[20] = '\0';
    display.setCursor(0, 32); display.print(l1);
  }

  // ── Countdown ────────────────────────────────────────────
  display.setCursor(0, 42); display.print(countdown1);

  // ── Divider ──────────────────────────────────────────────
  display.drawLine(0, 52, 127, 52, SSD1306_WHITE);

  // ── Event 2 compact ──────────────────────────────────────
  display.setCursor(0, 54);
  if (title2 && title2[0]) {
    char t2[11]; strncpy(t2, title2, 10); t2[10] = '\0';
    char ln[28]; snprintf(ln, sizeof(ln), "UP:%-10s %s", t2, time2);
    display.print(ln);
  } else {
    display.print(F("No other events"));
  }
  display.display();
}

void oledPairingCode(const char* code) {
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(8, 2);   display.println(F("SmartSchedule AI"));
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  display.setCursor(0, 18);  display.println(F("Pairing code:"));
  display.setTextSize(2); display.setCursor(10, 34); display.println(code);
  display.setTextSize(1); display.setCursor(0, 56);  display.println(F("Enter in the app"));
  display.display();
}

void oledProvisioning(const char* status) {
  display.clearDisplay(); display.setTextSize(1);
  display.setCursor(4, 2);   display.println(F("SmartSchedule AI"));
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  display.setCursor(0, 20);  display.println(F("Connect USB cable"));
  display.setCursor(0, 31);  display.println(F("Open the website ->"));
  display.setCursor(0, 42);  display.println(F("Devices > Connect"));
  display.setCursor(0, 55);  display.print(F("Status: "));
  display.print(status);
  display.display();
}

// ═════════════════════════════════════════════════════════════
//  Buzzer
// ═════════════════════════════════════════════════════════════

void beep(int freq, int durationMs) {
  tone(BUZZER_PIN, freq, durationMs); delay(durationMs + 20); noTone(BUZZER_PIN);
}

// 5-second alert buzz — plays as 10 × 500ms pulses so the WDT
// gets fed every iteration and the sound has a slight pulsing
// quality rather than a flat drone (easier to notice).
void alertBuzz() {
  Serial.println(F("ALERT BUZZ — 5 seconds"));
  for (int i = 0; i < 10; i++) {
    esp_task_wdt_reset();
    tone(BUZZER_PIN, 1800);   // start tone (no duration = continuous)
    delay(400);
    noTone(BUZZER_PIN);
    delay(100);               // tiny gap between pulses
  }
  noTone(BUZZER_PIN);         // ensure it's definitely off
}

void confirmBeep() { beep(2000, 80); }
void errorBeep()   { beep(300, 150); delay(60); beep(300, 150); }

// ═════════════════════════════════════════════════════════════
//  NVS — WiFi credentials
// ═════════════════════════════════════════════════════════════

bool nvsLoadWifiCreds() {
  prefs.begin("ss_wifi", true);   // read-only
  String s = prefs.getString("ssid", "");
  String p = prefs.getString("password", "");
  prefs.end();
  if (s.length() == 0) return false;
  strncpy(wifiSsid, s.c_str(), sizeof(wifiSsid)-1);     wifiSsid[sizeof(wifiSsid)-1]     = '\0';
  strncpy(wifiPassword, p.c_str(), sizeof(wifiPassword)-1); wifiPassword[sizeof(wifiPassword)-1] = '\0';
  return true;
}

void nvsSaveWifiCreds(const char* ssid, const char* password) {
  prefs.begin("ss_wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();
  strncpy(wifiSsid, ssid, sizeof(wifiSsid)-1);         wifiSsid[sizeof(wifiSsid)-1]     = '\0';
  strncpy(wifiPassword, password, sizeof(wifiPassword)-1); wifiPassword[sizeof(wifiPassword)-1] = '\0';
}

void nvsClearWifiCreds() {
  prefs.begin("ss_wifi", false);
  prefs.remove("ssid");
  prefs.remove("password");
  prefs.end();
  wifiSsid[0] = '\0'; wifiPassword[0] = '\0';
}

// ═════════════════════════════════════════════════════════════
//  NVS — device identity (owner uid, device id, pairing code)
// ═════════════════════════════════════════════════════════════

// Returns true if a valid ownerUid was found in NVS
bool nvsLoadOwnerUid() {
  prefs.begin("ss_device", true);          // read-only
  String uid = prefs.getString("owner_uid", "");
  prefs.end();
  if (uid.length() == 0) return false;
  strncpy(ownerUid, uid.c_str(), sizeof(ownerUid)-1);
  ownerUid[sizeof(ownerUid)-1] = '\0';
  return true;
}

void nvsSaveOwnerUid(const char* uid) {
  prefs.begin("ss_device", false);
  prefs.putString("owner_uid", uid);
  prefs.end();
}

void ensureDeviceId() {
  prefs.begin("ss_device", false);
  String stored = prefs.getString("device_id", "");
  if (stored.length() == 0) {
    String mac = WiFi.macAddress(); mac.replace(":", "");
    snprintf(deviceId, sizeof(deviceId), "esp32c3_%s", mac.c_str());
    prefs.putString("device_id", deviceId);
    Serial.print(F("New device ID: ")); Serial.println(deviceId);
  } else {
    strncpy(deviceId, stored.c_str(), sizeof(deviceId)-1);
  }
  prefs.end();
  Serial.print(F("Device ID: ")); Serial.println(deviceId);
}

void getPairingCode(char* out, size_t outLen) {
  prefs.begin("ss_device", false);
  String code = prefs.getString("pair_code", "");
  if (code.length() == 0) {
    const char* chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    randomSeed(esp_random());
    char tmp[7] = "";
    for (int i = 0; i < 6; i++) tmp[i] = chars[random(32)];
    tmp[6] = '\0';
    prefs.putString("pair_code", tmp);
    strncpy(out, tmp, outLen-1); out[outLen-1] = '\0';
  } else {
    strncpy(out, code.c_str(), outLen-1); out[outLen-1] = '\0';
  }
  prefs.end();
}

void clearPairingCode() {
  prefs.begin("ss_device", false); prefs.remove("pair_code"); prefs.end();
}

// ═════════════════════════════════════════════════════════════
//  WiFi connect (using whatever is currently in wifiSsid/wifiPassword)
//  Returns true on success. Single attempt with WIFI_TIMEOUT_MS budget.
//  The "try 3 times" policy lives in the callers (wifiProvisioning /
//  setup), since each context needs slightly different UI feedback
//  between attempts.
// ═════════════════════════════════════════════════════════════

bool connectWiFiOnce() {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (wifiSsid[0] == '\0') return false;

  Serial.print(F("WiFi connecting to: ")); Serial.println(wifiSsid);
  WiFi.mode(WIFI_OFF); delay(300);
  WiFi.mode(WIFI_STA); WiFi.setTxPower(WIFI_POWER_8_5dBm); delay(200);
  WiFi.begin(wifiSsid, wifiPassword);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_TIMEOUT_MS) {
      Serial.println(F(" TIMEOUT"));
      return false;
    }
    delay(400); Serial.print('.'); esp_task_wdt_reset();
  }
  Serial.printf(" OK (%s)\n", WiFi.localIP().toString().c_str());
  return true;
}

// Back-compat wrapper used elsewhere in the file (loop() reconnect, etc).
// Retries up to WIFI_MAX_ATTEMPTS times before giving up.
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  for (int attempt = 1; attempt <= WIFI_MAX_ATTEMPTS; attempt++) {
    char buf[24]; snprintf(buf, sizeof(buf), "Attempt %d/%d", attempt, WIFI_MAX_ATTEMPTS);
    oledShow("Connecting WiFi", wifiSsid, buf);
    if (connectWiFiOnce()) return true;
    esp_task_wdt_reset();
  }
  return false;
}

// ═════════════════════════════════════════════════════════════
//  USB SERIAL WIFI PROVISIONING
//
//  Protocol (line-delimited JSON, 115200 baud, newline-terminated):
//
//    Host -> Device:
//      {"cmd":"wifi","ssid":"MyNetwork","password":"secret123"}
//      {"cmd":"wifi_reset"}                 (forces re-provisioning)
//      {"cmd":"ping"}                       (liveness check)
//
//    Device -> Host:
//      {"status":"ready"}                              on entering provisioning mode
//      {"status":"connecting","ssid":"MyNetwork"}       while attempting
//      {"status":"ok","ip":"192.168.1.42"}              on success
//      {"status":"error","reason":"auth_failed"}        on failure
//      {"status":"error","reason":"timeout"}
//      {"status":"error","reason":"bad_json"}
//      {"status":"pong"}                                reply to ping
//
//  The website's "Connect via USB" button uses the Web Serial API to
//  open this same port and speak this protocol directly from the
//  browser — no native app required.
// ═════════════════════════════════════════════════════════════

// Reads one line from Serial (blocking with WDT feed), returns "" if
// nothing complete arrived within the timeout.
String readSerialLine(unsigned long timeoutMs) {
  static char buf[SERIAL_LINE_MAX];
  size_t len = 0;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    esp_task_wdt_reset();
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n') {
        buf[len] = '\0';
        String result(buf);
        result.trim();
        return result;
      }
      if (c != '\r' && len < SERIAL_LINE_MAX - 1) buf[len++] = c;
    }
    delay(10);
  }
  return String("");
}

void sendStatus(const char* json) {
  Serial.println(json);
}

// Attempts to apply new WiFi creds (up to WIFI_MAX_ATTEMPTS tries),
// reporting progress over Serial as it goes. Returns true on success.
bool tryApplyWifiCreds(const char* ssid, const char* password, char* reasonOut, size_t reasonLen) {
  strncpy(wifiSsid, ssid, sizeof(wifiSsid)-1);             wifiSsid[sizeof(wifiSsid)-1] = '\0';
  strncpy(wifiPassword, password, sizeof(wifiPassword)-1); wifiPassword[sizeof(wifiPassword)-1] = '\0';

  char msg[96];
  snprintf(msg, sizeof(msg), "{\"status\":\"connecting\",\"ssid\":\"%s\"}", ssid);
  sendStatus(msg);
  oledProvisioning("connecting...");

  for (int attempt = 1; attempt <= WIFI_MAX_ATTEMPTS; attempt++) {
    char obuf[40]; snprintf(obuf, sizeof(obuf), "try %d/%d", attempt, WIFI_MAX_ATTEMPTS);
    oledProvisioning(obuf);
    if (connectWiFiOnce()) return true;
    esp_task_wdt_reset();
  }
  strncpy(reasonOut, "auth_failed_or_timeout", reasonLen-1);
  reasonOut[reasonLen-1] = '\0';
  return false;
}

// Blocks here until valid WiFi credentials are received and applied
// successfully. Shows live status on the OLED the whole time.
void wifiProvisioning() {
  Serial.println(F("{\"status\":\"ready\"}"));
  oledProvisioning("waiting for USB...");

  while (true) {
    esp_task_wdt_reset();
    String line = readSerialLine(60000UL);   // 60s read window, loops forever
    if (line.length() == 0) {
      oledProvisioning("waiting for USB...");
      continue;
    }

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
      sendStatus("{\"status\":\"error\",\"reason\":\"bad_json\"}");
      continue;
    }

    const char* cmd = doc["cmd"] | "";

    if (strcmp(cmd, "ping") == 0) {
      sendStatus("{\"status\":\"pong\"}");
      continue;
    }

    if (strcmp(cmd, "wifi") == 0) {
      const char* ssid = doc["ssid"]     | "";
      const char* pass = doc["password"] | "";
      if (ssid[0] == '\0') {
        sendStatus("{\"status\":\"error\",\"reason\":\"missing_ssid\"}");
        continue;
      }
      char reason[40] = "";
      if (tryApplyWifiCreds(ssid, pass, reason, sizeof(reason))) {
        nvsSaveWifiCreds(ssid, pass);
        char ok[64];
        snprintf(ok, sizeof(ok), "{\"status\":\"ok\",\"ip\":\"%s\"}", WiFi.localIP().toString().c_str());
        sendStatus(ok);
        oledProvisioning("connected!");
        confirmBeep();
        delay(800);
        return;   // proceed into normal boot path
      } else {
        char fail[96];
        snprintf(fail, sizeof(fail), "{\"status\":\"error\",\"reason\":\"%s\"}", reason);
        sendStatus(fail);
        oledProvisioning("failed - retry");
        errorBeep();
      }
      continue;
    }

    if (strcmp(cmd, "wifi_reset") == 0) {
      // Already in provisioning mode, but acknowledge anyway.
      sendStatus("{\"status\":\"ready\"}");
      continue;
    }

    sendStatus("{\"status\":\"error\",\"reason\":\"unknown_cmd\"}");
  }
}

// Checked at the top of loop() so a device that's already running can
// be told (over USB) to switch networks without a physical reset.
void checkForWifiResetCommand() {
  if (!Serial.available()) return;
  String line = readSerialLine(200UL);
  if (line.length() == 0) return;

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, line)) return;
  const char* cmd = doc["cmd"] | "";

  if (strcmp(cmd, "wifi_reset") == 0) {
    Serial.println(F("{\"status\":\"ready\"}"));
    nvsClearWifiCreds();
    WiFi.disconnect(true);
    oledProvisioning("waiting for USB...");
    wifiProvisioning();          // blocks until re-provisioned
    // Resuming loop() after this is fine — Firebase session is untouched,
    // event polling will just pick back up on the next interval.
  } else if (strcmp(cmd, "ping") == 0) {
    Serial.println(F("{\"status\":\"pong\"}"));
  }
}

// ═════════════════════════════════════════════════════════════
//  Firebase
// ═════════════════════════════════════════════════════════════

bool firebaseInit() {
  fbConfig.api_key               = FIREBASE_API_KEY;
  fbConfig.database_url          = FIREBASE_DB_URL;
  fbConfig.token_status_callback = tokenStatusCallback;
  if (!fbSignedUp) {
    Serial.println(F("Firebase: signUp"));
    Firebase.signUp(&fbConfig, &fbAuth, "", "");
    fbSignedUp = true;
  } else {
    Serial.println(F("Firebase: resume auth"));
  }
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);
  unsigned long t = millis();
  while (!Firebase.ready()) {
    esp_task_wdt_reset();
    if (millis()-t > 10000) { Serial.println(F("Firebase timeout")); return false; }
    delay(200);
  }
  return true;
}

// ═════════════════════════════════════════════════════════════
//  Pairing flow (UNCHANGED from v5 — account linking only,
//  independent of WiFi provisioning)
// ═════════════════════════════════════════════════════════════

void runPairingFlow() {
  // Use a DEDICATED FirebaseData for pairing — never reused elsewhere
  FirebaseData fbdoPairing;

  char code[8] = "";
  getPairingCode(code, sizeof(code));
  Serial.print(F("Pairing code: ")); Serial.println(code);
  oledPairingCode(code);

  char pairPath[48];
  snprintf(pairPath, sizeof(pairPath), "/pairingCodes/%s", code);
  FirebaseJson json;
  json.set("deviceId", deviceId);
  json.set("createdAt/.sv", "timestamp");
  Firebase.RTDB.setJSON(&fbdoPairing, pairPath, &json);

  char devPath[64];
  snprintf(devPath, sizeof(devPath), "/devices/%s/ownerId", deviceId);

  Serial.println(F("Waiting to be paired..."));
  while (true) {
    esp_task_wdt_reset();
    delay(3000);
    esp_task_wdt_reset();   // feed again after the 3s delay

    if (Firebase.RTDB.getString(&fbdoPairing, devPath)) {
      String uid = fbdoPairing.stringData();
      if (uid.length() > 0 && uid != "null") {

        nvsSaveOwnerUid(uid.c_str());
        clearPairingCode();
        Firebase.RTDB.deleteNode(&fbdoPairing, pairPath);

        Serial.print(F("Paired! UID: ")); Serial.println(uid);
        oledShow("PAIRED!", "Rebooting...", "Please wait");

        esp_task_wdt_reset();
        confirmBeep();
        esp_task_wdt_reset();
        delay(1500);
        esp_task_wdt_reset();

        ESP.restart();
      }
    }
    oledPairingCode(code);
  }
}

// ═════════════════════════════════════════════════════════════
//  Settings
// ═════════════════════════════════════════════════════════════

void readSettings() {
  esp_task_wdt_reset();
  char path[48];
  snprintf(path, sizeof(path), "/devices/%s", deviceId);
  if (Firebase.RTDB.getJSON(&fbdoSettings, path)) {
    FirebaseJson&    j = fbdoSettings.jsonObject();
    FirebaseJsonData d;
    j.get(d, "alertMinutes");  if (d.success) alertMinutes  = d.to<int>();
    j.get(d, "buzzerEnabled"); if (d.success) buzzerEnabled = d.to<bool>();
  }
  Serial.printf("Settings: alert=%d buzz=%d\n", alertMinutes, (int)buzzerEnabled);
  esp_task_wdt_reset();
}

// ═════════════════════════════════════════════════════════════
//  Heartbeat
// ═════════════════════════════════════════════════════════════

void heartbeat() {
  esp_task_wdt_reset();
  char path[48];
  snprintf(path, sizeof(path), "/devices/%s", deviceId);
  FirebaseJson json;
  json.set("lastSeen/.sv", "timestamp");
  json.set("online", true);
  Firebase.RTDB.updateNode(&fbdoSettings, path, &json);
  esp_task_wdt_reset();
}

// ═════════════════════════════════════════════════════════════
//  Formatting helpers
// ═════════════════════════════════════════════════════════════

void formatTime(time_t t, char* out, size_t outLen) {
  struct tm* ti = localtime(&t);
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                          "Jul","Aug","Sep","Oct","Nov","Dec"};
  int h = ti->tm_hour, m = ti->tm_min;
  const char* ap = h >= 12 ? "PM" : "AM";
  if (h > 12) h -= 12; if (h == 0) h = 12;
  snprintf(out, outLen, "%s %d %d:%02d %s",
           months[ti->tm_mon], ti->tm_mday, h, m, ap);
}

void formatTimeOnly(time_t t, char* out, size_t outLen) {
  struct tm* ti = localtime(&t);
  int h = ti->tm_hour, m = ti->tm_min;
  const char* ap = h >= 12 ? "PM" : "AM";
  if (h > 12) h -= 12; if (h == 0) h = 12;
  snprintf(out, outLen, "%d:%02d %s", h, m, ap);
}

void formatDateTime(time_t t, char* out, size_t outLen) {
  struct tm* ti = localtime(&t);
  const char* days[]   = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                          "Jul","Aug","Sep","Oct","Nov","Dec"};
  int h = ti->tm_hour, m = ti->tm_min;
  const char* ap = h >= 12 ? "PM" : "AM";
  if (h > 12) h -= 12; if (h == 0) h = 12;
  snprintf(out, outLen, "%s %s %d  %d:%02d %s",
           days[ti->tm_wday], months[ti->tm_mon], ti->tm_mday, h, m, ap);
}

void formatCountdown(long s, char* out, size_t outLen) {
  if (s <= 0) { strncpy(out, "NOW", outLen-1); out[outLen-1]='\0'; return; }
  long mins = s / 60;
  if (mins < 60) snprintf(out, outLen, "%ldm", mins);
  else           snprintf(out, outLen, "%ldh%ldm", mins/60, mins%60);
}

// ═════════════════════════════════════════════════════════════
//  Event check (UNCHANGED logic from v5)
// ═════════════════════════════════════════════════════════════

void cacheEventDisplay(const char* t1, const char* tm1, const char* l1,
                       const char* t2, const char* tm2,
                       time_t s1, time_t s2, bool isLive, time_t liveEnd);

void checkEvents() {
  esp_task_wdt_reset();
  Serial.println(F(">>> checkEvents() start"));

  if (ownerUid[0] == '\0') {
    Serial.println(F("  ERROR: ownerUid is empty!"));
    oledShow("ERROR", "ownerUid empty", "Re-pair device");
    return;
  }

  if (!Firebase.ready()) {
    Serial.println(F("  Firebase not ready — skipping"));
    oledShow("Firebase", "not ready...");
    return;
  }

  static char path[72];
  snprintf(path, sizeof(path), "/users/%s/events", ownerUid);
  oledShow("Fetching...", path);

  bool ok = Firebase.RTDB.getJSON(&fbdo, path);
  esp_task_wdt_reset();
  if (!ok) {
    Serial.print(F("  Error reason : ")); Serial.println(fbdo.errorReason());
    oledShow("RTDB fetch fail", fbdo.errorReason().c_str());
    return;
  }

  String rawJson = fbdo.jsonString();
  esp_task_wdt_reset();

  static StaticJsonDocument<4096> doc;
  doc.clear();
  DeserializationError err = deserializeJson(doc, rawJson);
  if (err) {
    Serial.print(F("  JSON parse error: ")); Serial.println(err.c_str());
    oledShow("JSON parse error", err.c_str());
    return;
  }

  JsonArray arr = doc["events"].as<JsonArray>();
  int count = (int)arr.size();
  if (count == 0) { oledShow("SmartSchedule AI", "", "No upcoming events"); return; }

  time_t now = time(nullptr);

  bool   liveFound = false;
  time_t liveStart = 0, liveEnd = 0;
  static char liveTitle[64]; liveTitle[0] = '\0';
  static char liveLoc[64];   liveLoc[0]   = '\0';

  int idx = 0;
  for (JsonObject ev : arr) {
    esp_task_wdt_reset();
    long long stMs = ev["startTime"] | (long long)0;
    long long enMs = ev["endTime"]   | (long long)0;
    if (stMs == 0) { idx++; continue; }
    time_t start = (time_t)(stMs / 1000);
    time_t end   = enMs ? (time_t)(enMs / 1000) : 0;
    if (end != 0 && now >= start && now < end) {
      const char* evTitle = ev["title"]    | "Event";
      const char* evLoc   = ev["location"] | "";
      if (!liveFound || start > liveStart) {
        liveFound = true; liveStart = start; liveEnd = end;
        strncpy(liveTitle, evTitle, sizeof(liveTitle)-1); liveTitle[sizeof(liveTitle)-1] = '\0';
        strncpy(liveLoc,   evLoc,   sizeof(liveLoc)-1);   liveLoc[sizeof(liveLoc)-1]     = '\0';
      }
    }
    idx++;
  }

  long   delta1 = LONG_MAX, delta2 = LONG_MAX;
  time_t start1 = 0,        start2 = 0;
  static char title1[64]; title1[0] = '\0';
  static char loc1[64];   loc1[0]   = '\0';
  static char title2[64]; title2[0] = '\0';

  idx = 0;
  for (JsonObject ev : arr) {
    esp_task_wdt_reset();
    long long stMs  = ev["startTime"] | (long long)0;
    const char* evTitle = ev["title"]    | "Event";
    const char* evLoc   = ev["location"] | "";
    if (stMs == 0) { idx++; continue; }
    time_t start = (time_t)(stMs / 1000);
    if (liveFound && start == liveStart && strcmp(evTitle, liveTitle) == 0) {
      idx++; continue;
    }
    long   delta = (long)(start - now);

    if (delta < delta1) {
      delta2 = delta1; start2 = start1;
      strncpy(title2, title1, sizeof(title2)-1); title2[sizeof(title2)-1] = '\0';
      delta1 = delta; start1 = start;
      strncpy(title1, evTitle, sizeof(title1)-1); title1[sizeof(title1)-1] = '\0';
      strncpy(loc1,   evLoc,   sizeof(loc1)-1);   loc1[sizeof(loc1)-1]     = '\0';
    } else if (delta < delta2) {
      delta2 = delta; start2 = start;
      strncpy(title2, evTitle, sizeof(title2)-1); title2[sizeof(title2)-1] = '\0';
    }
    idx++;
  }

  bool isLive = liveFound;
  if (liveFound) {
    strncpy(title2, title1, sizeof(title2)-1); title2[sizeof(title2)-1] = '\0';
    start2 = start1;
    strncpy(title1, liveTitle, sizeof(title1)-1); title1[sizeof(title1)-1] = '\0';
    strncpy(loc1,   liveLoc,   sizeof(loc1)-1);   loc1[sizeof(loc1)-1]     = '\0';
    start1 = liveStart;
    delta1 = (long)(liveEnd - now);
  }

  if (start1 == 0) { oledShow("SmartSchedule AI", "", "No upcoming events"); return; }

  char time1str[20], countdown1[10];
  if (isLive) {
    char endStr[12];
    formatTimeOnly(liveEnd, endStr, sizeof(endStr));
    snprintf(time1str, sizeof(time1str), "Now - Ends %s", endStr);
    formatCountdown(delta1, countdown1, sizeof(countdown1));
  } else {
    formatTime(start1, time1str, sizeof(time1str));
    formatCountdown(delta1, countdown1, sizeof(countdown1));
  }
  char time2str[20] = "";
  if (start2 != 0) formatTime(start2, time2str, sizeof(time2str));
  char time2compact[10] = "";
  if (start2 != 0) formatTimeOnly(start2, time2compact, sizeof(time2compact));

  bool isAlert = (!isLive && delta1 >= 0 && delta1 <= (long)alertMinutes * 60);
  bool isNew   = (strcmp(title1, lastAlertedTitle) != 0);

  cacheEventDisplay(title1, time1str, loc1,
                    start2 ? title2 : "", start2 ? time2compact : "",
                    start1, start2, isLive, liveEnd);

  oledTwoEvents(title1, time1str, loc1, countdown1, isAlert,
                start2 ? title2 : "", start2 ? time2compact : "");

  if (isAlert && buzzerEnabled && isNew) {
    alertBuzz();
    strncpy(lastAlertedTitle, title1, sizeof(lastAlertedTitle)-1);
    alertedLast = true;
  } else if (!isAlert && alertedLast) {
    alertedLast = false; lastAlertedTitle[0] = '\0';
  }
  esp_task_wdt_reset();
}

// ═════════════════════════════════════════════════════════════
//  setup()
// ═════════════════════════════════════════════════════════════

void setup() {
  esp_task_wdt_add(NULL);

  Serial.begin(115200); delay(500);
  Serial.println(F("\n=== SmartSchedule AI v6 ==="));
  pinMode(BUZZER_PIN, OUTPUT);
  oledInit();

  oledShow("SmartSchedule AI", "v6", "", "Starting...");
  delay(600);

  ensureDeviceId();

  // ── WiFi: load saved creds, or go straight to provisioning ──
  bool haveCreds = nvsLoadWifiCreds();
  bool wifiOk = false;

  if (haveCreds) {
    Serial.print(F("Saved SSID: ")); Serial.println(wifiSsid);
    wifiOk = connectWiFi();   // tries WIFI_MAX_ATTEMPTS times internally
  }

  if (!wifiOk) {
    Serial.println(F("No working WiFi — entering provisioning mode"));
    wifiProvisioning();       // blocks until credentials work, then returns
  }

  ensureDeviceId();   // deviceId depends on MAC, which is stable post-connect too

  oledShow("WiFi OK", WiFi.localIP().toString().c_str(), "", "Firebase...");
  if (!firebaseInit()) {
    oledShow("Firebase failed", "", "Restarting...");
    delay(5000); ESP.restart(); return;
  }
  confirmBeep(); esp_task_wdt_reset();

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
  tzset();
  oledShow("Syncing time...");
  time_t t = 0; unsigned long ts = millis();
  while (t < 1000000 && millis()-ts < 8000) { esp_task_wdt_reset(); time(&t); delay(200); }
  Serial.printf("NTP: %ld\n", (long)t);

  bool alreadyPaired = nvsLoadOwnerUid();

  if (!alreadyPaired) {
    runPairingFlow();   // calls ESP.restart() internally on success
    // Never reaches here
  }

  Serial.println(F("=== Post-pair state ==="));
  Serial.print(F("  ownerUid : '")); Serial.print(ownerUid); Serial.println("'");
  Serial.print(F("  deviceId : '")); Serial.print(deviceId); Serial.println("'");

  readSettings();   esp_task_wdt_reset();
  heartbeat();      esp_task_wdt_reset();
  checkEvents();    esp_task_wdt_reset();
  lastPollMs      = millis();
  lastHeartbeatMs = millis();
}

// ═════════════════════════════════════════════════════════════
//  Cached display state (UNCHANGED from v5)
// ═════════════════════════════════════════════════════════════
static char cached_title1[64]     = "";
static char cached_time1[20]      = "";
static char cached_loc1[64]       = "";
static char cached_title2[64]     = "";
static char cached_time2[10]      = "";
static bool cached_hasEvents      = false;
static time_t cached_start1       = 0;
static time_t cached_start2       = 0;
static bool   cached_isLive       = false;
static time_t cached_liveEnd      = 0;

void cacheEventDisplay(const char* t1, const char* tm1, const char* l1,
                       const char* t2, const char* tm2,
                       time_t s1, time_t s2, bool isLive, time_t liveEnd) {
  strncpy(cached_title1, t1,  sizeof(cached_title1)-1);
  strncpy(cached_time1,  tm1, sizeof(cached_time1)-1);
  strncpy(cached_loc1,   l1,  sizeof(cached_loc1)-1);
  strncpy(cached_title2, t2,  sizeof(cached_title2)-1);
  strncpy(cached_time2,  tm2, sizeof(cached_time2)-1);
  cached_start1    = s1;
  cached_start2    = s2;
  cached_isLive    = isLive;
  cached_liveEnd   = liveEnd;
  cached_hasEvents = (s1 != 0);
}

void redrawDisplay() {
  if (!cached_hasEvents) return;
  time_t now = time(nullptr);
  long delta1 = cached_isLive ? (long)(cached_liveEnd - now)
                              : (long)(cached_start1 - now);
  char cd[10]; formatCountdown(delta1, cd, sizeof(cd));
  oledTwoEvents(cached_title1, cached_time1, cached_loc1, cd, cached_isLive,
                cached_title2, cached_time2);
}

// ═════════════════════════════════════════════════════════════
//  loop()
// ═════════════════════════════════════════════════════════════

unsigned long lastClockMs = 0;

void loop() {
  esp_task_wdt_reset();
  unsigned long now = millis();

  // Allow re-provisioning over USB at any time without a physical reset
  checkForWifiResetCommand();

  // Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    oledShow("WiFi lost", "Reconnecting...");
    if (!connectWiFi()) {
      // Saved creds no longer work (e.g. router password changed) —
      // drop into provisioning rather than retrying forever.
      Serial.println(F("WiFi reconnect failed 3x — entering provisioning"));
      wifiProvisioning();
    }
    if (!Firebase.ready()) firebaseInit();
  }

  if (now - lastPollMs >= POLL_INTERVAL_MS) {
    checkEvents();
    lastPollMs = millis();
  }

  if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    heartbeat();
    lastHeartbeatMs = millis();
  }

  if (now - lastClockMs >= 1000) {
    redrawDisplay();
    lastClockMs = millis();
  }

  delay(100);
}
