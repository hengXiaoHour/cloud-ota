# cloud-ota

ESP32 pulls `firmware.bin` from GitHub Releases. No server.

## Setup (once)

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install ArduinoJson
cp config.example.h config.h
python3 setup.py
arduino-cli compile --fqbn esp32:esp32:esp32 .
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 .
```

Then in the serial monitor, type:

```
setwifi <ssid> <password>
```

Board reboots and connects. WiFi lives in NVS, never in source or `firmware.bin`.

## OTA

```bash
python3 build.py   # enter new version -> builds + creates Release
```

Board checks every 30s (silent unless there's something new). New release prints an alert -> type `update` in serial. Done.

## Serial commands

```
update                          # check GitHub + flash now
version                         # show firmware version
status                          # toggle loop/OTA chatter on/off
wifi                            # show SSID + connection (password never printed)
setwifi <ssid> <password>       # save WiFi to NVS (password = last word)
clearwifi                       # erase WiFi + reboot to provisioning
```

Repo must be PUBLIC or raw/release URLs need a token.
