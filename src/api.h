#ifndef API_H
#define API_H

#include <ArduinoJson.h>

#include <FS.h>
#include <Update.h>
#include <sys/time.h>
#include <time.h>

// #include "AsyncJson.h"
#include "buttontiming.h"
#include "buttonpress.h"
#include "logdata.h"
#include "logdebug.h"
#include "power_monitoring.h"
#include "preference_management.h"
#include "timesettings.h"
// #include "web_content.h"
// #include "web_mappings.h"
#include "mqtt.h"
#include "websocketsettings.h"
#include "wifisettings.h"
#include <ESPAsyncWebServer.h>
#include "validations.h"

// Global variables
extern AsyncWebServer server;
extern uint8_t relayState;
extern bool isFirstTime;
extern bool isScheduleSet;
extern bool isNameSet;
extern bool isPLimitSet;
extern uint8_t isFloatEnabled;
extern volatile bool isWifiChanged;
extern bool ethernetEnabled;
extern volatile bool ethernetConnected;
extern uint8_t ledType;
extern String shuntOrCT;

// Function declarations
void setupAPIs();
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
// String getGlobalScheduleValuesfromMain();
// String getGlobalOtherValuesfromMain();
void setLocalTimeFromBrowser(String isoTime);
void loadMQTTSettings(JsonDocument &doc);
void stopWebServer();
void startWebServer();
// Reaps a web-portal firmware upload whose connection died mid-transfer and
// runs the deferred restart after a successful one. Call from loop().
void serviceFirmwareUpload();
// True while a firmware upload is streaming in, for callers that should stand
// down while it runs.
bool isFirmwareUploadActive();
void onMigrateLogs(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void onGetDebugLogFiles(AsyncWebServerRequest *request);
void onGetEnergyLogFiles(AsyncWebServerRequest *request);
void onGetOperationLogFiles(AsyncWebServerRequest *request);
void onGetAllFiles(AsyncWebServerRequest *request);
void onGetMigrationStatus(AsyncWebServerRequest *request);
void onClearLogsByType(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void onRenameFile(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void onSaveMasterSlaveSettings(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void onGetMasterSlaveSettings(AsyncWebServerRequest *request);
void onScanNetworks(AsyncWebServerRequest *request);
void onSetRootCACert(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
bool verifyPinWithParameter(const char *pin);
bool verifyPinWithParameter(const String &pin);
#endif // API_H