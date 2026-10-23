#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

// Forward declarations for NimBLE types to avoid including NimBLE headers in this file
class NimBLEServer;
class NimBLECharacteristic;

#include "wifisettings.h"
#include "preference_management.h"

// BLE Service and Characteristic UUIDs
#define BLE_SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_WIFI_CHAR_UUID      "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_STATUS_CHAR_UUID    "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"

// BLE Timeout in milliseconds (10 minutes)
#define BLE_PROVISIONING_TIMEOUT (10 * 60 * 1000)

// Option to skip BLE cleanup and just restart (safer for some boards)
// Set to 1 to skip cleanup, 0 to properly stop BLE before restart
#define BLE_SKIP_CLEANUP_ON_RESTART 0

// BLE provisioning states
enum BLEProvisioningState {
    BLE_IDLE = 0,
    BLE_ADVERTISING = 1,
    BLE_CONNECTED = 2,
    BLE_PROVISIONING_SUCCESS = 3,
    BLE_PROVISIONING_FAILED = 4,
    BLE_TIMEOUT = 5
};

// Global variables
extern bool bleProvisioningActive;
extern unsigned long bleProvisioningStartTime;
extern BLEProvisioningState bleProvisioningState;
extern bool bleProvisioningComplete;  // Flag to indicate provisioning completed successfully
extern String bleProvisioningErrorCode;  // Error code to send from main loop
extern NimBLEServer* pBLEServer;
extern NimBLECharacteristic* pStatusCharacteristic;

// Function declarations
void initBLE();
void startBLEProvisioning();
// restoreWiFi defaults to true: every exit path except "about to restart"
// needs the WiFi/AP that startBLEProvisioning() shut down brought back.
void stopBLEProvisioning(bool restoreWiFi = true);
void checkBLETimeout();
String processBLEProvisioningData(const char* jsonData);
String getBLEDeviceName();

#endif // BLE_PROVISIONING_H
