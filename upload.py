#!/usr/bin/env python3
"""
upload.py — manual USB flash to ESP32 via arduino-cli
Also: --trigger-ota to send /update over serial and let ESP32 fetch from GitHub
"""
import pathlib, subprocess, sys, shutil, argparse, glob, time, re

ROOT = pathlib.Path(__file__).parent
FQBN = "esp32:esp32:esp32"

def run(cmd, cwd=ROOT, check=True):
    print(f"$ {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if check and result.returncode != 0:
        sys.exit(result.returncode)
    return result

def list_ports():
    ports = []
    # arduino-cli board list
    if shutil.which("arduino-cli"):
        try:
            out = subprocess.check_output(["arduino-cli", "board", "list"], text=True)
            print(out)
            # parse /dev/tty...
            for m in re.findall(r'(/dev/tty\w+)', out):
                ports.append(m)
        except:
            pass
    if not ports:
        ports = glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*") + glob.glob("/dev/cu.*")
    return sorted(set(ports))

def trigger_ota(port, baud=115200):
    """Send /update over serial to trigger GitHub OTA (no reflash)"""
    print(f"[OTA] Sending /update to {port} @ {baud}")
    try:
        import serial  # pyserial
    except ImportError:
        print("pyserial not installed. Install: pip install pyserial")
        print(f"Fallback: run manually: arduino-cli monitor -p {port} -c baudrate={baud}")
        print("Then type /update + Enter")
        sys.exit(1)
    try:
        ser = serial.Serial(port, baud, timeout=2)
        time.sleep(2)  # wait for reset
        ser.write(b"/update\n")
        print("Sent /update, reading response (10s)...")
        end = time.time() + 10
        while time.time() < end:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode(errors="ignore")
                print(data, end="")
            time.sleep(0.1)
        ser.close()
        print("\n✓ Trigger sent. Watch ESP32 logs for [OTA] flow.")
    except Exception as e:
        print(f"ERROR serial: {e}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description="Manual upload to ESP32")
    parser.add_argument("-p", "--port", help="Serial port (/dev/ttyUSB0)")
    parser.add_argument("--fqbn", default=FQBN)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--trigger-ota", action="store_true", help="Don't flash, just send /update over serial to fetch from GitHub")
    parser.add_argument("--build-only", action="store_true", help="Only compile, don't upload")
    args = parser.parse_args()

    if args.trigger_ota:
        port = args.port
        if not port:
            ports = list_ports()
            if not ports:
                print("No serial ports found.")
                sys.exit(1)
            print("Found ports:", ports)
            port = input(f"Port [{ports[0]}]: ").strip() or ports[0]
        trigger_ota(port, args.baud)
        return

    # manual flash flow
    if shutil.which("arduino-cli") is None:
        print("ERROR: arduino-cli not found")
        sys.exit(1)

    port = args.port
    if not port and not args.build_only:
        ports = list_ports()
        if ports:
            print(f"Detected ports: {ports}")
            port = input(f"Port [{ports[0]}] (Enter to skip auto-detect): ").strip() or ports[0]
        else:
            port = input("Port (/dev/ttyUSB0): ").strip()
            if not port:
                print("No port given, will only compile.")
                args.build_only = True

    print(f"\n[1/2] Compiling ...")
    run(["arduino-cli", "compile", "--fqbn", args.fqbn, "."])
    print("✓ Compile OK")

    if args.build_only:
        print("Build only done. No upload.")
        return

    print(f"\n[2/2] Uploading to {port} ...")
    run(["arduino-cli", "upload", "-p", port, "--fqbn", args.fqbn, "."])
    print(f"\n✓ Uploaded to {port}. Now monitor:")
    print(f"  arduino-cli monitor -p {port} -c baudrate={args.baud}")
    print(f"  Type /update to pull GitHub OTA, or version to check FW")

if __name__ == "__main__":
    main()
