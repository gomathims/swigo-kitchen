#ifndef LOGDEBUG_H
#define LOGDEBUG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <RTClib.h>

#include "timesettings.h"

// Constants
#define LOG_DEBUG_SIZE_LIMIT 6 * 1024  // 30 Kilo Bytes

// Enum for debug actions
enum DebugAction : uint8_t {
  API_LOGIN = 0,
  API_SET_SCHEDULE = 1,
  API_SET_DEVICE_NAME = 2,
  API_SET_DEVICE_PIN = 3,
  API_SET_WIFI_DETAILS = 4,
  API_SET_CURRENT_LIMIT = 5,
  API_FACTORY_RESET = 6,
  API_SET_DEVICE_DETAILS = 7,
  API_SET_AP_PASSWORD = 8,
  API_RESTART = 9,
  API_CLEAR_LOGS = 10,
  API_RESET_CUMULATIVE_ENERGY = 11,
  API_UPDATE_FIRMWARE_FAILED = 12,
  API_UPDATE_FIRMWARE_SUCCESS = 13,
  API_RESET_FAULT = 14,
  API_CLOUD_SYNC_DETAILS = 15,
  API_RENAME_FILE = 16,
  ESP_RESTART = 101,
  TIME_ADJUSTMENT = 102,
  AUTO_RTC_ADJUST = 103,
  FACTORY_RESET_BUTTON_PRESS = 104,
  DISABLE_WIFI = 105,
  RTC_ERROR = 106,
  ADMIN_SETTINGS_FAILURE = 150,
  ADMIN_BUTTONTIME_SETTINGS_SUCCESS = 151,
  ADMIN_3PHASE_SETTINGS_SUCCESS = 152,
  SYSTEM_FAILURE = 200,
  MQTT_QUEUE_CREATION_FAILED = 201,
  SYSTEM_INFO = 202,
  CONNECTIVITY_FAILURE = 203,
  CONNECTIVITY_SUCCESS = 204
};

#define DEBUG_MESSAGE_SIZE 200
// Struct for debug log entry
struct DebugEntry {
  time_t timestamp;
  DebugAction mode;
  char message[DEBUG_MESSAGE_SIZE];
};

// Struct for circular buffer pointers
struct DebugLogPointers {
  size_t start;
  size_t end;
  size_t count;
};


// Function declarations
void debugFSSetup();
void clearDebugLogs();
void logDebugAction(DebugAction mode, char* message);
void saveCoreDumpToFile();
void logRestartReason();
const char* getResetReasonString(esp_reset_reason_t reason);

// Core-dump streaming over MQTT (called from MQTT publisher task only)
bool hasCoreDumpFile();
size_t coreDumpFileSize();
bool queueCoreDumpReplay();      // arms the streamer; returns false if busy / no file
bool isCoreDumpReplayPending();
void serviceCoreDumpReplay();    // pushes at most one chunk per call
void publishDumpAvailableAttribute(); // sends dumpAvailable shared attribute on connect
// size_t saveDebugPointers();
// Logs a raw schedule body, split across several entries when it exceeds one.
// `mode` ("Cloud", "Portal", ...) is stamped on every entry so the source of
// the save survives in the log.
void logDebugActionPartSplit(const uint8_t *data, size_t len, const char *mode);

#endif  // LOGDEBUG_H
