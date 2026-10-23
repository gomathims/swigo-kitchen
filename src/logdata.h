#ifndef LOGDATA_H
#define LOGDATA_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <logdebug.h>

#include <unordered_map>

#include "energydata.h"
#include "web_content.h"

#define LOG_SIZE_LIMIT 2 * 1024  // 2 Kilo Bytes
extern int16_t deviceTZOffset;
extern const char* propTopic;
extern bool isGeneralMQTTEnabled;
extern uint8_t relayState;
bool queueMQTTMessage(const char* topic, String payload, boolean retained);
// Define action modes as uint8_t

enum ModeOfAction : uint8_t {
  START_WEB = 0,
  START_SCHEDULED = 1,
  START_POWER_RESUME = 2,
  START_MANUAL_1 = 3,
  START_MANUAL_2 = 4,
  START_MANUAL_3 = 5,
  START_MANUAL_4 = 6,
  STOP_TIMER = 7,
  STOP_MANUAL = 8,
  STOP_SCHEDULED = 9,
  STOP_WEB = 10,
  STOP_POWER_DOWN = 11,
  STOP_DRYRUN = 12,
  STOP_OVERRUN = 13,
  STOP_LOWVOLTAGE = 14,
  STOP_HIGHVOLTAGE = 15,
  STOP_FLOAT = 16,
  STOP_PHASE_FAILURE = 17,
  STOP_PHASE_SEQUENCE_REVERSE = 18,
  RESTART_ESP = 20,

  START_MQTT = 21,
  STOP_MQTT = 22,
  START_MQTT_FLOAT_REJECT = 23,
  START_MQTT_HV_REJECT = 24,
  START_MQTT_LV_REJECT = 25,
  START_MQTT_DURATION_REJECT = 26,
  START_CYCLIC = 27,

  START_WEB_FLOAT_REJECT = 30,
  START_SCHEDULED_FLOAT_REJECT = 31,
  START_POWER_RESUME_FLOAT_REJECT = 32,
  START_MANUAL_1_FLOAT_REJECT = 33,
  START_MANUAL_2_FLOAT_REJECT = 34,
  START_MANUAL_3_FLOAT_REJECT = 35,
  START_MANUAL_4_FLOAT_REJECT = 36,

  START_WEB_HV_REJECT = 37,
  START_SCHEDULED_HV_REJECT = 38,
  START_POWER_RESUME_HV_REJECT = 39,
  START_MANUAL_1_HV_REJECT = 40,
  START_MANUAL_2_HV_REJECT = 41,
  START_MANUAL_3_HV_REJECT = 42,
  START_MANUAL_4_HV_REJECT = 43,

  START_WEB_LV_REJECT = 44,
  START_SCHEDULED_LV_REJECT = 45,
  START_POWER_RESUME_LV_REJECT = 46,
  START_MANUAL_1_LV_REJECT = 47,
  START_MANUAL_2_LV_REJECT = 48,
  START_MANUAL_3_LV_REJECT = 49,
  START_MANUAL_4_LV_REJECT = 50,

  ABNORMAL_CASE_OFF_NOT_TRIGGERED = 51,
  ABNORMAL_CASE_ON_NOT_TRIGGERED = 52,
  ABNORMAL_CASE_DIFF_IN_STATUS = 53,
  PHASE_SEQUENCE_REVERSE_ALERT = 54,
  PHASE_FAILURE_ALERT = 55,
  PHASE_SEQUENCE_REVERSE_ALERT_CLEARED = 56,
  PHASE_FAILURE_ALERT_CLEARED = 57,
  START_FLOW_SENSOR = 58,
  STOP_FLOW_SENSOR = 59,
  FLOW_NOT_DETECTED=60,
  START_WEBHOOK = 61,
  STOP_WEBHOOK = 62,
  START_MASTER_BUTTON_PRESS_SUCCESS = 63,
  STOP_MASTER_BUTTON_PRESS_SUCCESS = 64,
  START_MASTER_BUTTON_PRESS_SENDING_FAILED = 65,
  STOP_MASTER_BUTTON_PRESS_SENDING_FAILED = 66,
  START_MASTER_BUTTON_PRESS_ACTION_FAILED =67,
  STOP_MASTER_BUTTON_PRESS_ACTION_FAILED = 68,
  START_MASTER_BUTTON_PRESS_ALREADY_ON = 69,
  STOP_MAXTIME_BREAK = 70,
  START_MASTER_MQTT_SUCCESS = 71,
  STOP_MASTER_MQTT_SUCCESS = 72,
  FLOW_DETECTED = 73,
  FLOW_STOPPED = 74,
  START_MASTER_TANK_FULL_REJECT = 75,
  START_MASTER_FLOW_REJECT = 76,
  FORCE_START_FLOW = 77,
  LEAKAGE_DETECTED = 78,
  PHASE_FAILURE_START_REJECT = 79,
  PHASE_SEQUENCE_REVERSE_START_REJECT = 80,
  // Duration Based schedule: the owed ON time ran out before the window's end,
  // as against STOP_SCHEDULED, which is the window itself ending.
  STOP_DURATION_COMPLETED = 81,
};

// Define a struct for the log entry
struct LogEntry {
  time_t timestamp;
  ModeOfAction mode;
  ScheduleType sType;
  uint16_t duration;
  float current;
  float voltage;
};
extern std::unordered_map<ModeOfAction, String> modeMessages;
bool queueLogEntryToMQTT(LogEntry entry);

// Pointers for circular buffer
struct Pointers {
  size_t start;
  size_t end;
  size_t count;
};

void loadPointers();
size_t savePointers();

void formatLittleFS();
void loadLogfile();
void littleFSSetup();
void clearLogs();

// ts is the time the event actually happened. Pass it whenever the caller
// already holds it (most do) so the log records the event time rather than the
// time the logging task got around to writing it. 0 = read the RTC at write time.
void logAction(ModeOfAction mode, float current = 0.0, float voltage = 0.0, uint16_t duration = 0, ScheduleType scheduleType = ScheduleType::SCH_NONE, time_t ts = 0);
void formatTime(char* buffer, size_t bufferSize, time_t time);

// Fills the provided document with operation log entries. Caller serializes
// it directly into the HTTP response stream — no intermediate String alloc.
void prepareLogsJson(JsonDocument &doc);
// void sendAllLogsToMQTT();  // disabled - see logging_manager.cpp

#endif
