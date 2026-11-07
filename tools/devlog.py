#!/usr/bin/env python3
"""Read the board's persistent debug log over USB (vendor request 0xB0 on the XInput interface).

    python3 tools/devlog.py            # print the whole log once
    python3 tools/devlog.py --follow   # keep reading (this also keeps the USB device attached with zero pads)

Needs:  brew install libusb   and   pip install pyusb
The log survives crashes, watchdog resets and deep sleep. The first lines after a restart say why it restarted.
"""
import sys, time
import usb.core, usb.util

VID, PID = 0x045E, 0x028E
CHUNK = 240

def read_all(dev):
    out = b""
    i = 0
    while True:
        d = bytes(dev.ctrl_transfer(0xC1, 0xB0, i, 0, CHUNK, timeout=1000))   # vendor | interface | device-to-host
        out += d
        if len(d) < CHUNK:
            return out
        i += 1

def find():
    return usb.core.find(idVendor=VID, idProduct=PID)

def main():
    follow = "--follow" in sys.argv
    shown = 0
    last = b""
    while True:
        dev = find()
        if dev is None:
            if not follow:
                sys.exit("no Xbox Bridge found (plug in the native USB port; it stays attached for 30 s after boot)")
            time.sleep(0.3)
            continue
        try:
            data = read_all(dev)
        except usb.core.USBError as e:
            if not follow:
                sys.exit(f"read failed: {e}")
            time.sleep(0.3)
            continue
        if not follow:
            sys.stdout.write(data.decode("utf-8", "replace"))
            return
        # print only what is new (the log is a ring, so match on the tail of the previous read)
        text = data.decode("utf-8", "replace")
        tail = last[-200:]
        if last and text.startswith(last):
            new = text[len(last):]
        elif tail and tail in text:
            new = text[text.rindex(tail) + len(tail):]
        else:
            new = text
        if new:
            sys.stdout.write(new); sys.stdout.flush()
        last = text
        time.sleep(0.5)

if __name__ == "__main__":
    main()
