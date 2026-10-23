#ifndef RELAYHANDLING_H
#define RELAYHANDLING_H

#include <Arduino.h>
// #include "api.h"
#include "timesettings.h"
#include "wifisettings.h"
#include "pinmapping.h"
#include "buttontiming.h"
#include "buttonpress.h"
#include "logdata.h"
#include "logdebug.h"
#include "websocketsettings.h"
#include "preference_management.h"
#include "power_monitoring.h"
#include "esp_partition.h"

// External variables from buttonpress
extern  bool masterSwitchOn;
extern bool masterSwitchOnStatusMqtt;
extern unsigned long masterSwitchOnTimeMqtt;

// Global variables
extern int lastBlinkTime;
extern bool lastBlinkstatus;
extern int indicatorStartTime;
extern unsigned long colorLedBlinkstartTime;

extern char runType;
extern bool volatile floatStateLedIndicate, highVoltageLedIndicate, lowVoltageLedIndicate, dryrunLedIndicate, overrunLedIndicate, leakageLedIndicate;
extern bool volatile disableWiFiLedIndicate, factoryResetLedIndicate;
extern bool volatile vsmrPairLedIndicate;
extern DateTime lastManualOffTime, lastDryRunTime, lastOverRunTime, lastLowVoltageTime, lastHighVoltageTime;

extern TimeSpan relayOnDuration;
extern uint8_t countMQTTMessagesAfterRelayOff;
extern unsigned long lastMQTTUpdateTime;

extern unsigned long relayOnRTCTime;
extern unsigned long lastEnergySaveTime;
extern float energyOnStart;




enum BlinkType : uint8_t {
  RESET = 0,
  RUNNING = 1,
  DRYRUN_ERROR = 2,
  OVERLOAD_ERROR = 3,
  LOWVOLTAGE_ERROR = 4,
  HIGHVOLTAGE_ERROR = 5,
  LEAKAGE_ERROR = 6,
  NONE =100
};
extern volatile bool ignoreButton2FallOnce;
extern volatile bool ignoreButton3FallOnce;
extern volatile bool ignoreButton4FallOnce;
extern volatile bool ignoreButtonOffFallOnce;

// Function declarations
void setAllIndicatorsOff();
void blinkErrorLed(uint8_t ledNo);
void blinkColorLed(uint8_t ledNo);
void blinkColorLedWithDuration(const struct RGBColor color, BlinkType blinkType, unsigned long onDuration, unsigned long offDuration, unsigned long totalDuration, volatile bool* param = nullptr);
void handleLEDBlink(char varRunType);
void logStartFlowAlert(char varRunType);
void logStartFloatAlert(char varRunType);
void logStartHVAlert(char varRunType, float alertVoltage = 0);
void logStartLVAlert(char varRunType, float alertVoltage = 0);
int handleRelayOn(char varRunType, DateTime paraDateTime, TimeSpan duration);
void handleRelayOnSchedule(DateTime scheduledOnTime, DateTime scheduledOffTime, DateTime paraDateTime);
void handleRelayOnCyclic(DateTime scheduledOnTime, DateTime scheduledOffTime, DateTime paraDateTime);
void handleRelayOnResume(DateTime scheduledOnTime, DateTime scheduledOffTime, DateTime paraDateTime);
void handleRelayOnManualPress(uint8_t buttonNo, DateTime paraDateTime);
void handleRelayOnfromWeb(DateTime paraDateTime, TimeSpan duration);
void handleRelayOnfromMQTT(DateTime paraDateTime, TimeSpan duration);
void handleRelayOnfromWebhook(DateTime paraDateTime, TimeSpan duration);
// void handleRelayOFF(char varOffType, DateTime paraDateTime);
void handleRelayOFF(char varOffType, DateTime paraDateTime, bool phaseProblem = false);

void handleRelayOnFlow(DateTime paraDateTime);

void sendButtonPressToMaster(uint8_t buttonNo, String state, bool shouldLog = true);
void checkMasterState();
void handleMasterSwitchStatusLed();
void resetOtherStartCommands();
#endif // RELAYHANDLING_H