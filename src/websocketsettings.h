#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <Arduino.h>
#include <RTClib.h>

#include <ArduinoJson.h>
#include "timesettings.h"
#include "wifisettings.h"
#include "power_monitoring.h"
#include <ESPAsyncWebServer.h>
#include "mqtt.h"

#define MAX_WEBSOCKET_CLIENTS 3
#define MAX_SESSION_TIMEOUT 10 // In minutes 
#define MAX_TOKEN_LENGTH 17
#define RTC_MAGIC_NUMBER 0xA5A5A5A5

struct Session {
  char token[MAX_TOKEN_LENGTH];
  unsigned long lastActivityTime;
  bool active = false; // Track if the session slot is active
};

struct SessionData {
  uint32_t magicNumber;
  Session sessions[MAX_WEBSOCKET_CLIENTS];
};
extern RTC_DATA_ATTR SessionData rtcSessionData;
extern const unsigned long sessionTimeout ;

extern RTC_DATA_ATTR SessionData rtcSessionData;


extern uint8_t onTriggerFromInterface;
extern TimeSpan onTriggerDurationFromInterface;
extern uint8_t offTriggerFromInterface;
extern DateTime webOffTime;
extern uint8_t relayState;
extern DateTime calculatedScheduledOffTime, lastManualOffTime;
extern volatile bool dryRunAlertState, overRunAlertState, lowVoltageAlertState, highVoltageAlertState;
extern unsigned long dryRunStartTime, overRunStartTime, lowVoltageStartTime, highVoltageStartTime;
extern unsigned long floatMaxStartTime;
extern volatile bool floatingAlertState;
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern SwitchingEvent nextEvent;
extern uint8_t manualOff;
extern uint8_t scheduledTime;
extern uint8_t scheduledTimeOuter;
extern float cumulativeEnergy;
extern float energyOnStart;
extern DateTime espStartTime;
extern bool espStartTimeTrusted;

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void initWebSocket();
void initializeSessions();
uint8_t getFaultResetOption(DateTime scheduleStartTime, DateTime scheduleEndTime);
uint8_t checkFaultOccuredAndRestartOption(DateTime curDateTime, DateTime scheduleStartTime, DateTime scheduleEndTime,
                                         char faultType);
// The schedule status both UIs show: "" (nothing to report), "NEXT_SCHEDULE",
// "DURATION_OVER", or a SCHTIME_* interruption code. The portal gets it over the
// websocket, the cloud widget over MQTT - one decision, so they cannot disagree.
const char *getScheduleStatusCode(DateTime curDateTime);
void notifyClients(bool showScheduleStatus = true);
// Sends the single frame owed by notifyClients() calls that were coalesced.
// Call once per loop() pass.
void flushPendingNotify();
void cleanUpSessions(); 
void generateSessionToken(char* tokenBuffer);
Session* findSessionByToken(String token);
bool startNewSession(String& token);

#endif