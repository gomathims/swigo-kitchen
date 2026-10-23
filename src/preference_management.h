#ifndef PREFERENCE_MANAGEMENT_H       
#define PREFERENCE_MANAGEMENT_H

#include <Preferences.h>
#include <LittleFS.h>
#include "globals.h"
#include "buttontime.h"
#include "wifisettings.h"
#include "timesettings.h"



extern Preferences preferences;
extern bool isFirstTime;
extern bool isScheduleSet;
extern bool isNameSet;
extern bool isAPPassSet;
extern bool isWiFiSet;
extern bool isPLimitSet;
extern float cumulativeEnergy;
extern uint8_t isFloatEnabled;


extern bool isCloudConnected;
extern char token_buffer[41];
extern char deviceId_buffer[41];
extern SwitchingEvent tempEvent;
extern uint16_t dryRunRetryMinutes, overRunRetryMinutes, lowVoltageRetryMinutes, highVoltageRetryMinutes, startDelayMins, maxRunTimeMins, breakTimeMins;
extern uint8_t inRushExtraSecs;
extern bool  isThreePhaseDevice;
extern uint8_t  isRestartEnabled;
extern uint8_t  restartOptions;
extern uint8_t  isSlaveControlDevice;

extern float dryRunCurrentLimit, overRunCurrentLimit, lowVoltageLimit, highVoltageLimit, dryRunCurrentThreshold;
void initPreferences();
void resetPreferences();
void savePreference(char* key, bool value);
void saveRtcHealth();
bool readPreference(char* key);
void saveDeviceDetails();
void saveFloatDetails(uint8_t paraIsFloatEnabled, uint16_t flowDetectDelay = 10, uint16_t flowStopDelay = 10);
void saveWifiDetails();
void saveWifiSSIDPAss();
void saveApPassDetails();
void readEnergyData(unsigned long& relayStartTS, unsigned long& currentTS, float& energyConsumed);
void initiateTempEnergyData(unsigned long relayStartTS);
void updateTempEnergyData(unsigned long currentTS, float energyConsumed, float cumulativeEnergy);
void resetTempEnergyData();
void resetEnergyData();
void readCloudSyncDetails();
void updateCloudSyncDetails(bool syncEnabled, String token, String deviceId);
void saveMQTTSettings(const char* broker, int port, const char* username, const char* password, const char* publishTopic, const char* subscribeTopic, const char* type);
bool readGeneralMQTTSettings(String& broker, int& port, String& username, String& password, String& publishTopic, String& subscribeTopic, String& type);
bool readTempScheduleDetails(SwitchingEvent& tempEvent, char& varRunType);
void updateTempScheduleDetails(SwitchingEvent tempEvent, char varRunType);
void clearTempScheduleDetails();
void readButtonTimingDetails(TimerConfig& timingConfig, unsigned long& button1Dur, unsigned long& button2Dur, unsigned long& button3Dur, unsigned long& button4Dur);
void readButtonTimingString(char* formattedStr, size_t maxLength);
void updateButtonTimingDetails(TimerConfig timingConfig, unsigned long button1Duration = 0, unsigned long button2Duration = 0, unsigned long button3Duration = 0, unsigned long button4Duration = 0, const char* formattedStr = "");
// void save3PhaseSettings(bool threePhase, uint8_t tappingRequired, uint8_t tappingDuration, bool ethernetEnabled, uint8_t ledType, String shuntOrCT, uint8_t masterSlaveMode, String masterDeviceUrl, String mPin);
// vsmrMac: paired VSMR MAC for the ESP-NOW downlink filter (manual entry, e.g.
// "AA:BB:CC:DD:EE:FF"; empty = no filter).
void read3PhaseSettings(bool &threePhase, uint8_t &tappingRequired, uint8_t &tappingDuration, bool &ethernetEnabled, uint8_t &ledType, String &shuntOrCT, uint8_t &masterSlaveMode, String &masterDeviceUrl, String &mPin, String &remoteMethods, bool &leakDetectionEnabled, uint8_t &inRushSecs, bool &vsmrLinkEnabled, String &vsmrMac);
void save3PhaseSettings(bool threePhase, uint8_t tappingRequired, uint8_t tappingDuration, bool ethernetEnabled, uint8_t ledType, String shuntOrCT, bool leakDetectionEnabled, uint8_t inRushSecs, bool vsmrLinkEnabled, String vsmrMac);
void saveMasterRemoteSettings(uint8_t masterSlaveMode, String masterDeviceUrl, String mPin, String remoteMethods);
void saveScheduleToPreferences(uint8_t numObjects);
void loadScheduleFromPreferences();
void versionSetup();
String getFirmwareVersion();
void savePreviousFirmwareVersion();
String getPreviousFirmwareVersion();
bool isFirmwareVersionChanged();
bool shouldMigrateLogs();

#endif