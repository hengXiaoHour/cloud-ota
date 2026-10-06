#!/usr/bin/env python3
"""
setup.py — interactive wizard to configure cloud-ota (arduino-cli)
Steps through every field you need to modify in config.h
"""
import re
import pathlib
import sys

ROOT = pathlib.Path(__file__).parent
CONFIG = ROOT / "config.h"
EXAMPLE = ROOT / "config.example.h"

def read_config():
    text = CONFIG.read_text()
    def get(pattern, default=""):
        m = re.search(pattern, text, re.M)
        return m.group(1) if m else default
    return {
        "VERSION_URL": get(r'#define\s+VERSION_URL\s+"([^"]*)"'),
        "FW_VERSION": get(r'#define\s+FW_VERSION\s+"([^"]*)"'),
        "OTA_CHECK_INTERVAL": get(r'#define\s+OTA_CHECK_INTERVAL\s+(\d+)'),
        "USE_INSECURE": get(r'#define\s+USE_INSECURE\s+(\w+)'),
        "LED_PIN": get(r'#define\s+LED_PIN\s+(-?\d+)'),
    }

def ask(prompt, default, hide=False):
    if hide and default:
        shown = default[:2] + "****" if len(default) > 4 else "****"
    else:
        shown = default
    raw = input(f"{prompt} [{shown}]: ").strip()
    return raw if raw != "" else default

def bump_patch(v):
    try:
        a,b,c = [int(x) for x in v.split(".")]
        return f"{a}.{b}.{c+1}"
    except:
        return v

def main():
    if EXAMPLE.exists() and not CONFIG.exists():
        CONFIG.write_text(EXAMPLE.read_text())
        print(f"Created {CONFIG} from {EXAMPLE.name} — run setup.py to configure.")
    if not CONFIG.exists():
        print(f"ERROR: {CONFIG} not found (and no {EXAMPLE.name} to copy)")
        sys.exit(1)

    cur = read_config()
    print("=== Cloud OTA Setup Wizard (arduino-cli) ===")
    print(f"Editing: {CONFIG}\nLeave empty to keep current value.")
    print("WiFi is NOT configured here — it lives in NVS on the board (serial: setwifi).\n")

    url = ask("1/5 VERSION_URL (raw GitHub URL to version.json)", cur["VERSION_URL"])
    fw = ask("2/5 FW_VERSION (e.g. 1.0.1)", cur["FW_VERSION"])
    interval = ask("3/5 OTA_CHECK_INTERVAL ms (30000=30s, 3600000=1h)", cur["OTA_CHECK_INTERVAL"])
    insecure = ask("4/5 USE_INSECURE (true/false, true=skip cert check)", cur["USE_INSECURE"])
    led = ask("5/5 LED_PIN (-1 to disable, 2=built-in)", cur["LED_PIN"])

    # validation
    if not re.match(r'^\d+\.\d+\.\d+$', fw):
        print(f"WARNING: FW_VERSION '{fw}' not in x.y.z format")
    if url and "raw.githubusercontent.com" not in url:
        print(f"WARNING: VERSION_URL doesn't look like raw.githubusercontent.com: {url}")

    text = CONFIG.read_text()
    replacements = {
        r'#define\s+VERSION_URL\s+"[^"]*"': f'#define VERSION_URL     "{url}"',
        r'#define\s+FW_VERSION\s+"[^"]*"': f'#define FW_VERSION      "{fw}"',
        r'#define\s+OTA_CHECK_INTERVAL\s+\d+': f'#define OTA_CHECK_INTERVAL  {interval}',
        r'#define\s+USE_INSECURE\s+\w+': f'#define USE_INSECURE    {insecure}',
        r'#define\s+LED_PIN\s+-?\d+': f'#define LED_PIN         {led}',
    }
    for pat, repl in replacements.items():
        text = re.sub(pat, repl, text)

    CONFIG.write_text(text)
    print("\n✓ config.h updated:")
    print(CONFIG.read_text())
    print("\nNext:")
    print("  python3 setup.py        # re-run to edit again")
    print("  python3 build.py        # build .bin + push to GitHub (OTA)")
    print("  python3 upload.py       # manual USB flash")
    print("  Serial -> type /update  # trigger OTA from GitHub")

if __name__ == "__main__":
    main()
