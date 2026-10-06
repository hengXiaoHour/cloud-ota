#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"

// WiFi credentials live in NVS flash (namespace "cloud-ota"),
// provisioned once over serial with:  setwifi <ssid> <password>
// Nothing secret stays in config.h, git history, or firmware.bin.
#define NVS_NS   "cloud-ota"
#define NVS_SSID "ssid"
#define NVS_PASS "pass"

Preferences prefs;
String gSsid = "";
String gPass = "";
unsigned long lastCheck = 0;
bool gShowStatus = false; // serial `status` toggles the [Loop] line on/off

// Simple semantic version compare: returns -1 if a<b, 0 if equal, 1 if a>b
int compareVersion(String a, String b) {
  a.trim(); b.trim();
  int ai=0, bi=0, apos=0, bpos=0;
  while (apos < (int)a.length() || bpos < (int)b.length()) {
    int aEnd = a.indexOf('.', apos);
    int bEnd = b.indexOf('.', bpos);
    if (aEnd == -1) aEnd = a.length();
    if (bEnd == -1) bEnd = b.length();
    String aPart = a.substring(apos, aEnd);
    String bPart = b.substring(bpos, bEnd);
    ai = aPart.toInt();
    bi = bPart.toInt();
    if (ai < bi) return -1;
    if (ai > bi) return 1;
    apos = aEnd + 1;
    bpos = bEnd + 1;
    if (apos > (int)a.length()) apos = a.length();
    if (bpos > (int)b.length()) bpos = b.length();
  }
  return 0;
}

// Load credentials from NVS into gSsid/gPass. True if usable.
bool loadWiFi() {
  gSsid = prefs.getString(NVS_SSID, "");
  gPass = prefs.getString(NVS_PASS, "");
  gSsid.trim();
  return gSsid.length() > 0 && gPass.length() > 0;
}

void saveWiFi(const String& ssid, const String& pass) {
  prefs.putString(NVS_SSID, ssid);
  prefs.putString(NVS_PASS, pass);
  gSsid = ssid;
  gPass = pass;
}

void clearWiFi() {
  prefs.remove(NVS_SSID);
  prefs.remove(NVS_PASS);
  gSsid = "";
  gPass = "";
}

void printProvisionHelp() {
  Serial.println("[WiFi] No credentials in NVS.");
  Serial.println("[WiFi] Provision over serial: setwifi <ssid> <password>");
  Serial.println("[WiFi] (password = last word, so the SSID may contain spaces)");
}

void connectWiFi() {
  if (gSsid.length() == 0) {
    printProvisionHelp();
    return;
  }
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] Connecting to %s\n", gSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(gSsid.c_str(), gPass.c_str());
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] FAILED - check SSID/password, re-run setwifi");
  }
}

bool checkForUpdate(bool doInstall = true, bool verbose = false) {
  if (gSsid.length() == 0) {
    printProvisionHelp();
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;
  }

  // Routine chatter only for an explicit ask (manual update command, boot check).
  // The periodic check stays fully silent when the version is unchanged —
  // even in status mode — and only speaks when a new release is available.
  // New-version alerts, flash progress, and errors always print.
  bool v = verbose;

  if (v) Serial.printf("\n[OTA] Checking %s\n", VERSION_URL);
  if (v) Serial.printf("[OTA] Current FW: %s\n", FW_VERSION);

  String payload;
  {
    WiFiClientSecure client;
    if (USE_INSECURE) {
      client.setInsecure();
    }
    client.setTimeout(15000);

    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // Cache bust: avoid GitHub raw CDN stale (5min)
    String url = String(VERSION_URL) + "?t=" + String(millis());
    if (v) Serial.printf("[OTA] Fetching (cache-bust) %s\n", url.c_str());

    if (!http.begin(client, url)) {
      Serial.println("[OTA] http.begin failed");
      return false;
    }
    http.addHeader("Cache-Control", "no-cache");
    http.addHeader("Pragma", "no-cache");

    int code = http.GET();
    if (code != 200) {
      Serial.printf("[OTA] version.json GET failed: %d %s\n", code, http.errorToString(code).c_str());
      http.end();
      return false;
    }

    payload = http.getString();
    http.end();
  } // http & client destroyed here in correct order

  if (v) Serial.printf("[OTA] version.json: %s\n", payload.c_str());

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("[OTA] JSON parse failed: %s\n", err.c_str());
    return false;
  }

  String latest = doc["version"] | "";
  String binUrl  = doc["bin_url"] | "";
  if (latest == "" || binUrl == "") {
    Serial.println("[OTA] version.json missing version/bin_url");
    return false;
  }

  int cmp = compareVersion(FW_VERSION, latest);
  if (cmp >= 0) {
    if (v) Serial.printf("[OTA] Already on latest (%s >= %s)\n", FW_VERSION, latest.c_str());
    return false;
  }

  Serial.printf("[OTA] NEW VERSION! %s -> %s\n", FW_VERSION, latest.c_str());
  Serial.printf("[OTA] Bin URL: %s\n", binUrl.c_str());
  if (!doInstall) {
    Serial.println("[OTA] >>> New version available! Type update to flash <<<");
    Serial.printf("[OTA] Current: %s | Available: %s\n", FW_VERSION, latest.c_str());
    return false;
  }
  Serial.println("[OTA] Starting flash... DO NOT POWER OFF");

  if (LED_PIN >= 0) pinMode(LED_PIN, OUTPUT);

  {
    WiFiClientSecure otaClient;
    if (USE_INSECURE) otaClient.setInsecure();
    otaClient.setTimeout(30);

    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    httpUpdate.setLedPin(LED_PIN, LOW);
    httpUpdate.rebootOnUpdate(false);

    t_httpUpdate_return ret = httpUpdate.update(otaClient, binUrl);

    switch (ret) {
      case HTTP_UPDATE_FAILED:
        Serial.printf("[OTA] FAILED Error %d: %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        return false;
      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("[OTA] No updates");
        return false;
      case HTTP_UPDATE_OK:
        Serial.println("[OTA] SUCCESS - Rebooting in 2s");
        delay(2000);
        ESP.restart();
        break;
    }
  }
  return true;
}

// NOTE: the raw line is kept case-intact (SSID/password are case-sensitive).
// Only a lowercase copy is used to match the command word.
void handleCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;
  String lower = cmd;
  lower.toLowerCase();

  if (lower == "ota" || lower == "update") {
    Serial.println("[CMD] update triggered -> checking GitHub + flashing");
    checkForUpdate(true, true); // manual always installs, always verbose
  } else if (lower == "version") {
    Serial.printf("FW: %s\n", FW_VERSION);
  } else if (lower == "status") {
    gShowStatus = !gShowStatus;
    Serial.printf("[CMD] Status line %s\n", gShowStatus ? "ON" : "OFF");
  } else if (lower == "wifi" || lower == "showwifi") {
    if (gSsid.length() == 0) {
      printProvisionHelp();
    } else {
      // SSID is shown, password NEVER is (only its length).
      Serial.printf("[WiFi] SSID \"%s\", password set (%d chars), status %s\n",
        gSsid.c_str(), gPass.length(),
        WiFi.status() == WL_CONNECTED ? "connected" : "not connected");
    }
  } else if (lower == "clearwifi") {
    clearWiFi();
    Serial.println("[WiFi] Credentials erased from NVS. Rebooting to provisioning...");
    delay(1000);
    ESP.restart();
  } else if (lower == "setwifi" || lower.startsWith("setwifi ")) {
    // Password = last word, SSID = everything between command and password,
    // so SSIDs with spaces (e.g. "Ee Ourng secret123") work.
    int lastSp = cmd.lastIndexOf(' ');
    String ssid = (lastSp > 8) ? cmd.substring(8, lastSp) : "";
    String pass = (lastSp > 8) ? cmd.substring(lastSp + 1) : "";
    ssid.trim();
    pass.trim();
    if (ssid.length() == 0 || pass.length() == 0) {
      Serial.println("[CMD] Usage: setwifi <ssid> <password>  (password is the last word)");
    } else {
      saveWiFi(ssid, pass);
      Serial.printf("[CMD] Saved SSID \"%s\" (password %d chars, not shown). Rebooting to connect...\n",
        ssid.c_str(), pass.length());
      delay(1000);
      ESP.restart();
    }
  } else {
    Serial.printf("[CMD] Unknown '%s' | try: update, version, status, wifi, setwifi, clearwifi\n", cmd.c_str());
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n=== Cloud OTA GitHub (arduino-cli) ===");
  Serial.printf("FW: %s | Board: esp32dev\n", FW_VERSION);
  Serial.printf("Version URL: %s\n", VERSION_URL);

  if (LED_PIN >= 0) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
  }

  prefs.begin(NVS_NS, false);
  if (!loadWiFi()) {
    printProvisionHelp();
  } else {
    Serial.printf("[WiFi] Loaded SSID \"%s\" from NVS (password hidden)\n", gSsid.c_str());
    connectWiFi();
    // On boot: only check, don't auto-flash if AUTO_OTA=false — user must send update
    // Boot check answers once so you see the system works; periodic stays silent.
    checkForUpdate(AUTO_OTA, true);
  }
  lastCheck = millis();
}

void loop() {
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    lastBlink = millis();
    if (gShowStatus) {
      Serial.printf("[Loop] FW %s running, WiFi %s, heap %d\n",
        FW_VERSION,
        WiFi.status()==WL_CONNECTED?"OK":"DISC",
        ESP.getFreeHeap());
    }
    if (LED_PIN >= 0) digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }

  if (gSsid.length() > 0 && millis() - lastCheck > OTA_CHECK_INTERVAL) {
    lastCheck = millis();
    // Periodic check respects AUTO_OTA: false = notify only, true = auto-flash.
    // Silent when already on latest (both modes); new releases always alert.
    checkForUpdate(AUTO_OTA, false);
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleCommand(cmd);
  }
}
