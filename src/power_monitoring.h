#ifndef POWERCONFIGURATION_H
#define POWERCONFIGURATION_H

#include <ArduinoJson.h>

#include "logdata.h"
#include "pinmapping.h"
#include "power_sensor_bl0939.h"
#include "preference_management.h"
#include "websocketsettings.h"
#include "wifisettings.h"
#define POWER_SENSING_TIMEOUT_MS 500
#define ONEDAY 1440
#define DEFAULT_DRY_RUN_CURRENT_LIMIT 0
#define DEFAULT_OVER_RUN_CURRENT_LIMIT 20
#define DEFAULT_LOW_VOLTAGE_LIMIT 150
#define DEFAULT_HIGH_VOLTAGE_LIMIT 290
#define DEFAULT_RETRY_MINS 240
#define DEFAULT_RESTART_DELAY_MINS 1

// struct PowerSettings {
// float drCur;    // Dryrun current
// float orCur;    // Overload current
// float lVol;     // Low voltage
// float hVol;     // High voltage
// float thCur;    // Threshold current

// uint8_t drMin;  // Dryrun minutes
// uint8_t orMin;  // Overcurrent minutes
// uint8_t lVolMin; // Low voltage minutes
// uint8_t hVolMin; // High voltage minutes
// uint8_t stDly;   // Start delay

// uint8_t resOpt; // all restart options
// uint8_t is3Ph;
// uint8_t isSlave;

// };

extern bool isThreePhaseDevice;
extern uint8_t tappingRequired;
extern uint8_t tappingDuration;
extern unsigned long tappingStartTime;
extern bool ethernetEnabled;
extern volatile bool ethernetConnected;
extern uint8_t ledType;
extern String shuntOrCT;
extern uint8_t masterSlaveMode;
extern String masterDeviceUrl;
extern String mPin;
extern String remoteMethods;

extern float voltage, current, power, pf, energy;
extern float voltageToOutput, currentToOutput, powerToOutput, energyToOutput;
extern float dryRunCurrentLimit, overRunCurrentLimit, lowVoltageLimit,
    highVoltageLimit, dryRunCurrentThreshold;
extern unsigned long dryRunStartTime, overRunStartTime, lowVoltageStartTime,
    highVoltageStartTime;
extern unsigned long factoryResetStartTime, disableWiFiStartTime,
    floatLowStartTime, lastUIUpdateTime, bleEnableStartTime, vsmrPairStartTime;
extern DateTime lastManualOffTime, lastDryRunTime, lastOverRunTime,
    lastLowVoltageTime, lastHighVoltageTime, lastPhaseFailureTime, lastPhaseSequenceReverseTime;

extern volatile bool dryRunAlertState, overRunAlertState, lowVoltageAlertState,
    highVoltageAlertState, floatingAlertState, phaseFailureAlertState, phaseSequenceReverseAlertState;
// phaseAlertLogged is shared by phase failure and phase sequence reverse - one
// "start refusal already recorded for this fault episode" latch for both.
extern volatile bool lowVoltageAlertLogged, highVoltageAlertLogged, phaseAlertLogged;


extern uint16_t dryRunRetryMinutes, overRunRetryMinutes, lowVoltageRetryMinutes,
    highVoltageRetryMinutes, startDelayMins, maxRunTimeMins, breakTimeMins;
extern uint8_t inRushExtraSecs;
extern uint8_t isRestartEnabled, restartOptions, isSlaveControlDevice;

extern volatile bool buttonFactoryResetPressed, buttonDisableWiFiPressed;
extern volatile bool disableWiFiLedIndicate, factoryResetLedIndicate;
extern volatile bool vsmrPairLedIndicate;
extern volatile bool buttonBLEEnablePressed;
extern volatile bool bleComboRearmPending;

extern uint8_t relayState;
extern float energyOnStart;
extern volatile unsigned long relayOnTime, relayOffTime;

void checkFloat();
void checkPhaseFaults(DateTime curDateTime);
void monitorPower(DateTime curDateTime);
void checkFactoryReset();
void checkDisableWiFi();
void checkButtonOffLongPress();
void checkEnableBLE();
void checkVsmrPairing();
void readCurSettings(bool afterSave);
bool readSensorWithTimeout(void (*sensorFunction)(DateTime), unsigned long timeoutMs, DateTime curDateTime);
void resetPowerTimer();
void saveCurLimit(JsonDocument doc);
void resetFaultConditionsRoutine();
void setupPowerSensor();
JsonDocument getCurSettings();


/*Tem variable decleratin*/
extern volatile bool flowIncomingState;
extern volatile bool Flow_stopped_manual;

/*Temp function decleration */
void checkFlow();


/*Tem variable decleratin*/

extern volatile bool Float_On_Function;

extern volatile bool floatingAlertState2;

void float_select_mode();
#endif