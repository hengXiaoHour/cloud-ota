#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include "config.h"

unsigned long lastCheck = 0;

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

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] Connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] FAILED - will retry");
  }
}

bool checkForUpdate(bool doInstall = true) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;
  }

  Serial.printf("\n[OTA] Checking %s\n", VERSION_URL);
  Serial.printf("[OTA] Current FW: %s\n", FW_VERSION);

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
    Serial.printf("[OTA] Fetching (cache-bust) %s\n", url.c_str());

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

  Serial.printf("[OTA] version.json: %s\n", payload.c_str());

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
    Serial.printf("[OTA] Already on latest (%s >= %s)\n", FW_VERSION, latest.c_str());
    return false;
  }

  Serial.printf("[OTA] NEW VERSION! %s -> %s\n", FW_VERSION, latest.c_str());
  Serial.printf("[OTA] Bin URL: %s\n", binUrl.c_str());
  if (!doInstall) {
    Serial.println("[OTA] >>> New version available! Type /update to flash <<<");
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

  connectWiFi();
  // On boot: only check, don't auto-flash if AUTO_OTA=false — user must send /update
  checkForUpdate(AUTO_OTA);
  lastCheck = millis();
}

void loop() {
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    lastBlink = millis();
    Serial.printf("[Loop] FW %s running, WiFi %s, heap %d\n",
      FW_VERSION,
      WiFi.status()==WL_CONNECTED?"OK":"DISC",
      ESP.getFreeHeap());
    if (LED_PIN >= 0) digitalWrite(LED_PIN, !digitalRead(LED_PIN));
  }

  if (millis() - lastCheck > OTA_CHECK_INTERVAL) {
    lastCheck = millis();
    // Periodic check respects AUTO_OTA: false = notify only, true = auto-flash
    checkForUpdate(AUTO_OTA);
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "ota" || cmd == "/update" || cmd == "update") {
      Serial.println("[CMD] /update triggered -> checking GitHub + flashing");
      checkForUpdate(true); // manual always installs
    } else if (cmd == "version" || cmd == "/version") {
      Serial.printf("FW: %s\n", FW_VERSION);
    } else if (cmd.length() > 0) {
      Serial.printf("[CMD] Unknown '%s' | try: /update, version\n", cmd.c_str());
    }
  }
}
