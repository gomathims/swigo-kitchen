#ifndef MQTT_H
#define MQTT_H

#include <ArduinoJson.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <RTClib.h>
#include <WiFi.h>
//#include <WiFiClientSecure.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "relay_led_handling.h"
#include "timesettings.h"
#include "logdata.h"

// Constants
#define MQTT_QUEUE_SIZE 20
#define MQTT_TASK_STACK_SIZE 8192
#define MQTT_TASK_PRIORITY 1
#define MQTT_TASK_CORE 1
#define MAX_TOPIC_LENGTH 120
#define MAX_PAYLOAD_LENGTH 1024
#define ATTRIBUTE_MAX_PAYLOAD 2560  // Larger buffer only for schedule attribute publish
#define MQTT_LOOP_INTERVAL 250
#define MQTT_MAX_PACKET_SIZE 256
// #define THINGSBOARD_SERVER "mqtt.thingsboard.cloud"
// #define THINGSBOARD_SERVER "iot.eviqtech.com" 

#define THINGSBOARD_PORT 1883
#define RPC_TURNON_METHOD "turnOn"
#define RPC_TURNOFF_METHOD "turnOff"

// Structs
typedef struct {
  char topic[MAX_TOPIC_LENGTH];
  char payload[MAX_PAYLOAD_LENGTH];
  boolean retained;
  unsigned long enqueuedMs; // millis() at enqueue — used to measure queue hold time
} mqtt_message_t;

// Global variables
extern bool isCloudConnected;
extern bool isGeneralMQTTEnabled;
extern char token_buffer[41];
extern char deviceId_buffer[41];
extern const char* propTopic;
extern const char* attribTopic;
extern const char* controlTopic;
extern const char* attribListenTopic;
extern const char* attribReqResTopic;
extern const char* attribRequestTopic;
extern volatile uint8_t onTriggerFromMQTT;
extern volatile uint8_t offTriggerFromMQTT;
extern unsigned long onTriggerFromMQTTStart;
extern TimeSpan onTriggerDurationFromMQTT;
extern float voltageToOutput;
extern float currentToOutput;
extern float powerToOutput;
extern float energyToOutput;
extern unsigned long lastMQTTUpdateTime;
//extern WiFiClient espClient;
extern PubSubClient mqttClient;
extern QueueHandle_t mqttQueue;
extern uint8_t relayState;
extern DateTime calculatedScheduledOffTime;
extern SwitchingEvent nextEvent;
// Resets the MQTT reconnect backoff to its minimum. Call this when an
// external condition (WiFi recovery, Ethernet link up) means we should
// try to re-establish the MQTT connection immediately.
void resetMqttBackoff();

// MQTT settings
extern bool sendHADiscovery;
extern String mqttBroker;
extern int mqttPort;
extern String mqttUsername;
extern String mqttPassword;
extern String mqttPublishTopic;
extern String mqttSubscribeTopic;
extern String mqttIntegrationType;
extern bool sendMQTTAttrib;

// Function declarations
void setupMQTT();

// Ask mqttPublisherTask to re-run setupMQTT() at a safe point in its own cycle.
// Web/API handlers MUST use this instead of calling setupMQTT() directly: they
// run on the AsyncTCP task, and setupMQTT() -> initMQTTQueue() reconfigures the
// live PubSubClient (setServer/setCallback/setKeepAlive/setBufferSize, the last
// of which reallocs the internal buffer). PubSubClient is not thread-safe and
// nothing guards it, so doing that while the publisher task is inside publish()
// or loop() is a use-after-free.
void requestMQTTReconfigure();

// Called from loop(). Applies a pending requestMQTTReconfigure() only while
// mqttPublisherTask does not exist yet — the case where MQTT was disabled at
// boot, so setupMQTT() never reached initMQTTQueue() and never spawned the task
// that normally consumes the flag. No-op once the task is running.
void serviceMQTTReconfigure();
void initMQTT();
void reconnectMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void initMQTTQueue(const char* broker, int port, const char* username = nullptr,
                   const char* password = nullptr,
                   const char* publishTopic = nullptr,
                   const char* subscribeTopic = nullptr);
bool queueMQTTMessage(const char* topic, String payload,
                      boolean retained = false);
void handleMQTTMessaging();
void loadMQTTSettings(JsonDocument& doc);
String getHaStateTopic(String hAEntityType);
String getHaSwitchTopic(String hAEntityType, String parameter);
String getHaCommandTopic(String hAEntityType, String parameter);
String getHaSensorConfig(String parameter, String unit, String hAEntityType);
void sendHADiscoveryMessages();
String getHaDiscoveryStateMessage();
uint8_t getMQTTConnectionStatus();
void sendMQTTAttribMessage();
void publishScheduleAttributeToCloud();
void publishDeviceInfoAttributesOnConnect();
void publishRtcHealthToCloud();
// Mirrors getScheduleStatusCode() to the cloud widget. Driven by
// schStatusPublishPending (a relay start or stop, or a fresh connection), so it
// publishes whenever called; windowEnd rides along so the widget can stop showing
// a status whose window has closed (attributes outlive the device's reporting).
void publishScheduleStatusToCloud(const char *status, DateTime windowEnd);
void publishAdminSettingsToCloud();
void publishTimingConfigToCloud();
void publishPowerSettingsToCloud();
void disconnectMQTT();
String getSwitchNameForHa();
String getUniqueSwitchNameForHa();
void sendMQTTRemoteSwitchMessage(String switchStatus, uint8_t buttonNo, bool shouldLog = true);
void addVsmrTelemetry(JsonVariant values);

#endif  // MQTT_H