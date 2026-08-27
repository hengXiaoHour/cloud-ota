#pragma once

// ========= EDIT THESE =========
#define WIFI_SSID       "Ee Ourng"
#define WIFI_PASSWORD   "45616438"

// Raw URL to version.json on main branch (ALWAYS points to latest)
// Get this from GitHub: open version.json -> Raw -> copy URL
#define VERSION_URL     "https://raw.githubusercontent.com/hengXiaoHour/cloud-ota/main/version.json"

// How often to check for update (ms)
#define OTA_CHECK_INTERVAL  3000

// Current firmware version - BUMP THIS ON EVERY RELEASE
#define FW_VERSION      "0.0.1"

// Set to true to skip cert validation (easiest, works behind GitHub CDN)
// For production use set to false and add root CA
#define USE_INSECURE    true

// LED pin for status (2 = built-in on many devkits, set -1 to disable)
#define LED_PIN         4
