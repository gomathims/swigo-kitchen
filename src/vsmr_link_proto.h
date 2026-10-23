#ifndef VSMR_LINK_PROTO_H
#define VSMR_LINK_PROTO_H

// =====================================================================
//  VSMR <-> Supersmart ESP-NOW wire protocol  (minimal).
//
//  KEEP THIS FILE BYTE-FOR-BYTE IDENTICAL IN BOTH PROJECTS:
//     VSMR_ESP/src/vsmr_link_proto.h
//     Swigo-Gen2/src/vsmr_link_proto.h
//
//  Deliberately tiny (like EVIQ-3PH): NO magic, NO CRC, NO sequence
//  number, NO application ACK.
//    * Frame integrity  -> already guaranteed by the 802.11 FCS; corrupt
//                          frames never reach the receive callback.
//    * Delivery / channel-loss detection -> the ESP-NOW *send callback*
//                          (MAC-layer unicast ACK: SUCCESS/FAIL). Free.
//    * The 4-byte version string "VS-1" doubles as the sync word so a
//      listening VSMR won't adopt stray broadcast traffic as a beacon.
//
//  Roles:
//    - Supersmart (Wi-Fi station) BROADCASTS a beacon ONLY while it is
//      not currently hearing VSMR (link down / not yet found). The beacon
//      advertises its current channel + the MAC to unicast back to.
//    - VSMR (no Wi-Fi needed) auto-learns that MAC+channel once, then
//      unicasts voltage DATA and tracks the link via the send callback.
//      If sends start failing (router hopped channel) it hop-and-probes
//      the known MAC across channels to re-lock - no beacon wait needed.
// =====================================================================

#include <Arduino.h>   // ESP_ARDUINO_VERSION / ESP_ARDUINO_VERSION_VAL
#include <stdint.h>
#include <string.h>    // memcmp / memcpy (used by vlink_ver_ok and callers)
#include <esp_now.h>

// 4-char protocol version, doubles as the beacon sync word.
// NOTE: exactly 4 bytes on the wire (no NUL). Compared with memcmp(.,.,4).
#define VSMR_LINK_VER      "VS-1"
#define VSMR_LINK_VER_LEN  4

// Frame types (prefix.type)
enum {
    VLINK_BEACON  = 1,   // Supersmart -> broadcast : advertise channel + reply MAC
    VLINK_DATA    = 2,   // VSMR       -> Supersmart : voltage payload
    VLINK_HELLO   = 3,   // VSMR       -> Supersmart : pairing handshake request
    VLINK_WELCOME = 4,   // Supersmart -> VSMR       : handshake reply (accept/reject)
};

#pragma pack(push, 1)

// Common 5-byte prefix at the start of every frame. Used to validate the
// version/sync word and read the type before casting to the full struct.
typedef struct {
    char    ver[VSMR_LINK_VER_LEN];   // "VS-1"
    uint8_t type;                     // VLINK_*
} vlink_prefix_t;

// Supersmart -> broadcast. Tells VSMR where/how to reach it.
typedef struct {
    char    ver[VSMR_LINK_VER_LEN];   // "VS-1"
    uint8_t type;                     // VLINK_BEACON
    uint8_t channel;                  // Supersmart's current Wi-Fi channel (1..13)
    uint8_t mac[6];                   // MAC VSMR must unicast DATA to (Supersmart STA)
} vlink_beacon_t;

// VSMR -> Supersmart. Three phase voltages + status, tenths of a volt.
typedef struct {
    char     ver[VSMR_LINK_VER_LEN];  // "VS-1"
    uint8_t  type;                    // VLINK_DATA
    uint16_t rVoltDeci;               // R phase voltage x10 (e.g. 2301 = 230.1 V)
    uint16_t yVoltDeci;               // Y phase voltage x10
    uint16_t bVoltDeci;               // B phase voltage x10
    uint8_t  statusFlags;             // mirror of VSMR RYBFLAG (fault/health/seq bits)
    uint8_t  relayOn;                 // 1 = VSMR output relay energised
} vlink_data_t;

// Pairing handshake, used in BOTH directions (type distinguishes them):
//   * VSMR -> Supersmart  VLINK_HELLO   : "pair with me", carries VSMR's STA MAC.
//   * Supersmart -> VSMR  VLINK_WELCOME : reply, carries Supersmart's STA MAC.
// It closes the confirmation hole in the beacon-only flow: VSMR commits to a
// Supersmart ONLY after an explicit accept, so it can never latch onto a
// Supersmart that is configured for a different VSMR (that one replies reject).
//   status:  meaningful only in WELCOME -> 1 = accepted/paired, 0 = rejected.
//            HELLO always leaves it 0.
// mac:       the sender's own STA MAC, the canonical identity each side stores
//            (the receive-callback source MAC may be the AP +/-1 sibling).
typedef struct {
    char    ver[VSMR_LINK_VER_LEN];   // "VS-1"
    uint8_t type;                     // VLINK_HELLO or VLINK_WELCOME
    uint8_t mac[6];                   // sender's own STA MAC
    uint8_t status;                   // WELCOME: 1=accepted, 0=rejected; HELLO: 0
} vlink_handshake_t;

#pragma pack(pop)

// True if a received buffer carries our version/sync word.
static inline bool vlink_ver_ok(const uint8_t *data, int len) {
    return len >= (int)sizeof(vlink_prefix_t) &&
           memcmp(data, VSMR_LINK_VER, VSMR_LINK_VER_LEN) == 0;
}

// ESP-NOW receive-callback signature changed in Arduino-ESP32 core 3.x
// (IDF5): the sender MAC moved from a bare `const uint8_t*` to a field of
// `esp_now_recv_info_t`. These macros let one callback body compile on both.
#if defined(ESP_ARDUINO_VERSION) && (ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0))
    #define VLINK_RECV_SIG      const esp_now_recv_info_t *rxinfo, const uint8_t *data, int len
    #define VLINK_RECV_SRCMAC   (rxinfo->src_addr)
#else
    #define VLINK_RECV_SIG      const uint8_t *rxmac, const uint8_t *data, int len
    #define VLINK_RECV_SRCMAC   (rxmac)
#endif

#endif // VSMR_LINK_PROTO_H
