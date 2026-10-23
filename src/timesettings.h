#ifndef TIMESETTINGS_H
#define TIMESETTINGS_H

#include <ArduinoJson.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <RTClib.h>
#include <sys/time.h>
#include <time.h>
#include <WiFiUdp.h>
#include <ESP32Ping.h>
#include "logdebug.h"
#include "preference_management.h"
#include "preference_power.h"
#include "esp_task_wdt.h"

#define MAX_SCHEDULE_OBJECTS 10
struct TimeSchedule {
  uint8_t startHour;
  uint8_t startMinute;
  uint8_t endHour;
  uint8_t endMinute;
  uint8_t details;
  uint8_t days;
  // New fields for cyclic schedule, isCyclic is saved as a bit in details field
  uint8_t onDurHrs;
  uint8_t onDurMins;
  uint8_t offDurHrs;
  uint8_t offDurMins;
};
// Define a global variable for the time schedule array
extern TimeSchedule gScheduleArray[MAX_SCHEDULE_OBJECTS]; 
extern uint8_t gScheduleCount; 

// Constants
extern const char* ntpServer;
extern const int ntpPort;
extern const long utcOffsetInSeconds;

// Global variables
extern NTPClient timeClient;
extern WiFiUDP udp;
extern bool scheduleChanged;
extern uint8_t isRestartEnabled;
extern uint8_t isTempSchedule;
extern DateTime espStartTime;
extern bool espStartTimeLatched;
extern bool espStartTimeTrusted;

// Below this (2023-01-01) an RTC reading is not a real time - it is a cell that held nothing.
#define RTC_TIME_SANITY_EPOCH 1672531200UL

// Function declarations
void print_time(DateTime dt);
bool setRTCFromNTP();
void onRtcTimeValidated();
void rebaseEspStartTime(const DateTime& beforeAdjust, const DateTime& afterAdjust);
String formatDateTime(const DateTime& dt);
// How long a reading may be reused before the DS3231 is read again. A real
// read costs ~1.3ms of I2C and loop() calls this every iteration, so readings
// are cached and carried forward with millis() in between. Accuracy stays
// within the 1-second resolution DateTime can represent anyway.
#define RTC_CACHE_MAX_AGE_MS 500

DateTime getDateTimeFromRTC(bool returnImmediate = false);

// Drop the cached reading. Must be called after anything writes the clock
// (rtc.adjust), otherwise the stale baseline keeps being carried forward.
void invalidateRtcCache();

// How long a cached RTC reading may be reused before the chip is read again.
// #define RTC_CACHE_MAX_AGE_MS 100

// Cheap stand-in for getDateTimeFromRTC() on hot paths (loop() calls it every
// iteration). Reads the DS3231 at most once per RTC_CACHE_MAX_AGE_MS and
// carries the reading forward using millis() in between - a real read costs
// ~1.3ms of I2C, this costs a few microseconds. Accuracy stays within the
// 1-second resolution DateTime can represent anyway.
// Anything that writes the clock must call invalidateRtcCache() so the stale
// baseline is dropped immediately.
DateTime getDateTimeCached();
void invalidateRtcCache();

#ifdef LOG_TIMING
// Split of the most recent getDateTimeFromRTC() call - see the definition in
// timesettings.cpp. Temporary instrumentation; remove with LOG_TIMING.
extern volatile uint32_t rtcLastMutexWaitUs;
extern volatile uint32_t rtcLastReadUs;
#endif
void initiateTimeComponents();
String getTimeString();
bool isInternetAvailable();
String formatDuration(TimeSpan duration);
String getScheduleJsonString(TimeSchedule* scheduleArray);
void saveScheduleToEEPROM(JsonDocument doc);
SwitchingEvent getNextSchedule(DateTime currentTime);

#endif // TIMESETTINGS_H
