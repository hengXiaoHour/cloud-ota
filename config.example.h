#pragma once

// WiFi credentials are NOT stored here — they live in NVS flash on the board,
// provisioned once over serial with:  setwifi <ssid> <password>
// Copy this file to config.h and set VERSION_URL below. No secrets in either file.

// Raw URL to version.json on main branch (ALWAYS points to latest)
// Get this from GitHub: open version.json -> Raw -> copy URL
#define VERSION_URL     "https://raw.githubusercontent.com/YOUR_USERNAME/YOUR_REPO/main/version.json"

// How often to auto-check for update (ms) — only notifies if AUTO_OTA false
#define OTA_CHECK_INTERVAL  10000

// If true, periodic check auto-flashes. If false, it only NOTIFIES and needs "update"
#define AUTO_OTA            false

// Current firmware version - BUMP THIS ON EVERY RELEASE
#define FW_VERSION      "0.0.0"

// Set to true to skip cert validation (easiest, works behind GitHub CDN)
// For production use set to false and add root CA
#define USE_INSECURE    true

// LED pin for status (2 = built-in on many devkits, set -1 to disable)
#define LED_PIN         2
