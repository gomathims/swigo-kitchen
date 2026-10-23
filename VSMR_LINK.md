# VSMR ↔ Supersmart ESP-NOW Link (Supersmart side)

Branch: `VSMR-integration`

## Purpose
Supersmart receives 3-phase voltages from a **VSMR** unit over **ESP-NOW** while
staying connected to Wi-Fi. The link is enabled from the techsetup page and
secured by a **manually-entered VSMR MAC filter**.

## New files
- **`src/vsmr_link_proto.h`** — shared wire protocol (byte-identical copy exists
  in the VSMR project). Minimal by design: 4-byte version/sync word `"VS-1"`,
  **no magic / CRC / ACK / sequence number**. Four frame types:
  - `VLINK_BEACON` — Supersmart → broadcast: advertises current channel + its
    STA MAC (so a VSMR can discover it).
  - `VLINK_DATA` — VSMR → Supersmart: 3 phase voltages in **deci-volts**
    (×10, e.g. 2301 = 230.1 V) + status flags + relay state.
  - `VLINK_HELLO` — VSMR → Supersmart: **pairing handshake request** (carries the
    VSMR's own STA MAC).
  - `VLINK_WELCOME` — Supersmart → VSMR: **handshake reply** — carries Supersmart's
    STA MAC + a `status` byte (`1` = accepted/paired, `0` = rejected).
  - Includes a core-2.x/3.x receive-callback signature shim.
- **`src/vsmr_link.h` / `src/vsmr_link.cpp`** — the receiver module. Public API:
  - `void initVsmrLink(bool enabled, const char *peerMac)` — call once in
    `setup()` after Wi-Fi is up.
  - `void serviceVsmrLink()` — non-blocking housekeeping; call from `core0Task`.
  - `void setVsmrLinkEnabled(bool)` / `bool vsmrLinkEnabled()` — runtime enable
    (persistence is handled externally, see below).
  - `void vsmrLinkSetPeerMac(const char*)` / `String vsmrLinkPeerMacStr()` —
    MAC filter set/get.
  - `bool vsmrLinkAlive()` — true if a valid frame arrived within the window.
  - `bool vsmrLinkGetVoltages(float &r, float &y, float &b, uint8_t &flags, bool &relay)`
    — **read the received voltages here** (returns true if fresh).

## Module behavior
- On enable: `WiFi.setSleep(false)` (ESP-NOW frames are dropped during Wi-Fi
  modem sleep), `esp_now_init`, registers a broadcast peer.
- **Beacon** (channel + own STA MAC) broadcast ~1 Hz **only during a manual
  pairing window** (Button 1 held 5 s; see below). Supersmart does NOT auto-beacon
  on link loss — a paired VSMR reconnects on its own via PROBE (send to the known
  MAC), so outside a pairing window Supersmart is silent.
- **Receive path (DATA):** validates `"VS-1"`, checks it is a DATA frame, then
  applies the **MAC filter** — accepts if `src_addr` equals the configured/paired
  VSMR MAC **or either `±1` sibling**. Stores latest voltages + `rxLastMs`. **No
  ACK is sent** (a dropped sample is superseded by the next). DATA from an
  unpaired/unknown VSMR is **dropped** — pairing happens via the handshake, not
  by learning from DATA.

  The `±1` matters because VSMR (Option A) transmits via its **STA interface**, so
  its source is the **STA MAC**, while a STA/AP MAC pair differs by 1. Accepting
  the neighbours means it doesn't matter whether the STA (`…:30`) or AP (`…:31`)
  MAC was entered/learned — a common cause of "Supersmart receives but rejects."
- **Handshake path (HELLO → WELCOME):** on a `VLINK_HELLO`, Supersmart decides and
  replies with a `VLINK_WELCOME`:
  - HELLO MAC matches the stored companion (±1) → **accept** (`status=1`) at any
    time — this also recovers a **lost WELCOME** (the VSMR just retries).
  - No MAC stored **and** a Button-1 pairing window is open → **learn** that MAC
    (persisted by `serviceVsmrLink`) and **accept**.
  - Anything else (configured for a *different* VSMR, or no window) → **reject**
    (`status=0`), so a stray VSMR is never adopted.
  The reply is unicast from `serviceVsmrLink` (peer-add + `esp_now_send` kept out
  of the WiFi-task RX callback), out the STA iface so its source MAC matches the
  beacon MAC the VSMR is confirming against.

## Button-1 pairing broadcast

Hold **Button 1 (SWITCH_1) alone for 5 s** (all other buttons released) to
broadcast the beacon for a short pairing window. This lets a VSMR that was just
put into learning mode (its own 5 s button) rediscover this Supersmart.

- `checkVsmrPairing()` in `power_monitoring.cpp` (called from the main loop,
  same pattern as `checkFactoryReset` / `checkDisableWiFi`) detects the 5 s hold,
  calls `vsmrLinkStartPairing()`, and lights `vsmrPairLedIndicate` (LED_1 steady
  ~2 s) as feedback. It also sets `ignoreButton1FallOnce` so Button 1's normal
  short-press action does not fire from the same hold.
- `vsmrLinkStartPairing()` opens a **time-based window**: it stamps
  `pairingStartMs = millis()`; `pairingWindowOpen()` is true while
  `millis() - pairingStartMs < PAIRING_WINDOW_MS` (**5 min**). The window is what
  gates *learning a new MAC* — it does **not** gate answering a HELLO from an
  already-paired VSMR (that is accepted any time).
- `serviceVsmrLink()` sends one beacon/second while the window is open. The window
  **closes early** the instant a VSMR frame is heard (`vsmrLinkAlive()`), so a
  successful pair stops beaconing immediately; otherwise it stops after 5 min.

Full re-pair procedure: (optionally clear the MAC in techsetup to pair a *new*
VSMR) → hold the VSMR's button 5 s (it forgets + LISTENs) → hold this Supersmart's
Button 1 5 s (it beacons up to 5 min, or until the VSMR is heard) → the VSMR
sends HELLO, Supersmart learns it + replies WELCOME(accept), both sides pair.

## How the MAC filter works

**Key fact:** every received ESP-NOW frame already carries the **sender's MAC**
— it is in the 802.11 header and delivered to the receive callback for free
(`esp_now_recv_info_t->src_addr` on core 3.x). For **DATA**, Supersmart simply
compares that `src_addr` to the paired VSMR MAC (**or either `±1` sibling**) and
accepts only on a match.

The **handshake** exists for a different reason: it lets Supersmart tell the VSMR
*"you are (not) my VSMR"* so the VSMR only commits to a Supersmart that actually
accepts it — a beacon alone can't do that (it is broadcast; every VSMR hears it).
It is also where the MAC is **learned**: an unpaired Supersmart adopts the MAC
from a **HELLO** while its Button-1 window is open, not from DATA.

```
  VSMR (sender)                         SUPERSMART (receiver)
  no peer yet                           techsetup: "3-Phase with VSMR"
       |                                (blank MAC -> learns in pairing window)
       |                                       |
       |     (1) Button 1 5 s --- beacons -----|  broadcast: {channel, MY mac}
       |<---------------- BEACON ---------------|
       |                                       |
   (2) hold channel, HELLO                     |
       |--(3) HELLO {my STA MAC} ------------->|  window open + unpaired
       |                                       |  (4) LEARN + PERSIST that MAC
       |<----- WELCOME {SS MAC, status=1} -----|      -> accept
   (5) accept -> PERSIST SS MAC, LINK          |
       |                                       |
       |--(6) DATA (unicast to SS) ----------->|  src_addr == paired MAC (±1)?
       |        [802.11 hdr: src = VSMR mac]   |     yes -> accept, store voltages
       |                                (7) heard the VSMR -> STOP beaconing
       |<-------(beacon stops; link up)--------|
       |                                       |
   +-- other VSMR --(HELLO, wrong MAC) ------->|  configured for someone else?
   +--        <----- WELCOME {status=0} -------|     -> reject (VSMR keeps looking)
   +-- rogue  ------(DATA, spoofed MAC) ------>|     MAC filter only -> would pass
```

Notes:
- Learning is now on the **HELLO handshake**, gated to the Button-1 pairing window.
  Once a MAC is stored, HELLOs matching it are answered **any time** (so a lost
  WELCOME self-heals on the VSMR's retry), but a *new* MAC is only ever learned
  inside a window. To pair a different VSMR, clear the MAC in techsetup, then pair.
- Setting the MAC explicitly in techsetup skips learning — Supersmart will accept
  (and answer HELLOs from) exactly that MAC (±1).
- A rogue that spoofs the paired MAC would still pass DATA (MAC filtering only);
  see "Current limitations".

## Modified files
- **`src/preference_management.h` / `.cpp`**
  - `save3PhaseSettings(...)` and `read3PhaseSettings(...)` each gained **two
    trailing params**: `bool vsmrLinkEnabled` and `String vsmrMac`.
  - Persisted in the **`"Admin-config"`** NVS namespace, keys **`vsmrLink`**
    (bool) and **`vsmrMac`** (string).
- **`src/main.cpp`**
  - `read3PhaseSettings(...)` call now also reads `vsmrLinkConfig` +
    `vsmrMacConfig`.
  - `initVsmrLink(vsmrLinkConfig, vsmrMacConfig.c_str())` in `setup()`.
  - `serviceVsmrLink()` called inside `core0Task`.
- **`src/api.cpp`** — `/adminapi/3phasesettings`:
  - `"3p"` is now **tri-state**: `0` = not 3-phase, `1` = 3-phase,
    `2` = 3-phase **with VSMR**.
  - Parses `vsmrMac`; calls `save3PhaseSettings(..., vsmrLinkOn, vsmrMac)`,
    `setVsmrLinkEnabled(vsmrLinkOn)`, `vsmrLinkSetPeerMac(vsmrMac)`.
  - Settings GET returns `"3p"` (2 when enabled) and `"vsmrMac"`.
- **`src/mqtt.cpp`** — MQTT admin-settings path mirrors api.cpp (same tri-state
  parse, save, runtime apply, and `vsmrMac` in the report).
- **`data/techsetup.html`** — option **C "3-Phase with VSMR"** (value `2`) added
  to the 3-phase radio, plus a **"Paired VSMR MAC address"** text input
  (`#vsmr_mac`).
- **`data/scripts/adminscript.js`** — sends `vsmrMac` in the settings payload
  (with `AA:BB:CC:DD:EE:FF` validation) and populates the field on load.
  Note: `web_content.h` / `web_mappings.h` regenerate from `data/` at build via
  the pre-script.

## User flow
Techsetup → Q3 → **C. 3-Phase with VSMR** → enter the VSMR's MAC (read it from
the VSMR's own web portal "Device MAC address"). Enable flag + MAC persist in
`Admin-config`.

## Current limitations / next steps
- **No encryption / HMAC** — identity is MAC-based only. The HELLO/WELCOME
  handshake confirms *acceptance* (and prevents latching onto the wrong
  Supersmart), but it is **not cryptographic**: a rogue that spoofs the paired
  MAC would still pass DATA, and neither side proves its identity with a secret.
- Learning a **new** MAC requires an open Button-1 pairing window; a stored MAC is
  only changed by clearing it in techsetup (or entering a new one).
- Reconnection of an already-paired link relies on the VSMR side (its PROBE +
  MAC-layer send callback); Supersmart only beacons during a deliberate Button-1
  pairing window, never automatically on silence.

## Build status
`envS3C1` builds successfully (Flash ~73.2%).
