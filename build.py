#!/usr/bin/env python3
"""
build.py — build .ino -> .bin with arduino-cli and push to GitHub for OTA
Default: local build + instant gh release (no 2min Actions wait)
Use --use-actions to push tag and let GitHub Actions build instead
"""
import re, pathlib, subprocess, sys, json, shutil, argparse

ROOT = pathlib.Path(__file__).parent
CONFIG = ROOT / "config.h"
VERSION_JSON = ROOT / "version.json"
FQBN = "esp32:esp32:esp32"

def run(cmd, cwd=ROOT, check=True):
    print(f"$ {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if check and result.returncode != 0:
        sys.exit(result.returncode)
    return result

def get_version():
    m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', CONFIG.read_text())
    return m.group(1) if m else "0.0.0"

def set_version(v):
    text = CONFIG.read_text()
    text = re.sub(r'#define\s+FW_VERSION\s+"[^"]+"', f'#define FW_VERSION      "{v}"', text)
    CONFIG.write_text(text)

def bump_patch(v):
    try:
        a,b,c = [int(x) for x in v.split(".")]
        return f"{a}.{b}.{c+1}"
    except:
        return v

def find_bin():
    build_dir = ROOT / "build"
    # Prefer exact cloud-ota.ino.bin, never merged/bootloader/partitions
    exact = build_dir / "cloud-ota.ino.bin"
    if exact.exists():
        return exact
    # fallback: any .bin excluding merged/bootloader/partitions
    cands = [p for p in build_dir.glob("*.bin") if "merged" not in p.name and "bootloader" not in p.name and "partitions" not in p.name]
    if cands:
        return cands[0]
    # last resort
    all_cands = list(build_dir.glob("*.bin"))
    return all_cands[0] if all_cands else None

def main():
    parser = argparse.ArgumentParser(description="Build + push OTA")
    parser.add_argument("--version", help="New version x.y.z (default: bump patch)")
    parser.add_argument("--fqbn", default=FQBN, help="FQBN")
    parser.add_argument("--use-actions", action="store_true", help="Push tag and let GitHub Actions build (2min wait) instead of instant local release")
    parser.add_argument("--skip-push", action="store_true", help="Build only, don't git push/release")
    args = parser.parse_args()

    if not CONFIG.exists():
        print(f"ERROR: {CONFIG} missing. Run setup.py first.")
        sys.exit(1)
    if shutil.which("arduino-cli") is None:
        print("ERROR: arduino-cli not found.")
        sys.exit(1)

    cur = get_version()
    suggested = bump_patch(cur)
    new_ver = args.version or input(f"Current FW_VERSION={cur} -> New version [{suggested}]: ").strip() or suggested
    if not re.match(r'^\d+\.\d+\.\d+$', new_ver):
        print(f"ERROR: version must be x.y.z, got '{new_ver}'")
        sys.exit(1)

    print(f"\n=== Building {cur} -> {new_ver} ===")
    set_version(new_ver)
    print(f"✓ config.h FW_VERSION={new_ver}")

    print(f"\n[1/3] Compiling with arduino-cli ({args.fqbn}) ...")
    run(["arduino-cli", "compile", "--fqbn", args.fqbn, "--output-dir", "./build", "."])

    bin_path = find_bin()
    if not bin_path or not bin_path.exists():
        print(f"ERROR: .bin not found in ./build.")
        sys.exit(1)
    print(f"✓ bin: {bin_path} ({bin_path.stat().st_size} bytes) - correct (not merged)")

    fw_bin = ROOT / "firmware.bin"
    shutil.copy(bin_path, fw_bin)
    print(f"✓ copied to {fw_bin}")

    print(f"\n[2/3] Updating version.json ...")
    try:
        remote = subprocess.check_output(["git", "remote", "get-url", "origin"], text=True).strip()
        m = re.search(r'github\.com[:/](.+?)/(.+?)(\.git)?$', remote)
        repo = f"{m.group(1)}/{m.group(2)}" if m else "YOUR_USERNAME/YOUR_REPO"
    except:
        repo = "hengXiaoHour/cloud-ota"
    data = {
        "version": new_ver,
        "bin_url": f"https://github.com/{repo}/releases/download/v{new_ver}/firmware.bin",
        "notes": f"OTA {new_ver} - manual /update"
    }
    VERSION_JSON.write_text(json.dumps(data, indent=2) + "\n")
    print(VERSION_JSON.read_text())

    if args.skip_push:
        print("\n--skip-push: stopped before git. Push manually when ready.")
        return

    if args.use_actions:
        print(f"\n[3/3] Git commit/tag/push -> Actions will build release (2min) ...")
        run(["git", "add", "config.h", "version.json"])
        subprocess.run(["git", "commit", "-m", f"chore: bump OTA to {new_ver}"], cwd=ROOT)
        run(["git", "tag", f"v{new_ver}"], check=False)
        run(["git", "push", "origin", "main"])
        run(["git", "push", "origin", f"v{new_ver}"])
        print(f"\n✓ Pushed tag v{new_ver}. Watch https://github.com/{repo}/actions")
        return

    print(f"\n[3/3] Git commit + instant gh release (no wait) ...")
    run(["git", "add", "config.h", "version.json"])
    subprocess.run(["git", "commit", "-m", f"chore: bump OTA to {new_ver}"], cwd=ROOT)
    run(["git", "push", "origin", "main"])
    if shutil.which("gh") is None:
        print("ERROR: gh CLI not found, falling back to tag push")
        run(["git", "tag", f"v{new_ver}"], check=False)
        run(["git", "push", "origin", f"v{new_ver}"])
        sys.exit(1)
    subprocess.run(["git", "tag", "-d", f"v{new_ver}"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Creating release v{new_ver} with {fw_bin} ...")
    result = subprocess.run(["gh", "release", "create", f"v{new_ver}", str(fw_bin), "--title", f"v{new_ver}", "--notes", f"OTA {new_ver} - manual /update, 30s poll, AUTO_OTA=false", "--target", "main"], cwd=ROOT)
    if result.returncode != 0:
        print("gh release failed (maybe tag exists). Trying to upload asset to existing release...")
        run(["gh", "release", "upload", f"v{new_ver}", str(fw_bin), "--clobber"], check=False)
    print(f"\n✓ Done! Release v{new_ver} ready: https://github.com/{repo}/releases/tag/v{new_ver}")
    print(f"  ESP32 (FW {cur}) will show: 'New version available! Type /update to flash' on next poll (30s)")
    print(f"  Trigger: python3 upload.py --trigger-ota -p /dev/ttyUSB0  OR  Serial -> /update")

if __name__ == "__main__":
    main()
