#!/usr/bin/env python3
"""
build.py — build .ino -> .bin with arduino-cli and push to GitHub for OTA
Flow: bump FW_VERSION -> compile -> update version.json -> git commit/tag/push -> GitHub Action builds Release
Alternative: with --local-release, upload bin directly via 'gh release create'
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
    cands = list(build_dir.glob("*.bin"))
    if not cands:
        # also check arduino-cli output dir variants
        cands = list(ROOT.glob("*.bin"))
    if not cands:
        return None
    # prefer cloud-ota.ino.bin
    for p in cands:
        if "cloud-ota" in p.name:
            return p
    return cands[0]

def main():
    parser = argparse.ArgumentParser(description="Build + push OTA")
    parser.add_argument("--version", help="New version x.y.z (default: bump patch)")
    parser.add_argument("--fqbn", default=FQBN, help="FQBN")
    parser.add_argument("--local-release", action="store_true", help="Upload bin directly with gh release create (skip Actions)")
    parser.add_argument("--port", help="Not used here, for upload.py")
    parser.add_argument("--skip-push", action="store_true", help="Build only, don't git push")
    args = parser.parse_args()

    if not CONFIG.exists():
        print(f"ERROR: {CONFIG} missing. Run setup.py first.")
        sys.exit(1)

    # check arduino-cli
    if shutil.which("arduino-cli") is None:
        print("ERROR: arduino-cli not found. Install: https://arduino.github.io/arduino-cli/installation/")
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

    # compile
    print(f"\n[1/4] Compiling with arduino-cli ({args.fqbn}) ...")
    run(["arduino-cli", "compile", "--fqbn", args.fqbn, "--output-dir", "./build", "."])

    bin_path = find_bin()
    if not bin_path or not bin_path.exists():
        print(f"ERROR: .bin not found in ./build. Contents: {list((ROOT/'build').glob('*')) if (ROOT/'build').exists() else 'no build dir'}")
        sys.exit(1)
    print(f"✓ bin: {bin_path} ({bin_path.stat().st_size} bytes)")

    # prepare firmware.bin for release
    fw_bin = ROOT / "firmware.bin"
    shutil.copy(bin_path, fw_bin)
    print(f"✓ copied to {fw_bin}")

    # update version.json
    print(f"\n[2/4] Updating version.json ...")
    # get repo from git remote
    try:
        remote = subprocess.check_output(["git", "remote", "get-url", "origin"], text=True).strip()
        # parse github.com/USER/REPO
        m = re.search(r'github\.com[:/](.+?)/(.+?)(\.git)?$', remote)
        repo = f"{m.group(1)}/{m.group(2)}" if m else "YOUR_USERNAME/YOUR_REPO"
    except:
        repo = "hengXiaoHour/cloud-ota"
    data = {
        "version": new_ver,
        "bin_url": f"https://github.com/{repo}/releases/download/v{new_ver}/firmware.bin",
        "notes": f"OTA {new_ver}"
    }
    VERSION_JSON.write_text(json.dumps(data, indent=2) + "\n")
    print(VERSION_JSON.read_text())

    if args.skip_push:
        print("\n--skip-push: stopped before git. Push manually when ready.")
        return

    print(f"\n[3/4] Git commit/tag/push ...")
    # ensure git repo
    run(["git", "add", "config.h", "version.json"])
    # commit may fail if nothing changed
    subprocess.run(["git", "commit", "-m", f"chore: bump OTA to {new_ver}"], cwd=ROOT)
    run(["git", "tag", f"v{new_ver}"], check=False)  # ignore if exists
    run(["git", "push", "origin", "main"])
    run(["git", "push", "origin", f"v{new_ver}"])

    if args.local_release:
        print(f"\n[4/4] Creating GitHub Release directly (local bin) ...")
        if shutil.which("gh") is None:
            print("ERROR: gh CLI not found, cannot create release. Install gh or push tag for Actions to build.")
            sys.exit(1)
        run(["gh", "release", "create", f"v{new_ver}", str(fw_bin), "--title", f"v{new_ver}", "--notes", f"OTA {new_ver} (local build)", "--target", "main"])
        print(f"\n✓ Done! ESP32 will OTA to {new_ver} via {data['bin_url']}")
    else:
        print(f"\n[4/4] Pushed tag v{new_ver}. GitHub Actions will build + attach firmware.bin.")
        print(f"     Watch: https://github.com/{repo}/actions")
        print(f"     ESP32 will poll {data['bin_url']} after version.json update.")
        print(f"\n✓ Done! Tip: trigger now via Serial -> type /update")

    print("\nNext: python3 upload.py  # for wired manual flash if OTA fails")

if __name__ == "__main__":
    main()
