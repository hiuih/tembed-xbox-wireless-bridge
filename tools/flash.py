#!/usr/bin/env python3
"""One-command flash for the T-Embed Xbox bridge. No buttons needed.

Usage:  ~/.local/pipx/venvs/platformio/bin/python tools/flash.py [env]   (env: tembed (default) or s3_headless)
        needs pyserial, e.g. PlatformIO's venv

 1. If the board is running the app, ask it to reboot into the ROM bootloader (1200 baud touch).
 2. Wait for the bootloader's serial port to appear (the port name changes).
 3. Flash with PlatformIO (the upload hook then resets the board with a watchdog reset).
"""
import glob, shutil, subprocess, sys, time
import serial

ESPTOOL = shutil.which("esptool") or shutil.which("esptool.py")
PIO = shutil.which("pio") or shutil.which("platformio")
ROOT = __file__.rsplit("/tools/", 1)[0]

def ports():
    return sorted(glob.glob("/dev/cu.usbmodem*"))

def in_rom(port):
    r = subprocess.run([ESPTOOL, "--chip", "esp32s3", "--port", port, "--before", "no-reset", "--after", "no-reset", "chip-id"],
                       capture_output=True, text=True, timeout=30)
    return r.returncode == 0

def find_rom(deadline):
    while time.time() < deadline:
        for p in ports():
            try:
                if in_rom(p):
                    return p
            except Exception:
                pass
        time.sleep(0.7)
    return None

def main():
    rom = None
    for p in ports():
        if in_rom(p):
            rom = p
    if not rom:
        for p in ports():
            print(f"touching {p} at 1200 baud (reboot to bootloader) ...")
            try:
                s = serial.Serial(p, 1200); time.sleep(0.2); s.close()
            except Exception as e:
                print("  touch failed:", e)
        rom = find_rom(time.time() + 25)
    if not rom:
        sys.exit("Board not found. Plug it in; if the macOS 'Allow accessory' prompt is showing, click Allow.")
    print("bootloader on", rom)
    sys.exit(subprocess.run([PIO, "run", "-e", (sys.argv[1] if len(sys.argv) > 1 else "tembed"), "-t", "upload", "--upload-port", rom], cwd=ROOT).returncode)

main()
