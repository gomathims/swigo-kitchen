#ifndef WIFICONFIGURATION_H
#define WIFICONFIGURATION_H

#include <WiFi.h>
// #include <ETH.h>
#include <SPI.h>
// #include <WebServer_ESP32_SC_W5500.h> 
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include "preference_management.h"
#include "mqtt.h"
#include "api.h"

// Forward declaration for BLE provisioning state
extern bool bleProvisioningActive;

#define DEFAULT_DEVICE_NAME "SwiGOss"
#define DEFAULT_DEVICE_PIN "1234"
#define DEFAULT_AP_PASSWORD "SuperSmart"
#define ADMIN_PASSWORD "EVIQs3@ms33"
#define SPECIAL_ADMIN_PASSWORD "S3@Alak"

#define TIMEZONE_OFFSET 330


extern char ssidBuffer[21];
extern char wifiPassBuffer[21];
extern char apPassBuffer[21];
extern char switchName[21];
extern char devicePin[9];
extern int16_t deviceTZOffset;


extern volatile char wiFiMode;
extern char devicePin[9];
extern bool isNameSet;
extern bool isAPPassSet;
extern bool isWiFiSet;
extern volatile bool isWifiChanged;
extern bool ethernetEnabled;
extern uint8_t countMQTTMessagesAfterRelayOff;
extern unsigned long switchTimeAP;
extern IPAddress lastIP;
extern unsigned long lastMdnsRestart;
extern volatile bool parallelAPModeEnabled;  // Track if parallel AP mode is active
extern unsigned long parallelAPDisconnectCountdown;  // When parallel AP mode was enabled
extern volatile unsigned long removeWiFiRequestedAt;  // Pending web-portal request to clear WiFi config

extern IPAddress staticIP;
extern IPAddress gateway;
extern IPAddress subnet;
extern IPAddress primaryDNS;
extern IPAddress secondaryDNS;

//For ethenet
// extern byte mac[];
// extern SPIClass spi;
// extern ESP32_W5500 eth;
//extern ETHClass eth;

char* getSwitchName();
char getWiFiMode();
void startAPMode();
void initiateWifiSettings();
void switchToWifi();
void checkWifiConnection();
// Drains deferred WiFi-event work (mDNS restart, MQTT backoff reset) on a
// task with a real stack. Call regularly from core0Task.
void serviceWiFiEventDeferred();
// Re-creates the STA connection / softAP that startBLEProvisioning() tore down.
// Called when a BLE session ends without a restart (stop button, timeout).
void restoreWiFiAfterBLE();
void enableParallelAPMode();  // Enable AP mode while maintaining WiFi connection
void disableParallelAPMode();  // Disable AP mode and return to WiFi-only mode

String formatMacAddress(uint64_t mac);
void disableWiFi();
JsonDocument getWiFiSettings();
void generateDynamicMac(byte* mac,char);
// #ifdef LED_RGB_FLOATRELAY
void checkEthernetConnection();
// #endif

#endif
