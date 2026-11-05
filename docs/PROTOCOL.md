# Xbox Wireless controller link: protocol notes and implementation guide

How an Xbox One / Series controller talks to its "dongle" (the Xbox Wireless Adapter or a console) over
Xbox Wireless, and what you need to impersonate the dongle on your own hardware.

This is independent documentation written from (a) experiments with an ESP32-S3 and an Xbox Series controller
and (b) reading the public **xone** and **xow** Linux drivers. It is not affiliated with Microsoft. No driver code
is copied here; only protocol facts are described. Corrections welcome.

## How to read this document

Every claim is tagged with how it is known:

| Tag | Meaning |
|---|---|
| **[O]** | Observed on real hardware by this project (ESP32-S3 impersonating the dongle, Series controller, model 1914, USB PID `0B12`). |
| **[X]** | Read directly from the xone driver source (GPL-2.0-or-later, github.com/dlundqvist/xone). |
| **[W]** | From the xow README (github.com/medusalix/xow). |
| **[?]** | Unknown, unverified, or my interpretation. Do not rely on it. |

Only **one controller model and firmware** was tested. Other controllers, older Xbox One pads, Elite pads, and
headsets are not covered by observation.

## 1. Architecture

```
 controller  <== 2.4/5 GHz radio (802.11-style frames) ==>  dongle
   layer 3   GIP  (Game Input Protocol): commands, input reports, rumble, LEDs ...
   layer 2   802.11 QoS-data frames carry GIP; vendor management frames (subtype 7) carry link control
   layer 1   802.11 beacon / association (custom, minimal), OFDM/HT radio
```

- The **dongle behaves like a tiny custom Wi-Fi access point**; the **controller is the station**. [X][W]
- The official adapter is a MediaTek **MT76**-family Wi-Fi chip running Microsoft firmware that is loaded by
  the host driver; the radio-level work (acknowledgements, channel handling) happens in that firmware/hardware. [X][W]
- Authentication and encryption exist (GIP-level ECDH handshake, then AES-CCMP keys), but **the tested controller
  sent unencrypted input without it** (section 8). [O]

## 2. Radio

### 2.1 Channels [X]

The dongle uses 12 channels; the beacon announces the one in use.

| Band | Channels | Width |
|---|---|---|
| 2.4 GHz | 1, 6, 11 | 20 MHz |
| 5 GHz UNII-1 | 36, 40, 44, 48 | 40 MHz |
| 5 GHz UNII-3 | 149, 153, 157, 161, 165 | 80 MHz |

- A 2.4 GHz-only radio (ESP32, ESP32-S3) is enough to talk to the tested controller: it associated on
  channels 1, 6 and 11. [O] Whether it always prefers 2.4 GHz is unknown. [?]
- While pairing and with no client connected, the official driver rotates channels every **2 s**; with any client
  connected it stays put. [X] The ESP32 implementation hopped 1 -> 6 -> 11 every 3 s with no client and held the
  channel once a controller was registered; controllers found it that way. [O]

### 2.2 What the controller transmits [O]

| Frame | PHY seen | Notes |
|---|---|---|
| Probe request (type 0/sub 4), broadcast | legacy OFDM rates only (6-54 Mbps IE, no 11b rates) | sent while searching |
| Association request (type 0/sub 0), later re-association (sub 2) | - | sent to the BSSID after it hears a beacon |
| Vendor management (type 0/sub **7**) | legacy 6 Mbps OFDM | see section 4 |
| QoS data (type 2/sub 8) carrying GIP | HT, 20 MHz, long guard interval | normal ack policy |
| QoS null (type 2/sub 12) | HT, 20 MHz | ~60-85 per second while connected; ack policy normal |

The exact HT MCS index was **not** captured (the receiver field read was only valid for legacy frames). [?]

### 2.3 Acknowledgements (the hard part)

- Every unicast frame the controller sends expects a normal 802.11 **ACK** from the dongle within SIFS, so the ACK
  must come from **hardware**, not software. [O]
- QoS data, QoS null and association requests were acknowledged by the ESP32-S3 when it ran as a **soft-AP with the
  dongle MAC set on the AP interface**. [O] I did not isolate whether station mode with a spoofed MAC would also
  work. [?]
- **Vendor management frames (subtype 7) were never acknowledged by the ESP32-S3.** The controller sends such a
  frame (command `0x0E`, section 4.3) about 2-5 times per second and retransmits each one **4-6 times** within a
  few milliseconds (100-400 us apart), then moves on. [O] Input still flowed, so this is not fatal, but a
  chip that ACKs these would be cleaner. The official MT76 dongle presumably ACKs them in hardware. [?]

## 3. Discovery and association

### 3.1 Beacon (dongle -> broadcast) [X][O]

An ordinary beacon management frame, every **100 ms**:

| Field | Value |
|---|---|
| Frame control | `80 00` (beacon) |
| Address 1 (DA) | `ff:ff:ff:ff:ff:ff` |
| Address 2 (SA) / Address 3 (BSSID) | the dongle's MAC (any locally chosen MAC worked) |
| Timestamp | microseconds, 8 bytes |
| Beacon interval | `64 00` (100 TU) |
| Capability info | `31 c6` (0xC631) |
| Body | `00 00` (SSID IE, length 0), then the vendor IE below |
| TX rate | OFDM (6 Mbps used on the ESP32) |

Vendor IE (18 bytes including its id and length bytes):

```
dd 10                       element id 0xDD, length 16
00 50 f2 11                 Microsoft OUI 00:50:F2, type 0x11
01 10                       constant in xone
PP                          pairing flag: 01 = pairing mode, 00 = normal
a5 30 99                    constant in xone
CC                          channel number currently in use (e.g. 06)
00 00 00 00 00              zero padding
```

- `PP` is rewritten when pairing starts/stops. [X] A controller that is in pairing mode (Pair button, fast-blinking
  light) responded when beacons carried `PP = 01`. [O]
- The meaning of `01 10` and `a5 30 99` is unknown (xone comments: "probably includes the selected channel pair"). [?]

### 3.2 Association (controller -> dongle -> controller)

1. Controller hears a beacon (it scans all channels), may send broadcast probe requests, then sends an
   **association request** (type 0/sub 0, or sub 2 for re-association) addressed to the dongle MAC. [O]
2. The dongle replies with an **association response**, to the controller's address, from the dongle MAC / BSSID. [X][O]

Association response body (14 bytes after the 24-byte header):

```
00 00        capability info
10 01        status code 0x0110   (not a standard 802.11 status; copied from the original, "original status code")
00 0f        association id 0x0f00
00 00 00 00 00 00 00 00   8 zero bytes
```

- No 802.11 *authentication* frame was observed being sent to the bridge before the association request. [O]
  (My receive filter only passed frames addressed to the BSSID and I only reacted to association requests, so I
  cannot rule out that authentication frames exist.) [?]
- The official driver then allocates a client slot (up to 16) and tells its firmware the client's MAC, an index,
  and a few parameters. A software implementation just needs to keep per-controller state. [X]
- The controller remembers the dongle MAC from the last pairing and reconnects when you press the Xbox button. [O]
  A controller reconnects to a fixed fake MAC chosen by the implementer (`62:45:bd:00:00:01` in this project). [O]

### 3.3 Sequence numbers

Every frame you transmit needs a proper, increasing 12-bit 802.11 sequence number in the sequence-control
field. An early build sent every frame with sequence 0 and never got past the identify exchange; it was fixed by
adding increasing sequence numbers and correct chunk acknowledgements **at the same time**, so which change
mattered is not isolated. Do both. [O]

## 4. Vendor management frames (type 0, subtype 7)

Subtype 7 is a *reserved* management subtype; the protocol uses it for link control. [X][O]

Frame: management header with address 1 = destination, address 2 = sender, address 3 = BSSID. Body:

```
70 CC <data...>        0x70 marker, then a one-byte command
```

### 4.1 Commands [X]

| CC | Name | Direction | Notes |
|---|---|---|---|
| `01` | PAIR_REQ | controller -> dongle | observed body: `70 01 01` (3 bytes) [O] |
| `02` | PAIR_RESP | dongle -> controller | data: `00 45 55 01 0f 8f ff 87 1f` |
| `03` / `04` | CHANGE_CHAN_REQ / RESP | | not exercised |
| `05` / `06` | STATISTICS_REQ / RESP | | a `70 06 ...` frame with a 202-byte body was seen once from the controller [O] |
| `07` / `08` | SCAN_CHAN_REQ / RESP | | not exercised |
| `10` | ENABLE_ENCRYPTION | controller -> dongle, dongle answers `70 10 00 00` | part of the encryption setup (section 8); not needed in the tested setup |

The official driver silently ignores unknown vendor commands. [X]

### 4.2 Pairing flow [O][X]

1. Dongle enters pairing mode: beacon pair flag `PP = 01` for up to **60 s**. [X]
2. User holds the controller's Pair button until it blinks fast. The controller associates and sends `70 01 01`.
3. Dongle replies `70 02 00 45 55 01 0f 8f ff 87 1f` (a reply is only useful if the frame can actually be transmitted;
   on ESP32 the Wi-Fi library rejects reserved management subtypes until you bypass its raw-frame sanity check).
4. Dongle clears the pair flag. The controller now stays associated and later reconnects on its own.

### 4.3 Command `0x0E` (unknown) [O]

The controller sends an 18-byte body about 2-5 times/s, each message transmitted 4-6 times (no ACK, section 2.3):

```
70 0e 00 00 5e 01 XX 01 90 01 00 00 e8 03 00 00 00 00
```

`XX` varies between messages (values 0xA6-0xDD seen); the constants decode as little-endian 350, 400 and 1000.
Meaning unknown (link-quality or telemetry report?). [?]

## 5. Data frames

- **Dongle -> controller:** QoS data (type 2/sub 8), **FromDS** set, duration 144, address 1 = controller,
  addresses 2 and 3 = dongle MAC, 26-byte header (24 + 2-byte QoS control = 0), then the GIP packet(s) directly.
  [X][O] xone inserts two extra zero bytes before the header in its buffers; those are DMA alignment padding for the
  MT76 chip. They are not required on air: the ESP32 sent none and the controller answered. [O]
- **Controller -> dongle:** QoS data, address 1 = BSSID, GIP bytes start right after the 26-byte header and end
  4 bytes before the end (FCS). One frame may hold several back-to-back GIP packets. [O]
- Maximum GIP packet size per frame is **58 bytes**; larger transfers are chunked (6.3). [X]
- Retransmissions carry the retry bit and the same sequence number; ignore duplicates. [O]
- QoS null frames from the controller (~60-85/s) appear to act as a keepalive; treat a controller as gone after
  ~4 s of silence. [O]

## 6. GIP (Game Input Protocol)

### 6.1 Packet header [X][O]

```
byte 0   command
byte 1   options:   bits 3..0  client id (0 for a single-function device)
                    bit 4      0x10 ACK requested
                    bit 5      0x20 INTERNAL (system/control packet; input and rumble are not internal)
                    bit 6      0x40 CHUNK_START
                    bit 7      0x80 CHUNK
byte 2   sequence number (never 0; host and controller each count their own)
byte 3.. packet length, 7-bit groups, least significant first, bit 7 = "more bytes follow"
         if CHUNK: chunk offset, same varint encoding
         the header is padded to an even length by setting the continuation bit on the last length byte and
         appending a 0x00
```

Most packets in practice have a 1-byte length, i.e. a 4-byte header: `CMD OPT SEQ LEN`.

### 6.2 Commands [X][O]

| Cmd | Name | Internal | Notes |
|---|---|---|---|
| `01` | ACKNOWLEDGE | yes | see 6.4 |
| `02` | ANNOUNCE | yes | controller -> host, 28-byte payload (6.5) |
| `03` | STATUS | yes | payload byte 0: bit 7 connected, bits 3..2 battery type, bits 1..0 level |
| `04` | IDENTIFY | yes | host requests; controller answers with a chunked descriptor |
| `05` | POWER | yes | payload `00` = on, `01` sleep, `04` off, `07` reset |
| `06` | AUTHENTICATE | yes | security handshake (section 8) |
| `07` | VIRTUAL_KEY | yes | Guide button: payload `DD 5B` (DD = 1 down / 0 up, `5B` = left-Win key code) |
| `08` | AUDIO_CONTROL | yes | headsets, not covered |
| `09` | RUMBLE | no | host -> controller (6.7) |
| `0A` | LED | yes | host -> controller (6.7) |
| `0B` | HID_REPORT | yes | |
| `0C` | FIRMWARE | yes | |
| `1E` | EXTENDED | yes | capabilities / serial number |
| `20` | INPUT | no | controller -> host (6.6) |
| `60` | AUDIO_SAMPLES | | |
| `1F` | ? | yes, ACK requested | observed ~10/s, 1-byte payload (`06`); requests an ACK; meaning unknown [O][?] |

### 6.3 Chunked transfers [X]

- First chunk: options include ACK|CHUNK_START|CHUNK; its chunk-offset field holds the **total length**, its length
  field the chunk size (observed 122 bytes). Following chunks carry their **byte offset**. An **empty chunk**
  (length 0) from the sender signals completion. [X][O]
- The receiver acknowledges each chunk that has the ACK flag and the last non-empty chunk (6.4). [X]

### 6.4 ACKNOWLEDGE payload (9 bytes) [X][O]

```
header:  01  20  <seq of the packet being acked>  09
payload: 00  <cmd of acked packet>  <options>(use 20)  <len lo> <len hi>  00 00  <rem lo> <rem hi>
```

`len` = bytes received so far (offset + chunk length for chunked packets, packet length otherwise); `rem` =
bytes still expected (total - len), 0 for non-chunked packets. A controller kept retransmitting a chunk until the
ACK carried the correct `len`/`rem`. [O]

### 6.5 ANNOUNCE payload (28 bytes) [X][O]

```
6   MAC address of the controller
2   unknown
2   vendor id   (0x045E = Microsoft)
2   product id  (0x0B12 for the tested Series controller)
8   firmware version  (4 x u16: major, minor, build, revision)
8   hardware version  (4 x u16)
```

### 6.6 INPUT payload (gamepad) [X][O]

```
u16 LE  buttons
u16 LE  left trigger   0..1023
u16 LE  right trigger  0..1023
s16 LE  left stick X     s16 LE  left stick Y     (up = positive)
s16 LE  right stick X    s16 LE  right stick Y    (up = positive)
(longer packets append a Share-button byte and timestamps; layout not verified [?])
```

Button bits: 2 Menu, 3 View, 4 A, 5 B, 6 X, 7 Y, 8 D-up, 9 D-down, 10 D-left, 11 D-right, 12 LB, 13 RB, 14 L3,
15 R3. [X] Verified by pressing buttons: A, B, X, Y, all four D-pad directions. [O] Not verified: LB/RB/L3/R3/View/Menu
bit positions, triggers' full range, the Share button and the Guide button's VIRTUAL_KEY packet. [?]

### 6.7 Output commands

| Packet | Bytes | Status |
|---|---|---|
| POWER on | `05 20 SEQ 01 00` | sent after identification [O] |
| LED | `0A 20 SEQ 03 00 MODE BRIGHT` (MODE: 0 off, 1 on, 2-4 blink, 8/9 fade) | sent, controller ACKed; **visible effect never confirmed** [?] |
| RUMBLE | `09 00 SEQ 09 00 0F LT RT L R FF 00 EB` (motor values 0..100) | sent, controller ACKed; **no vibration observed** [O] |

The rumble packet layout is from xone. The tested controller rumbled normally over Bluetooth, so the motors work;
why it ignored rumble over this link is **unsolved**. Candidate causes: the missing security handshake (section 8),
a required packet ordering, or a rumble enable flag. [?]

## 7. Host session state machine

1. Beacon every 100 ms on channel C (pair flag per mode).
2. On association request: send association response (rate-limit repeated requests).
3. On vendor `70 01` while pairing: send `70 02 ...`, then clear the pair flag.
4. For every GIP packet that has the ACK flag: send ACKNOWLEDGE (6.4) promptly.
5. On ANNOUNCE (also whenever the controller sends a new one after you already considered it identified, which
   means its GIP session restarted): send IDENTIFY (`04 20 SEQ 00`); reset your per-controller state.
6. Reassemble the chunked IDENTIFY answer (about 291 bytes in the tested controller), ACKing as in 6.3/6.4.
7. When it completes: send POWER on, then LED.
8. Consume STATUS and INPUT packets. Input began flowing without the security handshake. [O]
9. If nothing is heard from the controller for ~4 s, drop it; it will reconnect when woken.
10. Identification is optional for input: a controller that never completes it can still be treated as ready after
    ~1.5 s. [O]

Two controllers connected and exchanged traffic simultaneously with this state machine for 50+ seconds. [O]

## 8. Security (not implemented here)

xone performs a GIP-level handshake (commands `06`): host hello, controller certificate and public key,
ECDH key agreement, SHA-256 transcript and HMAC-based key derivation, then loads a **16-byte AES-CCMP pairwise key**
into the radio; when the controller sends vendor command `10` it answers `70 10 00 00` and from then on marks frames to that client as protected. The host-side code generates its own key pair and only extracts the
controller's public key from its Microsoft-issued certificate, so no Microsoft secret appears to be needed on the
host side. [X] (not tested here)

Controllers are not the only party that can require this: other models, newer firmware, and Xbox consoles may. [?]

## 9. Requirements for your own hardware

You need a radio that can do all of these. Items marked **hard** were the real obstacles.

| # | Requirement | Notes |
|---|---|---|
| R1 | Transmit hand-built management frames: beacon with custom IE every 100 ms | ESP32: `esp_wifi_80211_tx`, with a soft-AP running so the radio is up |
| R2 | Set the radio's MAC address to a value you choose | controllers pair to it |
| R3 | Receive every frame addressed to that MAC, including vendor subtype 7 | ESP32: promiscuous mode |
| R4 | **Hardware ACK** of unicast frames to that MAC within SIFS | **hard**; QoS/assoc OK on ESP32 soft-AP; subtype 7 not acked |
| R5 | Choose the channel (2.4 GHz 1/6/11 suffices for the tested controller) | |
| R6 | Fixed OFDM TX rate for beacons/management (6 Mbps used) | ESP32: disable A-MPDU TX to force a rate |
| R7 | Set 802.11 sequence numbers in software | section 3.3 |
| R8 | Receive HT20 data frames | |
| R9 | Transmit reserved management subtypes | ESP32 library rejects subtype 7; its frame sanity check must be overridden |

### 9.1 ESP32 notes [O]

- Soft-AP mode with the dongle MAC on the AP interface; raw TX with `esp_wifi_80211_tx`; promiscuous RX.
- `esp_wifi_80211_tx` returned `ESP_ERR_INVALID_ARG` for subtype 7 until `ieee80211_raw_frame_sanity_check` was
  overridden (strong definition + `-Wl,-zmuldefs`).
- `esp_wifi_internal_set_fix_rate` only works with `ampdu_tx_enable = 0`.
- Radio-side tuning (TX power, RX filter, channel, buffer counts) did **not** change the loss figures below.

### 9.2 Using a laptop or PC Wi-Fi chip: untested assessment [?]

Nobody has publicly done this, as far as I could find, and I have not tried. What it would take:

- **Windows / macOS:** I know of no supported way to get raw-frame injection, custom beacons, or MAC-level ACK
  control from their Wi-Fi drivers. Treat as infeasible unless someone shows otherwise.
- **Linux:** conceivable with a chip whose driver supports AP/monitor mode plus frame injection **and** hardware
  ACK for a chosen MAC (R4). MediaTek MT76 and Atheros ath9k-class USB adapters are the usual starting points
  because they allow injection, but I do not know whether any generic one satisfies R4, or whether the official
  adapter's firmware can run on a generic MT76 card. Unknowns: whether a stock `hostapd` can do it (the controller
  appeared to skip 802.11 authentication, which hostapd expects), and the ACK budget (SIFS, about 10 microseconds), which user space cannot meet.
- **Other microcontrollers:** any chip meeting R1-R9. ESP32 variants with 5 GHz (C5) might reach the 5 GHz
  channels; not tried.
- **Simplest working path:** use the real Xbox Wireless Adapter with the xone/xow drivers, or Bluetooth.

## 10. Measurements from the ESP32-S3 bridge [O]

- Input reports: ~100-118 per second while a stick is moved; about 99% of gaps between reports under 40 ms,
  ~1 stall of 40+ ms per second.
- 17-25% of GIP sequence numbers are never seen by the host. Radio tuning did not change this; whether the
  controller drops them itself or they are lost over the air is **not established**. [?]
- A second controller joined and stayed connected alongside the first.

## 11. Open questions

1. Why does the controller ignore rumble over this link? (security handshake? ordering? flags?)
2. What does vendor command `0x0E` carry, and does acknowledging it change controller behavior?
3. What are the beacon constants `01 10` and `a5 30 99`, and the unknown ANNOUNCE field?
4. What is GIP command `0x1F`?
5. Do other controllers and firmware versions need encryption before sending input?
6. Is 802.11 authentication really absent?
7. Does anything beyond the pair flag change how a controller picks a dongle?

## 12. References

- xone (Linux kernel driver): https://github.com/dlundqvist/xone , original by medusalix: https://github.com/medusalix/xone
- xow (user-space driver, protocol notes): https://github.com/medusalix/xow
- OpenWrt/Linux `mt76` driver (register-level work on the dongle's chip)
- Colin Munro, Xbox 360 wireless receiver notes (a different, older protocol): https://tattiebogle.net/ProjectRoot/Xbox360Controller/WirelessUsbInfo
- This repository: `src/radio.cpp` (beacon/ACK/frame handling), `src/gip.cpp` (GIP state machine), `src/xinput.cpp`,
  `src/hid.cpp` (USB side)
