#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>
#include <ArduinoJson.h>

// OTA state machine
enum OTAState : uint8_t {
    OTA_IDLE = 0,
    OTA_DOWNLOADING = 1,
    OTA_UPDATING = 2,
    OTA_UPDATED = 3,
    OTA_FAILED = 4
};

// Firmware info received from ThingsBoard shared attributes
struct FirmwareInfo {
    char title[64];
    char version[32];
    uint32_t size;
    char checksum[65];      // MD5=32 hex, SHA256=64 hex
    char checksumAlg[16];   // "MD5", "SHA256", etc.
    bool available;         // true when fw_* attributes received and version is newer
};

extern FirmwareInfo targetFirmware;
extern volatile OTAState otaState;
extern volatile uint8_t otaProgress;    // 0-100
extern char otaErrorMessage[128];

// Parse fw_* shared attributes from ThingsBoard push or attribute response
void processOTASharedAttributes(JsonDocument& doc);

// Handle startOTA RPC command
void handleStartOTARpc(const String& requestId);

// Perform the actual firmware download and flash (blocking)
bool performOTAUpdate();

// Compare target firmware version with current
bool isOTAUpdateAvailable();

// Publish OTA-related client attributes (fw_update_available, target_fw_version, fw_state)
void publishOTAAttributes();

// Request existing fw_* shared attributes from ThingsBoard on connect
void requestOTASharedAttributes();

// Initialize OTA module
void initOTA();

// Confirm current firmware is valid (call in setup after successful boot)
void confirmFirmwareValid();

#endif // OTA_UPDATE_H
