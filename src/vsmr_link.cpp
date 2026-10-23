#include "vsmr_link.h"
#include "vsmr_link_proto.h"
#include "logdebug.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------------
//  Tunables
// ---------------------------------------------------------------------
#define BEACON_INTERVAL_MS   1000UL    // must be < VSMR's per-channel LISTEN dwell
#define RX_FRESH_MS         12000UL    // no DATA for this long -> link considered down
#define PAIRING_WINDOW_MS  300000UL    // Button-1 pairing window: beacon for 5 minutes
#define VSMR_CHANGE_DECI       20      // per-phase step (2.0 V) that flags an immediate publish

static const uint8_t BCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------------
//  Config. The enable flag is persisted centrally by save3PhaseSettings
//  ("Admin-config" namespace); this module only holds the runtime state.
// ---------------------------------------------------------------------
static bool linkEnabled = false;

// ---------------------------------------------------------------------
//  Runtime
// ---------------------------------------------------------------------
static bool espNowUp = false;
static bool bcastPeerAdded = false;
static unsigned long lastBeacon = 0;
// Button-1 pairing window start (0 = no window open). Beaconing happens ONLY
// while this window is open; it closes on expiry or once the VSMR is heard.
static volatile unsigned long pairingStartMs = 0;

static inline bool pairingWindowOpen() {
    return pairingStartMs != 0 && (millis() - pairingStartMs) < PAIRING_WINDOW_MS;
}

// Paired VSMR MAC filter. allowedSet=false => unpaired: DATA is ignored until
// a Button-1 pairing window learns a companion.
static uint8_t allowedMac[6] = {0};
static volatile bool allowedSet = false;
// Set by the RX callback when a VSMR MAC was auto-learned during a pairing
// window; serviceVsmrLink persists it (NVS writes stay out of the WiFi task).
static volatile bool macLearnedPending = false;

// Pairing handshake: a HELLO sets these so serviceVsmrLink unicasts a WELCOME
// back (peer-add + esp_now_send stay out of the WiFi-task RX callback).
static volatile bool    helloReplyPending = false;
static volatile uint8_t helloReplyMac[6]  = {0};   // where to unicast the WELCOME
static volatile uint8_t helloReplyStatus  = 0;     // 1 = accept, 0 = reject

// Latest VSMR readings (written from RX callback; small + volatile).
static volatile uint16_t rxRDeci = 0, rxYDeci = 0, rxBDeci = 0;
static volatile uint8_t  rxFlags = 0, rxRelay = 0;
static volatile unsigned long rxLastMs = 0;
// Bumped by the RX callback when a frame differs materially from the previous
// one (see onEspNowRecv). Consumers remember the last count they published and
// send immediately when it moves - no clear needed, so two independent
// consumers (local UI, cloud) cannot race each other.
static volatile uint32_t changeCount = 0;

// ---------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------
// Parse "AA:BB:CC:DD:EE:FF" (also accepts '-' separators) into 6 bytes.
static bool parseMac(const char *s, uint8_t out[6]) {
    if (!s) return false;
    int vals[6];
    if (sscanf(s, "%x:%x:%x:%x:%x:%x", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) != 6 &&
        sscanf(s, "%x-%x-%x-%x-%x-%x", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (vals[i] < 0 || vals[i] > 0xFF) return false;
        out[i] = (uint8_t)vals[i];
    }
    return true;
}
static void addBroadcastPeer() {
    if (bcastPeerAdded) return;
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, BCAST_MAC, 6);
    p.channel = 0;               // 0 = current (our STA) channel
    p.encrypt = false;
    p.ifidx   = WIFI_IF_STA;     // Supersmart is a station; beacon out the STA iface
    esp_now_add_peer(&p);
    bcastPeerAdded = true;
}

// A device's STA and AP MACs differ by 1 (48-bit): AP = STA + 1. We accept the
// paired MAC and BOTH neighbours (+/-1), so the filter holds regardless of which
// interface the VSMR transmits on and which of the pair was entered/learned.
static void macPlus1(const uint8_t *in, uint8_t *out) {
    memcpy(out, in, 6);
    for (int i = 5; i >= 0; i--) if (++out[i] != 0) break;   // +1 with carry
}
static void macMinus1(const uint8_t *in, uint8_t *out) {
    memcpy(out, in, 6);
    for (int i = 5; i >= 0; i--) if (out[i]-- != 0) break;   // -1 with borrow
}

// RX callback (WiFi-task context): validate DATA + store it. No ACK, no reply.
static void onEspNowRecv(VLINK_RECV_SIG) {
    if (!vlink_ver_ok(data, len)) return;                 // "VS-1" sync check
    const vlink_prefix_t *pre = (const vlink_prefix_t *)data;

    // ---- Pairing handshake: HELLO -> WELCOME (accept/reject) --------------
    // VSMR won't commit to us (or send DATA) until we accept, so this is where
    // pairing is actually decided. We accept a HELLO whose MAC matches our
    // stored companion (+/-1 sibling) at ANY time - that also recovers a lost
    // WELCOME - and learn a NEW MAC only while a Button-1 pairing window is
    // open. Any other HELLO is rejected (status 0), so a Supersmart configured
    // for a different VSMR never adopts a stray one.
    if (pre->type == VLINK_HELLO && len >= (int)sizeof(vlink_handshake_t)) {
        const vlink_handshake_t *h = (const vlink_handshake_t *)data;
        const uint8_t *vmac = h->mac;   // VSMR's own STA MAC (canonical identity)

        bool match = false;
        if (allowedSet) {
            uint8_t up[6], dn[6];
            macPlus1(allowedMac, up);
            macMinus1(allowedMac, dn);
            match = memcmp(vmac, allowedMac, 6) == 0 ||
                    memcmp(vmac, up, 6) == 0 ||
                    memcmp(vmac, dn, 6) == 0;
        }

        uint8_t status;
        if (match) {
            status = 1;                                   // known/paired VSMR -> accept
        } else if (!allowedSet && pairingWindowOpen()) {
            memcpy(allowedMac, vmac, 6);                  // learn on the handshake
            allowedSet = true;
            macLearnedPending = true;
            status = 1;
            Serial.printf("[VLINK] HELLO: learned companion VSMR %02X:%02X:%02X:%02X:%02X:%02X\n",
                          vmac[0], vmac[1], vmac[2], vmac[3], vmac[4], vmac[5]);
        } else {
            status = 0;                                   // configured for another VSMR / no window -> reject
        }

        // Reply is unicast back to the HELLO's source MAC; sent from
        // serviceVsmrLink to keep peer/NVS churn out of the WiFi task.
        memcpy((void *)helloReplyMac, VLINK_RECV_SRCMAC, 6);
        helloReplyStatus  = status;
        helloReplyPending = true;
        Serial.printf("[VLINK] HELLO from %02X:%02X:%02X:%02X:%02X:%02X -> WELCOME status=%u\n",
                      vmac[0], vmac[1], vmac[2], vmac[3], vmac[4], vmac[5], status);
        return;
    }

    if (pre->type != VLINK_DATA || len < (int)sizeof(vlink_data_t)) return;

    const uint8_t *src = VLINK_RECV_SRCMAC;

    // Auto-learn: no paired MAC configured and the pairing window is open
    // -> adopt this VSMR as the companion. From here on the filter is
    // armed with its MAC; persistence happens in serviceVsmrLink.
    if (!allowedSet && pairingWindowOpen()) {
        memcpy(allowedMac, src, 6);
        allowedSet = true;
        macLearnedPending = true;
        Serial.printf("[VLINK] auto-learned companion VSMR %02X:%02X:%02X:%02X:%02X:%02X\n",
                      src[0], src[1], src[2], src[3], src[4], src[5]);
    }

    // No paired VSMR (blank/cleared MAC) and no pairing window open: ignore ALL
    // DATA. Voltages are only ever read from a paired companion - pair via the
    // Button-1 hold (5 s), which learns and saves the first VSMR heard.
    if (!allowedSet) {
        Serial.printf("[VLINK] DATA from %02X:%02X:%02X:%02X:%02X:%02X ignored (no paired VSMR - hold Button 1 to pair)\n",
                      src[0], src[1], src[2], src[3], src[4], src[5]);
        return;
    }

    // MAC filter: accept the paired VSMR, or either STA/AP sibling (paired +/-1),
    // so it doesn't matter whether the STA or AP MAC was entered/learned and
    // which interface the VSMR transmits on.
    {
        uint8_t up[6], dn[6];
        macPlus1(allowedMac, up);
        macMinus1(allowedMac, dn);
        if (memcmp(src, allowedMac, 6) != 0 &&
            memcmp(src, up, 6) != 0 &&
            memcmp(src, dn, 6) != 0) {
            Serial.printf("[VLINK] DATA from %02X:%02X:%02X:%02X:%02X:%02X REJECTED (paired MAC %02X:%02X:%02X:%02X:%02X:%02X)\n",
                          src[0], src[1], src[2], src[3], src[4], src[5],
                          allowedMac[0], allowedMac[1], allowedMac[2],
                          allowedMac[3], allowedMac[4], allowedMac[5]);
            return;   // not our paired VSMR (nor its STA/AP sibling)
        }
    }

    const vlink_data_t *d = (const vlink_data_t *)data;
    // Change detection BEFORE the copy: a >= 2 V step on any SINGLE phase (an
    // average can hide equal-and-opposite swings), or any flags/relay change,
    // bumps changeCount -> consumers push to local UI + cloud immediately.
    if (abs((int)d->rVoltDeci - (int)rxRDeci) >= VSMR_CHANGE_DECI ||
        abs((int)d->yVoltDeci - (int)rxYDeci) >= VSMR_CHANGE_DECI ||
        abs((int)d->bVoltDeci - (int)rxBDeci) >= VSMR_CHANGE_DECI ||
        d->statusFlags != rxFlags ||
        (d->relayOn != 0) != (rxRelay != 0)) {
        changeCount++;
    }
    rxRDeci  = d->rVoltDeci;
    rxYDeci  = d->yVoltDeci;
    rxBDeci  = d->bVoltDeci;
    rxFlags  = d->statusFlags;
    rxRelay  = d->relayOn;
    rxLastMs = millis();
    // No Serial.printf here: this callback runs on the WiFi driver task, and
    // DATA arrives continuously once linked (not just during pairing). A
    // blocking UART write on every frame was stalling that task often enough
    // to miss AsyncTCP/WebSocket ping-pong deadlines ("Connection to the
    // device is lost") and to lag the rest of the system's Serial-based
    // logging. Tracing now happens out-of-band in serviceVsmrLink(), gated on
    // changeCount so it only fires when a frame actually differs.
}

static void sendBeacon() {
    vlink_beacon_t b;
    memcpy(b.ver, VSMR_LINK_VER, VSMR_LINK_VER_LEN);
    b.type    = VLINK_BEACON;
    b.channel = WiFi.channel();              // our current (router-fixed) channel
    esp_wifi_get_mac(WIFI_IF_STA, b.mac);    // MAC VSMR must unicast DATA to
    esp_err_t err = esp_now_send(BCAST_MAC, (const uint8_t *)&b, sizeof(b));
    Serial.printf("[VLINK] Beacon sent (ch=%u, my MAC %02X:%02X:%02X:%02X:%02X:%02X)%s\n",
                  b.channel, b.mac[0], b.mac[1], b.mac[2], b.mac[3], b.mac[4], b.mac[5],
                  err == ESP_OK ? "" : " - send FAILED");
}

// Unicast a WELCOME (handshake reply) back to a VSMR that just sent HELLO.
// Registers it as a peer first (unicast needs one) and replies out the STA
// iface so the source MAC matches the beacon MAC the VSMR is confirming against.
static void sendWelcome(const uint8_t *dst, uint8_t status) {
    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, dst, 6);
    p.channel = 0;                  // 0 = current (our STA) channel
    p.encrypt = false;
    p.ifidx   = WIFI_IF_STA;
    esp_now_del_peer(dst);          // refresh if already present
    esp_now_add_peer(&p);

    vlink_handshake_t w;
    memcpy(w.ver, VSMR_LINK_VER, VSMR_LINK_VER_LEN);
    w.type   = VLINK_WELCOME;
    esp_wifi_get_mac(WIFI_IF_STA, w.mac);   // our STA MAC
    w.status = status;
    esp_err_t err = esp_now_send(dst, (const uint8_t *)&w, sizeof(w));
    Serial.printf("[VLINK] WELCOME status=%u -> %02X:%02X:%02X:%02X:%02X:%02X%s\n",
                  status, dst[0], dst[1], dst[2], dst[3], dst[4], dst[5],
                  err == ESP_OK ? "" : " - send FAILED");
}

static void espNowStart() {
        Serial.println("[VLINK] espNowStart 111111111");

    if (espNowUp) return;
        Serial.println("[VLINK] espNowStart 22222222");

    // ESP-NOW frames arriving during Wi-Fi modem sleep are dropped: keep the
    // radio awake while the VSMR link is active.
    WiFi.setSleep(false);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[VLINK] esp_now_init failed");
        return;
    }
    esp_now_register_recv_cb(onEspNowRecv);
    addBroadcastPeer();
    espNowUp = true;
    Serial.println("[VLINK] ESP-NOW up (VSMR link)");
}

static void espNowStop() {
    if (!espNowUp) return;
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    espNowUp = false;
    bcastPeerAdded = false;
}

// ---------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------
bool vsmrLinkEnabled() { return linkEnabled; }

bool vsmrLinkAlive() {
    return rxLastMs != 0 && (millis() - rxLastMs) <= RX_FRESH_MS;
}

bool vsmrLinkGetVoltages(float &r, float &y, float &b, uint8_t &statusFlags, bool &relayOn) {
    r = rxRDeci / 10.0f;
    y = rxYDeci / 10.0f;
    b = rxBDeci / 10.0f;
    statusFlags = rxFlags;
    relayOn = rxRelay != 0;
    return vsmrLinkAlive();
}

bool vsmrLinkGetAvgVoltage(float &avg) {
    // deci-volts summed, one divide: /3 phases and /10 deci in a single step
    avg = (rxRDeci + rxYDeci + rxBDeci) / 30.0f;
    return vsmrLinkAlive();
}

uint32_t vsmrLinkChangeCount() { return changeCount; }

void vsmrLinkSetPeerMac(const char *macStr) {
        Serial.println("[VLINK] LinkSetPeer 111111111");
    
    uint8_t m[6];
    if (macStr && parseMac(macStr, m)) {
        memcpy(allowedMac, m, 6);
        allowedSet = true;
    } else {
        allowedSet = false;   // "" / invalid -> unpaired (DATA ignored until Button-1 pairing)
    }
}

String vsmrLinkPeerMacStr() {
    if (!allowedSet) return "";
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             allowedMac[0], allowedMac[1], allowedMac[2],
             allowedMac[3], allowedMac[4], allowedMac[5]);
    return String(buf);
}

void initVsmrLink(bool enabled, const char *peerMac) {
        Serial.println("[VLINK] initVsmrLink 111111111");

    linkEnabled = enabled;
    vsmrLinkSetPeerMac(peerMac);
    if (linkEnabled) espNowStart();
}

void setVsmrLinkEnabled(bool en) {
    if (en == linkEnabled) return;
    linkEnabled = en;
    if (en) espNowStart();   // runtime only; persistence via save3PhaseSettings
    else    espNowStop();
}

void vsmrLinkStartPairing() {
    unsigned long now = millis();
    // Open the pairing window: beacon so a VSMR can (re)learn our channel/MAC.
    // A saved pairing is NEVER overwritten here - auto-learn runs only while no
    // MAC is saved (clear it from techsetup to pair a different VSMR). The
    // freshness stamp is reset so beacons go out even if data was flowing;
    // fresh DATA from the accepted MACs closes the window again.
    rxLastMs = 0;
    pairingStartMs = now ? now : 1;   // 0 is the "closed" sentinel
    Serial.printf("[VLINK] pairing window started (beaconing up to 5 min)%s\n",
                  allowedSet ? " - keeping saved pairing" : " - will learn first VSMR heard");
}

// Called from Core0task
void serviceVsmrLink() {
    if (!linkEnabled || !espNowUp) return;

    // // Trace VSMR frames here instead of in onEspNowRecv (WiFi task) - only
    // // when a frame actually changed something, so this stays near-silent
    // // once linked rather than printing on every ~1 Hz DATA frame.
    // static uint32_t lastLoggedChangeCount = 0;
    // uint32_t cc = changeCount;
    // if (cc != lastLoggedChangeCount) {
    //     lastLoggedChangeCount = cc;
    //     uint16_t r = rxRDeci, y = rxYDeci, b = rxBDeci;
    //     uint8_t flags = rxFlags, relay = rxRelay;
    //     Serial.printf("[VLINK] DATA changed: R=%u.%uV Y=%u.%uV B=%u.%uV flags=0x%02X relay=%u\n",
    //                   r / 10, r % 10, y / 10, y % 10, b / 10, b % 10, flags, relay);
    // }

    // Answer a pairing handshake. Done BEFORE any pairing-window / silent
    // return so a paired VSMR that lost its WELCOME still gets one back (its
    // MAC matches, so status=1) and re-locks - even outside a pairing window.
    if (helloReplyPending) {
        helloReplyPending = false;
        uint8_t dst[6];
        memcpy(dst, (const void *)helloReplyMac, 6);
        sendWelcome(dst, helloReplyStatus);
    }

    // Persist an auto-learned companion MAC (same key save3PhaseSettings uses,
    // so it is read back by read3PhaseSettings on the next boot).
    if (macLearnedPending) {
        macLearnedPending = false;
        Preferences prefs;
        prefs.begin("Admin-config", false);
        prefs.putString("vsmrMac", vsmrLinkPeerMacStr());
        prefs.end();
        Serial.printf("[VLINK] companion VSMR MAC saved: %s\n", vsmrLinkPeerMacStr().c_str());
        // "Learned" marks the pairing-window path, as opposed to a MAC typed in
        // on the techsetup page - the two are indistinguishable afterwards, and
        // which one happened is the first thing you want when a device turns out
        // to be paired to the wrong VSMR. Logged here rather than at the learn
        // site because this runs on Core0task, not the WiFi RX callback.
        char dbgBuf[DEBUG_MESSAGE_SIZE];
        snprintf(dbgBuf, sizeof(dbgBuf), "VSMR companion learned during pairing. mac=%s",
                 vsmrLinkPeerMacStr().c_str());
        logDebugAction(DebugAction::ADMIN_3PHASE_SETTINGS_SUCCESS, dbgBuf);
    }

    // Beacon ONLY during a manual pairing window (Button 1 held 5 s). We do NOT
    // auto-beacon just because data stopped: a paired VSMR reconnects on its own
    // via PROBE (send to the known MAC), so beaconing stays gated on a deliberate
    // button press -> controlled pairing.
    if (pairingStartMs == 0) return;                    // no window open -> silent
    if (!pairingWindowOpen()) {                         // 5 minutes elapsed -> give up
        pairingStartMs = 0;
        Serial.println("[VLINK] pairing window expired (5 min, no VSMR heard)");
        return;
    }
    if (vsmrLinkAlive()) {                              // VSMR heard -> paired, stop
        pairingStartMs = 0;
        Serial.println("[VLINK] VSMR heard - pairing window closed");
        return;
    }

    unsigned long now = millis();
    if (now - lastBeacon >= BEACON_INTERVAL_MS) {
        lastBeacon = now;
        sendBeacon();
    }
}
