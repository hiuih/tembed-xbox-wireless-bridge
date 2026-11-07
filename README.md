# T-Embed Xbox Wireless Bridge

An experimental firmware for the **LilyGO T-Embed CC1101 (Plus)** (ESP32-S3) that acts like an
Xbox Wireless dongle: Xbox One / Series controllers pair to it over their native 2.4 GHz
"Xbox Wireless" link (not Bluetooth), and the bridge presents each controller to your computer as a
**USB HID gamepad**.

The protocol work is based on the reverse-engineering done by the **xone** and **xow** Linux
drivers (GPL). This project is not affiliated with Microsoft.

## Status

| Feature | State |
|---|---|
| Pair and connect Xbox One / Series controllers (tested: Series) | works |
| Several controllers at once (up to 8) | built, **only tested with one controller** |
| USB gamepad per controller (buttons, sticks, triggers, D-pad) | works |
| LED ring + screen status, knob controls | works |
| Xbox button wakes a sleeping computer (USB remote wakeup) | built, **untested** |
| **Rumble / vibration** | **does NOT work** (see Known issues) |
| Controller LED control (Xbox button light) | sent, **unconfirmed** |
| Encryption / authentication handshake | not implemented |
| Bare ESP32-S3 (no screen/buttons) build `s3_headless` | compiles, **not yet run on hardware** |

## The knob (rotary encoder with a push button)

| Action | What it does |
|---|---|
| **Hold 1 second** | Start **pairing mode** (60 s). Hold again to cancel. Works even if the screen is dead. |
| Turn | Move the selection on screen |
| Click | Open / activate the selected item |
| **Hold 10 seconds** | Reboot into the **bootloader** (the LED ring fills red as a warning) |

**Sleep (T-Embed):** hold the separate **user key (GPIO6, the other button, not the knob) for 5 seconds**. The ring fills orange as a countdown, then the board goes to deep sleep (screen, LEDs and radio off; it drops off the computer's USB). Press the same key to wake it. An earlier version cut the battery with the charger chip; it could then only be woken by unplugging the battery, so that was removed.

## The LED ring (the main status display)

| What you see | Meaning |
|---|---|
| LEDs 1-7, one per controller | green = ready, yellow = connecting, **blinking red/green = controller battery low**, white = its Xbox button is held, quick white flash = connected, red flash = disconnected |
| LED 8 | USB link to the computer: green = connected, **blue = computer asleep**, red = not connected |
| **Spinning blue** | Pairing mode (red-blue pulsing in the last 10 seconds) |
| Green flash (whole ring) | A controller just paired |
| Amber double flash | Pairing timed out / cancelled |
| Dim amber breathing on LED 1 | Idle, waiting for a controller |
| White pulse | The computer was woken |
| Purple | Restarting (changing the number of USB gamepads) |
| Orange pulse | Rumble test |
| Red filling up | Knob held toward the 10-second bootloader reboot |

## Pairing a controller

1. **Hold the knob for 1 second.** The ring flashes white, then spins blue.
2. On the controller, **hold the small Pair button** (top edge, next to the USB-C port) until its
   light **blinks fast**. Keep it close to the T-Embed.
3. The ring flashes **green** and the controller's LED goes steady.

Afterwards just press the **Xbox button** on the controller to reconnect it. Controllers
remember the bridge, and the bridge remembers up to 8 controllers (stored in flash).

Until the first controller is paired, the bridge adopts whichever controller connects first.
After that, unknown controllers can only connect while pairing mode is on.

## The screen (left ~176 px only; the display on this unit is damaged)

- **Controller list:** each paired controller, with battery bars and signal bars. Online controllers
  show live info; offline ones are dimmed.
- **Click a controller:** live view of sticks / triggers / buttons plus a menu:
  *Rumble test, LED brightness, Disconnect, Forget controller, Back*.
- **+ Pair controller:** starts pairing mode (same as holding the knob).
- **Settings:** *USB gamepads* (1-8, restarts the bridge), *Forget ALL controllers*,
  *Reboot to bootloader*, *Back*.

## USB gamepads

Each slot is a separate HID gamepad (report id = slot + 1). The count is fixed while the device is
plugged in (a USB limitation). The default is **4**; the bridge **restarts itself to expose more**
when a controller needs a slot that isn't exposed yet.

| HID control | Source |
|---|---|
| Buttons 1-12 | A, B, X, Y, LB, RB, View, Menu, L3, R3, Guide (Xbox button), Share |
| Buttons 13-16 | D-pad up, down, left, right (also exposed as the hat switch) |
| Hat switch | D-pad (8 directions + released) |
| X, Y / Rx, Ry | Left / right stick (16-bit signed) |
| Z / Rz | Left / right trigger (0-1023) |
| Output report (4 bytes) | strong motor, weak motor, left trigger motor, right trigger motor |

**"Axis 9 stuck at the bottom" in Gamepad Tester is normal.** Browsers show a hat switch (the D-pad)
as an axis. When nothing is pressed it reads about +1.29, off the end of the scale, so it looks pinned
at the bottom. Pressing Down reads +0.14, which looks like "zero". That is how a hat switch is
reported, not a stuck input.

Browsers will say **"vibration N/A"** for this device. They only offer rumble for gamepads they
recognise by name.

## Serial console commands

Open the bridge's USB serial port (it appears as `/dev/cu.usbmodem...`, 115200 baud) and type one letter:

| Key | Action |
|---|---|
| `s` | Status: power report, last reset reason, USB state, radio counters, each controller |
| `d` | Toggle GIP packet tracing (non-input packets, both directions) |
| `p` | Toggle pairing mode |
| `f` | Forget ALL paired controllers |
| `v` | Rumble test (all connected controllers, 0.7 s) |
| `l` | Cycle the Xbox button LED mode on all controllers |
| `1` `2` `3` | Diagnostics: rumble / rumble(internal) / LED command with an acknowledge request |
| `B` | Reboot into the ROM bootloader |
| `R` | Restart |

The status line `last reset: ...` says why the chip last reset (power-on, brownout, crash,
watchdog...), and for a watchdog reset which step of the main loop it was in.

## Flashing (no buttons needed)

```sh
~/.local/pipx/venvs/platformio/bin/python tools/flash.py
```

The script puts the running board into the bootloader (1200-baud touch), flashes it with
PlatformIO, then resets it with a watchdog reset. (The normal DTR/RTS reset leaves this board stuck in
the bootloader; `tools/upload_hook.py` replaces it.) Needs PlatformIO and `esptool` >= 5 on your PATH.

Build only: `pio run -e tembed`.

## If the board is stuck / not showing up

1. A software route: hold the knob 10 s (bootloader), or send `B` on the serial console.
2. Hardware route: **hold the knob (BOOT), click the tiny RST switch on the back of the board, keep
   holding ~2 s, then release.** The chip stays in the bootloader after you let go; it appears on the Mac
   as `USB JTAG_serial debug unit`. Then run `tools/flash.py`.
3. The firmware also reboots itself into the bootloader if it crashes 3 times in a row, and a hardware
   watchdog resets it if the main loop stalls for 5 s.
4. macOS: set **System Settings > Privacy & Security > Allow accessories to connect** to
   *Always / Automatically when unlocked*, or the Mac can hold the board back at an "Allow accessory"
   prompt after every reboot.

## Bare ESP32-S3 board (no screen, no buttons): the `s3_headless` build

Works on a plain ESP32-S3 dev board (for example the ESP32-S3-DevKitC-1). Nothing needs wiring.

**Pairing with no hardware at all:** every time the board powers up, a **2-minute pairing window opens
automatically**. Plug it in, then hold the small Pair button on the controller until it blinks fast. To pair
another controller later, unplug and replug the board (the paired list is kept). Optional extras:
- the board's **BOOT button** (GPIO0): hold 1 second to toggle pairing, hold 10 seconds for the bootloader;
- the **serial console** commands above (`p` pair, `f` forget all, `s` status...);
- the board's **RGB LED** (GPIO48 by default; `-DLED_PIN=38` for v1.1 boards):

| LED | Meaning |
|---|---|
| Fast blue blinking | Pairing window open |
| Green flashes, one per connected controller, then a pause | Connected (red flash = that controller's battery is low) |
| In the pause: dim red / dim blue | USB not connected / computer asleep |
| Solid yellow | A controller is connecting |
| Amber breathing | Idle, waiting for a controller |
| Whole-LED flash | Green = paired, white = woke the computer, purple = restarting |

**Use the board's native USB port** (the one labelled *USB*, wired to GPIO19/20), not the UART/COM one.

Build and flash (no buttons for a blank board or one already running this firmware):

```sh
~/.local/pipx/venvs/platformio/bin/python tools/flash.py s3_headless
```

If the board is running some other firmware, hold its BOOT button while plugging it in once, to start it in the bootloader.
Only the T-Embed build has been tested on real hardware; this one compiles but has not been run yet.

## Windows XInput build (`s3_xinput`): shows up as a real Xbox controller

Games like FIFA treat a generic USB gamepad as a PlayStation-style DirectInput device. This build instead
presents the bridge as an **Xbox 360 (XInput) controller** (USB `045E:028E`), so Windows and games use the Xbox
mapping and glyphs. **One controller works on Windows.** Two controllers in one USB device is experimental.

- XInput must be the **only USB function**: no gamepad-HID, no USB serial. The log/console is the board's **UART
  USB-C port** (115200 baud); the **native/OTG port** goes to the PC. You can't use both ports at once if your
  cables don't reach, so use the UART port to flash and read logs, then move the cable to the PC.
- Flash through the UART port (no 1200-baud touch here):
  `pio run -e s3_xinput -t upload --upload-port /dev/cu.usbmodemXXXX`
- Interface N carries the N-th controller that connected (player 1 = first connected).
- Rumble requests from games are received and forwarded, but rumble still does not work on the radio side.
- Mac/Linux: use the HID builds instead.

**Layouts**: hold the board's **BOOT button for 4 seconds** to cycle; the layout is stored in flash and shown by
purple LED blinks for the first 6 seconds after boot:

| Blinks | Layout |
|---|---|
| 1 | one Xbox controller interface (default, known to work on Windows) |
| 2 | two interfaces, real-pad class descriptor (experimental) |
| 3 | two interfaces, receiver-style class descriptor, as in github.com/Berghopper/360-w-raw-gadget (MIT) (experimental) |

If only one controller shows up in Windows with a two-controller layout, try the other one; if neither works,
Windows does not accept several XInput interfaces in this device.

Extra serial commands in this build (UART): `r` dump what each XInput interface is being fed, `x` census of
everything the controller transmits (frame types, ack policy, PHY rate), `z` timing trace of the controller's
vendor frames, `w` cycle TX power, `m` toggle RX filter, `e` toggle radio tweaks, `k` pin the channel (1/6/11/auto).

### Wireless link measurements (T-Embed + bare S3, Series controller)

- ~100-118 input reports/s while a stick is moved; 99% of intervals under 40 ms.
- The controller sends HT20 MCS3 frames with normal ack policy. It also sends a vendor frame (`70 0e`) 3-5 times
  a second, retried 4-6 times in a burst, because the ESP32 radio never acknowledges that (reserved) frame type.
  It costs about 1 ms of airtime per burst.
- About 17-25% of GIP sequence numbers are never seen. TX power, receive filter, channel (1/6/11), receive buffers
  and driver beacon settings did not change that.

## T-Embed app (`tembed_xinput`): screen, knob and automatic Xbox pads

Built for the T-Embed CC1101 (Plus) as an app-only image for the **Launcher** firmware: copy `firmware.bin` to the SD card
(for example `downloads/XboxBridge.XInput.0.2.bin`) and install it from Launcher's SD menu. Do **not** flash it over USB
with an upload: that writes at the start of flash and would erase Launcher and the other installed firmware.

**Controls:** turn the knob to move, click to select, **top key short press = back**, **hold the knob 1 s = pairing**,
hold the top key 5 s = deep sleep (press it again to wake). The battery is never disconnected.

**Screens:** Home (6 tiles), Controllers (live list), Controller (sticks with trails, triggers, buttons, rumble/LED/drop/forget),
Pair (radar + countdown), Radio (channel, frame and event rates, queue drops, loop time, memory, 60 s graphs),
Spectrum (2.4 GHz occupancy from the nRF24, with Wi-Fi channels 1/6/11 marked), Power (battery, voltage graph, sleep now),
Settings (brightness, LED ring, accent colour, auto-sleep, USB style, knob direction, forget all, bootloader), About.

**0.7 additions:** 9-tile home (scrolls), **Latency** screen (input-gap histogram, loss, rate per controller), **Event log**
(connects, USB re-plugs, pairing, sleep), and 17 settings: brightness, auto-dim, auto-sleep, accent, LED ring level and idle
effect, flip screen, knob direction, **USB mode**, stick deadzone, Xbox-button-wakes-PC, pairing window, pair on power-up,
reset, forget all, bootloader. The footer explains the selected setting.
**USB mode:** *Wired 360 pad* presents the descriptor of a wired Xbox 360 controller (best for most games);
*360 receiver* presents the descriptor of the wireless-receiver style device. Windows treats each as a separate device.

**Bluetooth (1.0):** *Settings > Bluetooth* (restarts the board) makes it a Bluetooth LE gamepad device named "Xbox Bridge".
Pair it from the computer's Bluetooth settings; the Xbox controllers appear as up to four separate gamepads in that one
device (one HID gamepad collection each). The ESP32-S3 has Bluetooth LE only, it is not an XInput device (games that only
read XInput will not see it; games using generic HID/DirectInput/SDL will), and it shares the 2.4 GHz radio with the Xbox
link, so expect a little more latency than USB. Off by default.

**Automatic Xbox pads:** the number of Xbox controllers shown to the PC follows the number actually connected (1 to 4). It grows
1.5 s after another controller becomes ready and shrinks only after the extra ones have been gone for 30 s. The change is a soft
USB unplug/replug (about a second), not a restart. *Settings > USB style* switches between the two descriptor styles (A/B).
Whether Windows accepts more than one Xbox interface in a single USB device is **not confirmed**.

**The nRF24 cannot carry Xbox controller traffic** (it is a different kind of radio). It is used only as the spectrum scanner.

**Screen simulator:** `tools/sim/build_and_render.sh` renders every screen to PNG on the Mac using the real drawing code
(`src/ui_screens*.cpp`, fake data), so layouts can be reviewed without hardware.

## Waking the computer

When the computer is suspended, pressing the Xbox button (or a controller connecting) sends a **USB remote
wakeup**. This needs the host to allow wakeup for the device and the USB port to stay powered during sleep.
It can wake a *sleeping* computer, not one that is shut down. Untested so far.

## Known issues

- **A browser can block the gamepad on macOS.** If Gamepad Tester shows nothing and `hidutil list` has no `0x303a` entry, check whether a Chromium-based browser (it showed up as a `Dia` user client in `ioreg`) has the USB device open. Quit it completely (Cmd+Q) and replug the board's native USB port: the gamepad attaches normally. Not seen on Windows.
- **Rumble does not work.** The controller acknowledges rumble packets at the protocol level and
  rumbles fine over Bluetooth, but ignores them from the bridge. Suspected cause: the bridge does not
  perform the Xbox security handshake (xone's `auth` code) that a real dongle performs. Not confirmed.
  The LED command is also unconfirmed.
- During experiments one controller needed its battery removed to power back on. Avoid sending unusual
  packets to a controller you care about.
- Multiple controllers at once, USB wake, and the LED/Share-button decoding are untested.
- The unit's display is damaged, so the screen layout is limited to the left part; use the LED ring.

## Protocol documentation

How the Xbox Wireless controller link works, and what you need to implement it on your own hardware:
[docs/PROTOCOL.md](docs/PROTOCOL.md) (every claim is tagged observed / from the xone driver / unknown).

## Layout

```
src/radio.*      Wi-Fi radio: dongle beacon, raw 802.11 TX/RX, event queue
src/gip.*        Per-controller Game Input Protocol state machine
src/pairing.*    Paired-controller list (flash) and pairing window
src/hid.*        USB HID gamepads (TinyUSB) and the serial port
src/ui.*         Screen + knob menus
src/leds.*       LED ring (T-Embed) / single RGB LED (headless) status display
src/knob.*       Hold-to-pair
src/bootguard.*  Crash-loop guard, reset reason, software bootloader entry
src/power.*      Charger (BQ25896) status report
src/main.cpp     Glue and serial commands
tools/           flash.py and the upload hook
```
