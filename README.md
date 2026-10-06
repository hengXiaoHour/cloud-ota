# ESP32 Cloud OTA with GitHub as Cloud ☁️ (arduino-cli)

ESP32 downloads `firmware.bin` from **GitHub Releases** and flashes itself. No server.

```
ESP32 (boot/every 30s) -> GET https://raw.githubusercontent.com/USER/REPO/main/version.json
                       <- {"version":"1.0.1","bin_url":"https://github.com/.../firmware.bin"}
                       if remote > FW_VERSION -> GET bin -> flash -> reboot
```

### 1. Install (one time)

```bash
# arduino-cli
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
sudo mv bin/arduino-cli /usr/local/bin/

arduino-cli core update-index
arduino-cli core install esp32:esp32
arduino-cli lib install ArduinoJson
```

### 2. Configure

```bash
cp config.example.h config.h
python3 setup.py   # fills WiFi + VERSION_URL into config.h (gitignored, never pushed)
```
`config.h` is local-only. CI builds its own from GitHub Secrets `WIFI_SSID` / `WIFI_PASSWORD` (repo Settings -> Secrets -> Actions).
Edit `version.json` -> replace `YOUR_USERNAME/YOUR_REPO`.

### 3. First Flash (USB needed once)

```bash
# find port
arduino-cli board list

# compile + upload
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 .

# monitor
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
# type 'ota' + enter to force check, 'version' to show FW
```

### 4. OTA Update (no USB)

**Auto (recommended):**
```bash
git add .
git commit -m "1.0.1"
git tag v1.0.1
git push origin main --tags
# GitHub Action builds .bin, updates version.json, creates Release
# ESP32 auto-flashes in ~30s
```

**Manual:**
1. `arduino-cli compile --fqbn esp32:esp32:esp32 --output-dir ./build .`
2. GitHub -> Releases -> Draft `v1.0.1` -> upload `build/cloud-ota.ino.bin` as `firmware.bin`
3. Update `version.json`:
```json
{"version":"1.0.1","bin_url":"https://github.com/YOUR_USERNAME/YOUR_REPO/releases/download/v1.0.1/firmware.bin"}
```
4. `git add version.json && git commit -m "1.0.1" && git push`

### Files
```
cloud-ota/
├── cloud-ota.ino               # sketch (main code)
├── config.h                    # LOCAL ONLY, gitignored (copy from config.example.h)
├── config.example.h            # tracked template (placeholders only)
├── version.json                # polled by ESP32
└── .github/workflows/build-and-release.yml  # arduino-cli build on tag
```

### Gotchas

- Repo must be **PUBLIC** (private needs token + `http.addHeader("Authorization","token ghp_xxx")`)
- `HTTPC_STRICT_FOLLOW_REDIRECTS` required — GitHub releases 302 to AWS
- `setInsecure()` skips cert check (fine for hobby). Prod: add CA via `setCACert()`
- Must use OTA partition: default `esp32:esp32:esp32` already has 2x OTA slots
- Bump `FW_VERSION` on every release or ESP32 will ignore it
- Change `OTA_CHECK_INTERVAL` to `3600000` (1h) for prod

### Compile check without hardware
```bash
arduino-cli compile --fqbn esp32:esp32:esp32 .
```
