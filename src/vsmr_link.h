#ifndef VSMR_LINK_H
#define VSMR_LINK_H

#include <Arduino.h>

// ESP-NOW downlink on Supersmart: receive 3-phase voltages from a VSMR.
//
// Enabled when the "3-phase Motor" tech setup is "3-Phase with VSMR"
// (techsetup value 2). While enabled:
//   * Wi-Fi modem-sleep is turned OFF so ESP-NOW frames are never missed.
//   * A "VS-1" beacon (current channel + this device's STA MAC) is
//     BROADCAST only while no VSMR frame has arrived recently, so a VSMR
//     that doesn't yet know us (or lost our channel) can (re)discover us.
//     Beaconing stops as soon as VSMR frames are flowing - near-zero
//     airtime once linked. No ACK is sent: a dropped sample is superseded
//     by the next one, and VSMR tracks the link via its MAC-layer send cb.
//   * Incoming voltage frames are stored; read them with vsmrLinkGet...().

// Call once from setup() AFTER Wi-Fi is initialised, passing the persisted
// enable flag + paired VSMR MAC (both read via read3PhaseSettings). No-op
// unless enabled. peerMac "" = unpaired: DATA is ignored until a Button-1
// pairing window (5 s hold) learns and saves a companion VSMR.
void initVsmrLink(bool enabled, const char *peerMac);

// Non-blocking housekeeping (beacon when link is down). Call from core0Task.
void serviceVsmrLink();

// Start a manual pairing broadcast: beacon for a short window regardless of
// link state, so a re-learning VSMR can (re)discover this Supersmart. Triggered
// by holding Button 1 for 5 s.
void vsmrLinkStartPairing();

// Runtime enable/disable (starts/stops ESP-NOW). Persistence is handled by
// save3PhaseSettings in the "Admin-config" namespace, NOT here - wire this to
// the techsetup "3p" handler (enable when the value is 2), calling it after
// save3PhaseSettings so the stored flag and runtime state stay in sync.
void setVsmrLinkEnabled(bool en);
bool vsmrLinkEnabled();

// Set the accepted (paired) VSMR MAC at runtime, e.g. "AA:BB:CC:DD:EE:FF".
// "" or an unparseable string clears the pairing: DATA is ignored until
// Button-1 pairing learns a new companion.
void vsmrLinkSetPeerMac(const char *macStr);

// The accepted VSMR MAC as "AA:BB:..", or "" when the filter is off.
String vsmrLinkPeerMacStr();

// True if a VSMR voltage frame arrived within the freshness window.
bool vsmrLinkAlive();

// Latest received phase voltages (Volts) + status. Returns true if fresh
// (link alive). Any out-param reference is always written when true.
bool vsmrLinkGetVoltages(float &r, float &y, float &b,
                         uint8_t &statusFlags, bool &relayOn);

// Average of the three phase voltages (Volts). Returns true if fresh
// (link alive); avg is always written.
bool vsmrLinkGetAvgVoltage(float &avg);

// Monotonic counter, bumped when a received frame steps >= 2 V on any single
// phase or changes flags/relay state. Consumers remember the last value they
// published and push immediately (local UI / cloud) when it moves.
uint32_t vsmrLinkChangeCount();

#endif // VSMR_LINK_H
