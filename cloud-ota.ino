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

bool checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return false;
  }

  Serial.printf("\n[OTA] Checking %s\n", VERSION_URL);
  Serial.printf("[OTA] Current FW: %s\n", FW_VERSION);

  WiFiClientSecure *client = new WiFiClientSecure();
  if (USE_INSECURE) {
    client->setInsecure();
  }

  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(*client, VERSION_URL)) {
    Serial.println("[OTA] http.begin failed");
    delete client;
    return false;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf("[OTA] version.json GET failed: %d %s\n", code, http.errorToString(code).c_str());
    http.end();
    delete client;
    return false;
  }

  String payload = http.getString();
  http.end();
  delete client;

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
  Serial.println("[OTA] Starting flash... DO NOT POWER OFF");

  if (LED_PIN >= 0) pinMode(LED_PIN, OUTPUT);

  WiFiClientSecure *otaClient = new WiFiClientSecure();
  if (USE_INSECURE) otaClient->setInsecure();
  otaClient->setTimeout(30);

  httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  httpUpdate.setLedPin(LED_PIN, LOW);
  httpUpdate.rebootOnUpdate(false);

  t_httpUpdate_return ret = httpUpdate.update(*otaClient, binUrl);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[OTA] FAILED Error %d: %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
      delete otaClient;
      return false;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[OTA] No updates");
      delete otaClient;
      return false;
    case HTTP_UPDATE_OK:
      Serial.println("[OTA] SUCCESS - Rebooting in 2s");
      delay(2000);
      ESP.restart();
      break;
  }
  delete otaClient;
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
  checkForUpdate();
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
    checkForUpdate();
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "ota") checkForUpdate();
    if (cmd == "version") Serial.printf("FW: %s\n", FW_VERSION);
  }
}
