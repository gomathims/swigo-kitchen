#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <RTClib.h>

#define MAX_RUNTIME_HOURS_WEB 24
#define MAX_WIFI_TRIALS 10
#define MAX_LOG_ENTRIES_TO_UI 30
#define MAX_LOG_ENTRIES_TO_SYNC_MQTT 3

#define WIFI_CHECK_INTERVAL 15000  // 15 seconds
#define MQTT_PUBLISH_INTERVAL 10000  // 10 seconds

// Voltage logging: collect voltage every 500ms and send as volt2 with local timestamps
// Comment/uncomment to enable/disable
// #define VOLT_LOG_ENABLED
#define VOLT_LOG_INTERVAL 500        // 500ms voltage sampling interval
#define VOLT_LOG_MAX_SAMPLES 20      // Max samples to buffer before sending
#define ENERGY_SAVE_INTERVAL (5 * 60 * 1000)  // 5 minutes
// #define CUMULATIVE_ENERGY_UPDATE_INTERVAL (1* 60 *1000) // 1 minutes

#define TEN_SECS 10000                      // 10 seconds checking
#define FACTORY_RESET_PRESS_DURATION 20000  // 20 seconds checking
#define DISABLE_WIFI_PRESS_DURATION 10000   // 20 seconds checking
#define BLE_ENABLE_PRESS_DURATION 5000     // 5 seconds for BLE enable
#define VSMR_PAIR_PRESS_DURATION 5000      // Button 1 held 5 s -> VSMR beacon
#define float_change_mode 5000              // 5 second

#define AP_MODE_MIN_TIME (1)  
  // In minutes-  
  // Once AP mode is set, it will not try to change to Wifi for a while

#define VOLTAGE_NOT_AVAILABLE 150
#define CURRENT_NOT_AVAILABLE 0

#define RESTART_ENABLED_FOR_BUTTONS 1
#define FAULT_MONITOR_DURATION 7000
#define OVERLOAD_MONITOR_DURATION 4000
#define INRUSH_CURRENT_SETTLE_DURATION 7000
#define DRYRUN_MONITOR_DURATION 4000
#define DRYRUN_CURRENT_SETTLE_DURATION 7000
#define LOOP_INTERVEL 200

enum PowerResumeMode : uint8_t {
  NO = 0,
  YES = 1,
};

enum ScheduleType : uint8_t {
  SCH_NONE = 0,
  DAILY = 1,
  WEEKLY = 2,
  TEMPORARY = 3,
  CYCLIC = 4,
  DURATION = 5
};

struct SwitchingEvent {
  DateTime startTime;   // Actual time to start, will vary from schStartTime only in case of cyclic schedule
  DateTime endTime;
  PowerResumeMode restartOption;
  ScheduleType scheduleType;
  long totalDuration;
  long remainingDuration;
  DateTime schStartTime;  // Original schedule start time
  DateTime schEndTime;    // Original schedule end time
};

struct PowerSettings {
  float drCur;  // Dryrun current
  float orCur;  // Overload current
  float lVol;   // Low voltage
  float hVol;   // High voltage
  float thCur;  // Threshold current

  uint8_t drMin;    // Dryrun minutes
  uint8_t orMin;    // Overcurrent minutes
  uint8_t lVolMin;  // Low voltage minutes
  uint8_t hVolMin;  // High voltage minutes
  uint8_t stDly;    // Start delay
  uint16_t maxRunTimeMins;  // Max run time in minutes
  uint16_t breakTimeMins;   // Break time in minutes

  uint8_t resOpt;  // all restart options
  uint8_t is3Ph;
  uint8_t isSlave;
};
extern RTC_DS3231 rtc;
// RTC backup-battery health (DS3231 OSF diagnostics)
extern bool rtcBatteryLow;
extern int  rtcLostPowerCount;
extern long rtcLastDriftSec;
extern bool rtcOsfBaselined;
// Global variable declarations
extern bool isFirstTime;
extern bool isScheduleSet;
extern bool isNameSet;
extern bool isAPPassSet;
extern bool isWiFiSet;
extern bool isPLimitSet;
extern uint8_t isFloatEnabled;

extern volatile bool isWifiChanged;

extern unsigned long previousMillis;
extern unsigned long wifiCheckMillis;

extern uint8_t relayState;
extern uint8_t countMQTTMessagesAfterRelayOff;

extern char runType;
extern DateTime calculatedScheduledOffTime;
extern uint8_t manualOff;
extern uint8_t scheduledTime;

extern volatile bool dryRunAlertState, overRunAlertState, lowVoltageAlertState,
    highVoltageAlertState, floatingAlertState, phaseFailureAlertState, phaseSequenceReverseAlertState;

extern DateTime espStartTime;
extern uint16_t dryRunRetryMinutes, overRunRetryMinutes, lowVoltageRetryMinutes,
    highVoltageRetryMinutes, startDelayMins, maxRunTimeMins, breakTimeMins;
extern uint8_t inRushExtraSecs;

extern uint8_t isRestartEnabled;
extern uint8_t restartOptions;
extern uint8_t isSlaveControlDevice;

extern volatile bool buttonFactoryResetPressed;
extern volatile bool buttonDisableWiFiPressed;

extern volatile unsigned long relayOnTime;
extern volatile unsigned long relayOffTime;
extern TimeSpan relayOnDuration;
// Raised by every relay start attempt and every stop (and by a fresh cloud
// connection), and consumed by handleMQTTMessaging() later in the same loop pass -
// by which time the handler's own bookkeeping (manualOff, lastDryRunTime, fault
// details) has settled, so the schedule status published to the cloud describes the
// finished event rather than the one in progress. See publishScheduleStatusToCloud().
extern bool schStatusPublishPending;
// Set when the running relay was switched on for a Duration Based schedule whose
// owed ON time ends BEFORE its window closes, so the stop that relayOnDuration
// brings about is "duration completed", not "stopped at scheduled time". Decided
// at switch-on, where both figures are known; false for every other run.
extern bool runEndsOnDurationSpent;

extern unsigned long relayOnRTCTime;
extern unsigned long lastEnergySaveTime;
extern float energyOnStart;
extern float cumulativeEnergy;

extern bool scheduleChanged;
extern SwitchingEvent nextEvent;
extern SwitchingEvent tempEvent;
extern char tempEventRunType;
extern uint8_t isTempSchedule;

extern uint8_t onTriggerFromInterface;
extern TimeSpan onTriggerDurationFromInterface;
extern uint8_t offTriggerFromInterface;
extern volatile bool Flow_stopped_manual;
extern volatile bool flowIncomingState;
extern unsigned long flow_start_time;
extern unsigned long flow_stop_time;
extern uint16_t flowDetectionDelaySec;  // Delay in seconds to detect flow start
extern uint16_t flowStopDelaySec;        // Delay in seconds to detect flow stop

extern volatile uint8_t onTriggerFromMQTT;
extern volatile uint8_t offTriggerFromMQTT;
extern TimeSpan onTriggerDurationFromMQTT;
extern unsigned long onTriggerFromMQTTStart;
extern DateTime webOffTime;

extern volatile uint8_t onTriggerFromWebhook;
extern volatile uint8_t offTriggerFromWebhook;
extern TimeSpan onTriggerDurationFromWebhook;

extern SemaphoreHandle_t rtcMutex;

void core0Task(void *parameter);
void handleSwitching(DateTime curDateTime);

// Logs only when this task's stack high-water mark drops below 1 KB,
// AND only when it worsens past the previous low. Each task owns a
// `static uint32_t low = UINT32_MAX;` and passes it by reference.
void checkStackHighWater(uint32_t &prevLow);

extern DateTime lastManualOffTime, lastDryRunTime, lastOverRunTime,
    lastLowVoltageTime, lastHighVoltageTime, lastPhaseFailureTime, lastPhaseSequenceReverseTime;

extern volatile uint32_t zcrossTime;
#endif  // GLOBALS_H