#include "mqtt.h"
#include "api.h"
#include "buttonpress.h"
#include "buttontiming.h"
#include "logdata.h"
#include "logdebug.h"
#include "logging_manager.h"
#include "ota_update.h"
#include "power_monitoring.h"
#include "preference_management.h"
#include "vsmr_link.h"
#include "wifisettings.h"

// Global variables
bool isCloudConnected = false;
bool isGeneralMQTTEnabled = false;
char token_buffer[41];
char deviceId_buffer[41];
const char *propTopic = "v1/devices/me/telemetry";
const char *attribTopic = "v1/devices/me/attributes";
const char *controlTopic = "v1/devices/me/rpc/request/+";
const char *attribListenTopic = "v1/devices/me/attributes";
const char *attribReqResTopic = "v1/devices/me/attributes/response/+";
const char *attribRequestTopic = "v1/devices/me/attributes/request/";

extern float voltageToOutput;
extern float currentToOutput;
extern float powerToOutput;
extern float energyToOutput;
unsigned long lastMQTTUpdateTime = 0;
WiFiClient espClient;
PubSubClient mqttClient(espClient);
QueueHandle_t mqttQueue = NULL;
String mqttBroker;
int mqttPort;
String mqttUsername;
String mqttPassword;
String mqttPublishTopic;
String mqttSubscribeTopic;
String mqttIntegrationType;
bool sendHADiscovery = false;
bool sendMQTTAttrib = false;

// Time-based backoff for MQTT reconnect. reconnectMQTT() is single-attempt;
// the publisher task only calls it once millis() >= mqttNextRetryMs.
static const unsigned long MQTT_BACKOFF_MIN_MS = 1000;
static const unsigned long MQTT_BACKOFF_MAX_MS = 60000;
// When the underlying link (WiFi/Ethernet) is up but the broker connect fails,
// it's almost always a transient broker-side event (e.g. a ThingsBoard restart),
// not a network outage. Cap the backoff lower in that case so the device
// re-establishes within ~15s of the broker coming back, instead of waiting out
// the full 60s. WiFi/link-recovery events still reset the backoff to MIN.
static const unsigned long MQTT_BACKOFF_MAX_LINKUP_MS = 15000;
static unsigned long mqttBackoffMs = MQTT_BACKOFF_MIN_MS;
static unsigned long mqttNextRetryMs = 0;

void resetMqttBackoff() {
    if (mqttBackoffMs != MQTT_BACKOFF_MIN_MS || mqttNextRetryMs != 0) {
        Serial.println("[MQTT][RC] backoff reset (external recovery signal)");
    }
    mqttBackoffMs = MQTT_BACKOFF_MIN_MS;
    mqttNextRetryMs = 0;
}

// Larger buffer used only when sending schedule attribute (not in queue)
static char scheduleAttribPayload[ATTRIBUTE_MAX_PAYLOAD];
static size_t scheduleAttribLen = 0;
static volatile bool scheduleAttribPending = false;

#ifdef VOLT_LOG_ENABLED
// Voltage logging: buffer to collect readings every 500ms, sent as batched telemetry
struct VoltLogEntry {
    float voltage;
    unsigned long captureMillis; // millis() at capture time
};
static VoltLogEntry voltLogBuffer[VOLT_LOG_MAX_SAMPLES];
static uint8_t voltLogCount = 0;
static unsigned long lastVoltLogTime = 0;
#endif

uint8_t getMQTTConnectionStatus() {
    if (isCloudConnected) {
        return mqttClient.connected() ? 11 : 10;
    } else if (isGeneralMQTTEnabled) {
        if (mqttIntegrationType == "HA") {
            return mqttClient.connected() ? 21 : 20;
        } else {
            return mqttClient.connected() ? 31 : 30;
        }
    } else {
        return 0; // MQTT not enabled
    }
}
void setupMQTT() {
    // delay(300);
    esp_task_wdt_reset();
    Serial.println("MQTT setup called");
    readCloudSyncDetails();
    // mqttClient.setServer(THINGSBOARD_SERVER, THINGSBOARD_PORT);
    // reconnectMQTT();
    if (isCloudConnected) {
        initMQTTQueue(THINGSBOARD_MQTT, THINGSBOARD_PORT);
    } else {
        // Pass global variables into readGeneralMQTTSettings

        if (isGeneralMQTTEnabled = readGeneralMQTTSettings(mqttBroker, mqttPort, mqttUsername, mqttPassword,
                                                           mqttPublishTopic, mqttSubscribeTopic, mqttIntegrationType)) {
            Serial.println("Initializing MQTT server with the following settings:");
            Serial.println("Broker: " + mqttBroker);
            Serial.println("Port: " + String(mqttPort));
            Serial.println("Username: " + mqttUsername);
            Serial.println("Publish Topic: " + mqttPublishTopic);
            Serial.println("Subscribe Topic: " + mqttSubscribeTopic);
            // Initialize the MQTT server
            initMQTTQueue(mqttBroker.c_str(), mqttPort, mqttUsername.c_str(), mqttPassword.c_str(),
                          mqttPublishTopic.c_str(), mqttSubscribeTopic.c_str());
        } else {
            Serial.println("General MQTT is not enabled. Skipping MQTT initialization.");
        }
    }
    resetMqttBackoff();
}

// String controlAckTopic =
// "/devices/11f3dfa0-ba06-11ef-82e8-957b60666c9e/commands/ack";
void sendStatusDiscovery() {
    // String discoveryMsg = getHaDiscoveryStateMessage();
    // mqttClient.publish(getHaStateTopic("binary_sensor").c_str(), discoveryMsg.c_str(), true);
    Serial.println("Sending initial status discovery");
    String mainRelayTopic = getHaSwitchTopic("switch", "relayStatus");

    // Serial.println("Main Relay Topic: " + mainRelayTopic);
    // Serial.println("Main Relay Status: " + String(relayState ? "ON" : "OFF"));

    mqttClient.publish(mainRelayTopic.c_str(), relayState ? "ON" : "OFF", true);
}
void disconnectMQTT() {
    mqttClient.disconnect();
} // Disconnect from Mqtt
// Single-attempt reconnect. The publisher task paces retries via time-based
// backoff (mqttNextRetryMs) — there is no inner retry loop or blocking delay
// here, so the message queue keeps draining between attempts.
void reconnectMQTT() {
    if (mqttClient.connected()) {
        return;
    }
    if (!(wiFiMode == 'W' || ethernetConnected)) {
        return;
    }

    unsigned long t0 = millis();
    Serial.printf("[MQTT][RC] attempt start mode=%s\n",
                  isCloudConnected ? "cloud" : (isGeneralMQTTEnabled ? "general" : "none"));
    bool success = false;

    if (isCloudConnected) {
        if (mqttClient.connect(getSwitchName(), token_buffer, NULL)) {
            mqttClient.setCallback(mqttCallback);
            mqttClient.subscribe(controlTopic);
            if (mqttClient.subscribe(attribListenTopic)) {
                Serial.println("MQTT subscribe attribListenTopic OK");
            } else {
                Serial.println("MQTT subscribe attribListenTopic failed");
            }
            mqttClient.subscribe(attribReqResTopic);
            publishScheduleAttributeToCloud();
            publishDeviceInfoAttributesOnConnect();
            // Resend the schedule status on every fresh connection: what the cloud
            // still holds is unknown (attribute cleared, device re-provisioned, or a
            // status that changed while the link was down). handleMQTTMessaging()
            // picks this up on its next pass, where the RTC read belongs.
            schStatusPublishPending = true;
            publishRtcHealthToCloud();
            publishAdminSettingsToCloud();
            publishTimingConfigToCloud();
            publishPowerSettingsToCloud();
            publishOTAAttributes();
            requestOTASharedAttributes();
            publishDumpAvailableAttribute();
            // Auto-backfill: replay the 5 most recent operation logs in case any
            // were lost during the outage (QoS0 + zombie-socket window).
            queueReplayRequest(LOG_TYPE_OPERATION, 0, 0, 5);
            // Countdown resync: if the relay is ON, publish the REMAINING run time
            // as a fresh duration datapoint (server-stamped ts = now). The widget
            // computes end-of-run as ts + duration, so this stays correct even if
            // the original ON event never reached the cloud.
            if (relayState) {
                unsigned long offAtMs = relayOnTime + (relayOnDuration.totalseconds() * 1000);
                if (millis() < offAtMs) {
                    // Resend remaining run time AND runType so a widget connecting
                    // mid-run knows both the countdown and whether to show the
                    // current schedule window (see logActionInternal).
                    char durBuf[64];
                    snprintf(durBuf, sizeof(durBuf), "{\"durationSecs\":%lu,\"runType\":\"%c\"}",
                             (unsigned long)((offAtMs - millis()) / 1000), runType);
                    queueMQTTMessage(propTopic, durBuf);
                }
            }
            success = true;
        }
    } else if (isGeneralMQTTEnabled) {
        if (mqttIntegrationType == "HA") {
            String switchStateTopic = getHaStateTopic("status");
            if (mqttClient.connect(getSwitchName(), mqttUsername.c_str(), mqttPassword.c_str(),
                                   switchStateTopic.c_str(), 0, true, "offline")) {
                delay(50);
                mqttClient.publish(switchStateTopic.c_str(), "online", true);
                delay(50);
                sendStatusDiscovery();
                mqttClient.setCallback(mqttCallback);
                String haSubTopic = "homeassistant/switch/" + getUniqueSwitchNameForHa() + "/set";
                Serial.println(haSubTopic.c_str());
                mqttClient.subscribe(haSubTopic.c_str());
                success = true;
            }
        } else {
            if (mqttClient.connect(getSwitchName(), mqttUsername.c_str(), mqttPassword.c_str())) {
                mqttClient.setCallback(mqttCallback);
                mqttClient.subscribe(mqttPublishTopic.c_str());
                success = true;
            }
        }
    }

    esp_task_wdt_reset();
    unsigned long elapsed = millis() - t0;

    // Track consecutive broker-connect failures while the underlying link is up,
    // so we can flag "WiFi/Ethernet OK but MQTT can't reach broker" once instead
    // of spamming every retry tick.
    static uint8_t mqttFailWhileLinkUp = 0;

    if (success) {
        Serial.printf("[MQTT][RC] connect OK in %lums, state=%d, queueDepth=%u\n", elapsed, mqttClient.state(),
                      (unsigned)(mqttQueue ? uxQueueMessagesWaiting(mqttQueue) : 0));
        mqttFailWhileLinkUp = 0;
        resetMqttBackoff();
        // Enqueue does not guarantee broker delivery, so the two current=0 updates
        // sent right after a relay-off can be lost across a broker outage, leaving
        // the cloud stuck at the last non-zero current. Reset the counter on a fresh
        // connect so those updates are re-sent with the current relayStatus.
        countMQTTMessagesAfterRelayOff = 0;
        // char dbgBuf[200];
        // snprintf(dbgBuf, sizeof(dbgBuf), "MQTT connected. mode=%s elapsed=%lums",
        //          isCloudConnected ? "cloud" : (isGeneralMQTTEnabled ? "general" : "none"), elapsed);
        // logDebugAction(DebugAction::CONNECTIVITY_SUCCESS, dbgBuf);
    } else {
        mqttNextRetryMs = millis() + mqttBackoffMs;
        Serial.printf("[MQTT][RC] connect FAIL rc=%d in %lums, nextRetryIn=%lums, queueDepth=%u\n", mqttClient.state(),
                      elapsed, mqttBackoffMs, (unsigned)(mqttQueue ? uxQueueMessagesWaiting(mqttQueue) : 0));

        bool linkUp = (WiFi.status() == WL_CONNECTED) || ethernetConnected;
        if (linkUp) {
            if (mqttFailWhileLinkUp < 255)
                mqttFailWhileLinkUp++;
            // Persist a debug entry on the 5th consecutive failure (≈ 1+1+2+4+8 = 16s
            // of backoff in by then) so flash isn't spammed but a real outage is captured.
            if (mqttFailWhileLinkUp == 5) {
                char dbgBuf[200];
                snprintf(dbgBuf, sizeof(dbgBuf),
                         "MQTT connect failing while link up. rc=%d backoff=%lums fails=%u "
                         "wifiStatus=%d RSSI=%d eth=%d",
                         (int)mqttClient.state(), mqttBackoffMs, (unsigned)mqttFailWhileLinkUp, (int)WiFi.status(),
                         WiFi.RSSI(), ethernetConnected ? 1 : 0);
                logDebugAction(DebugAction::CONNECTIVITY_FAILURE, dbgBuf);
            }
        } else {
            // No link — clear the counter; this isn't a broker-side problem.
            mqttFailWhileLinkUp = 0;
        }

        mqttBackoffMs *= 2;
        // Link up => broker-only outage (e.g. server restart): cap lower so we
        // reconnect promptly once the broker is back. Link down => keep the
        // larger cap to avoid hammering during a real network outage.
        unsigned long backoffCap = linkUp ? MQTT_BACKOFF_MAX_LINKUP_MS : MQTT_BACKOFF_MAX_MS;
        if (mqttBackoffMs > backoffCap) {
            mqttBackoffMs = backoffCap;
        }
    }
}

// Set by requestMQTTReconfigure() from any task; consumed by mqttPublisherTask
// once it exists, so all mqttClient reconfiguration happens on the one task
// that also uses it. Before the task exists, loop() consumes it instead — see
// serviceMQTTReconfigure().
static volatile bool mqttReconfigurePending = false;

// True once mqttPublisherTask has been spawned by initMQTTQueue(). Only that
// task may touch mqttClient afterwards; until then loop() is the only candidate.
static volatile bool mqttPublisherTaskCreated = false;

void requestMQTTReconfigure() {
    mqttReconfigurePending = true;
}

// Boot with neither cloud sync nor general MQTT enabled and setupMQTT() never
// reaches initMQTTQueue(), so mqttPublisherTask is never spawned. A later
// /connectcloud (or MQTT settings save) then sets mqttReconfigurePending with
// nothing alive to consume it, and MQTT stays dead until the next reboot.
// loop() calls this to close that gap: it applies the pending reconfiguration
// itself while the publisher task does not exist yet — the ownership rule that
// makes deferral necessary does not bind when there is no other user of
// mqttClient. Once setupMQTT() spawns the task, this becomes a no-op and the
// task takes the flag back over.
void serviceMQTTReconfigure() {
    if (mqttPublisherTaskCreated || !mqttReconfigurePending) {
        return;
    }
    mqttReconfigurePending = false;
    Serial.println("[MQTT] applying deferred reconfiguration from loop (publisher task not started)");
    setupMQTT();
}

void mqttPublisherTask(void *parameter) {
    mqtt_message_t msg;
    static uint32_t mqttStackLow = UINT32_MAX;

    for (;;) {
        checkStackHighWater(mqttStackLow);

        // Reconfigure here, between cycles, where this task is provably not
        // inside publish()/loop() and cannot be using the client's buffer.
        if (mqttReconfigurePending) {
            mqttReconfigurePending = false;
            Serial.println("[MQTT] applying deferred reconfiguration");
            setupMQTT();
        }

        if (wiFiMode == 'W' || ethernetConnected) {
            if (!mqttClient.connected() && !bleProvisioningActive && (int32_t)(millis() - mqttNextRetryMs) >= 0) {
                reconnectMQTT();
            }
            if (mqttClient.connected()) {
                // Publish pending schedule attribute with larger buffer (only when sending attributes)
                if (scheduleAttribPending && isCloudConnected) {
                    Serial.println("inside scheduleAttribPending and sending sechdule");

                    mqttClient.setBufferSize(ATTRIBUTE_MAX_PAYLOAD + 256);
                    mqttClient.publish(attribTopic, scheduleAttribPayload, false);
                    mqttClient.setBufferSize(MAX_PAYLOAD_LENGTH + 256);
                    scheduleAttribPending = false;
                    mqttClient.loop();
                }
                // Process multiple messages per loop to prevent queue buildup
                // Process up to 5 messages per loop cycle to handle bursts better
                int messagesProcessed = 0;

                // Peek-publish-consume: the message stays in the queue until publish()
                // succeeds AND the client still reports connected. If either check fails
                // we force a disconnect so the next tick re-establishes the session from
                // scratch — and the un-acked message is retried in order.
                while (messagesProcessed < 5 && mqttClient.connected() && xQueuePeek(mqttQueue, &msg, 0) == pdTRUE) {
                    unsigned long heldMs = millis() - msg.enqueuedMs;
                    bool pubOk = mqttClient.publish(msg.topic, msg.payload, msg.retained);
                    mqttClient.loop(); // drive keepalive / detect dead socket promptly
                                       // Serial.print("------------ After loop connection status -----");
                                       // Serial.println(mqttClient.connected());

                    if (pubOk && mqttClient.connected()) {
                        // Consume only after confirmed success.
                        xQueueReceive(mqttQueue, &msg, 0);
                        UBaseType_t depthAfter = uxQueueMessagesWaiting(mqttQueue);
                        // Serial.printf("[MQTT][Q] publish OK held=%lums depthAfter=%u topic=%s\n", heldMs,
                        //   (unsigned)depthAfter, msg.topic);
                        // Serial.println(msg.payload);
                        messagesProcessed++;
                        if (messagesProcessed < 5) {
                            vTaskDelay(pdMS_TO_TICKS(10));
                        }
                    } else {
                        UBaseType_t depthNow = uxQueueMessagesWaiting(mqttQueue);
                        Serial.printf("[MQTT][Q] publish FAIL held=%lums depth=%u topic=%s — retained in queue, "
                                      "forcing reconnect\n",
                                      heldMs, (unsigned)depthNow, msg.topic);
                        mqttClient.disconnect();
                        break; // exit drain; reconnectMQTT() runs on next task tick
                    }
                }

                // Push one pending replay entry when the live queue is idle.
                servicePendingReplay();

                // Push one core-dump chunk when both live queue and log replay are idle.
                serviceCoreDumpReplay();

                // Run client loop regularly regardless of queue state
                mqttClient.loop();
                // Serial.print("------------ After loop connection status -----");
                // Serial.println(mqttClient.connected());
            }
        }
        // Ensure we don't call loop() too frequently
        vTaskDelay(pdMS_TO_TICKS(MQTT_LOOP_INTERVAL));
    }
}
void initMQTTQueue(const char *broker, int port, const char *username, const char *password, const char *publishTopic,
                   const char *subscribeTopic) {
    // Create queue if not already created
    if (mqttQueue == NULL) {
        mqttQueue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(mqtt_message_t));
        if (mqttQueue != NULL) {
            Serial.println("MQTT queue creation successful");
        } else {
            Serial.println("MQTT queue creation failed");
            logDebugAction(DebugAction::MQTT_QUEUE_CREATION_FAILED, (char *)"MQTT queue creation failed");
            return;
        }
    }
    // Setup MQTT client
    mqttClient.setServer(broker, port);
    mqttClient.setCallback(mqttCallback);
    // 30s keepalive shrinks the zombie-socket detection window from ~2 min to ~60 s
    // worst case — PINGREQ every 15s, declared dead if no PINGRESP within 30s.
    mqttClient.setKeepAlive(30);
    // Buffer must fit topic + payload; ThingsBoard attribute (schedule) can be large
    mqttClient.setBufferSize(MAX_PAYLOAD_LENGTH + 256);

    // // Subscribe to topics if provided
    // if (subscribeTopic) {
    //     mqttClient.subscribe(subscribeTopic);
    //     Serial.println("Subscribed to topic: " + String(subscribeTopic));
    // }

    // Create task pinned to core if not already created
    if (!mqttPublisherTaskCreated) {
        xTaskCreatePinnedToCore(mqttPublisherTask, "MQTT_Publisher", MQTT_TASK_STACK_SIZE, NULL, MQTT_TASK_PRIORITY,
                                NULL, MQTT_TASK_CORE);
        mqttPublisherTaskCreated = true;
    }
}
bool queueMQTTMessage(const char *topic, String payload, boolean retained) {
    if (mqttQueue == NULL) {
        return false;
    }
    // Serial.println("Queueing MQTT message");
    mqtt_message_t msg;

    // Check lengths
    // if (strlen(topic) >= MAX_TOPIC_LENGTH || strlen(payload) >=
    // MAX_PAYLOAD_LENGTH) { if (strlen(topic) >= MAX_TOPIC_LENGTH ||
    // payload.length() >= MAX_PAYLOAD_LENGTH) {
    if (payload.length() >= MAX_PAYLOAD_LENGTH) {
        Serial.println("MQTT message too long");
        return false;
    }
    // Prepare message
    // Message topic is sent while calling the function itself. As for HomeAssistant, state topic varies
    // if (isCloudConnected) {
    //     strcpy(msg.topic, topic);
    // } else if (isGeneralMQTTEnabled) {
    //     if (mqttIntegrationType == "HA") {
    //         // Use the Home Assistant MQTT topic format
    //         strcpy(msg.topic, getHaStateTopic("sensor").c_str());
    //     } else {
    //         // Use the default MQTT topic format
    //         strcpy(msg.topic, mqttPublishTopic.c_str());
    //     }
    if (isCloudConnected || isGeneralMQTTEnabled) {
        strcpy(msg.topic, topic);
    } else {
        Serial.println("MQTT not enabled");
        return false;
    }
    strcpy(msg.payload, payload.c_str());
    msg.retained = retained;
    msg.enqueuedMs = millis();
    BaseType_t sent = xQueueSend(mqttQueue, &msg, pdMS_TO_TICKS(100));
    if (sent == pdTRUE) {
        UBaseType_t depth = uxQueueMessagesWaiting(mqttQueue);
        // Serial.printf("[MQTT][Q] enqueued, depth=%u topic=%s\n", (unsigned)depth, msg.topic);
    } else {
        Serial.printf("[MQTT][Q] enqueue FAILED (queue full) topic=%s\n", msg.topic);
    }
    return sent == pdTRUE;
}
// Attach VSMR 3-phase voltages to a telemetry payload - only while the link is
// alive (vsmrLinkGetVoltages returns false on a stale link, so old readings
// are never re-published). Takes a JsonVariant so callers can pass either a
// flat document root or a nested object (e.g. the "values" object of a
// timestamped {"ts":..,"values":{..}} payload).
void addVsmrTelemetry(JsonVariant values) {
    if (!vsmrLinkEnabled()) return;
    float vsmrRVolt, vsmrYVolt, vsmrBVolt;
    uint8_t vsmrFlags;
    bool vsmrRelay;
    if (vsmrLinkGetVoltages(vsmrRVolt, vsmrYVolt, vsmrBVolt, vsmrFlags, vsmrRelay)) {
        values["vsmrR"] = round(vsmrRVolt * 10.0) / 10.0;
        values["vsmrY"] = round(vsmrYVolt * 10.0) / 10.0;
        values["vsmrB"] = round(vsmrBVolt * 10.0) / 10.0;
        values["vsmrRelay"] = vsmrRelay;
        if (vsmrFlags) {
            values["vsmrFlags"] = vsmrFlags;
        }
    }
}

void handleMQTTMessaging() {
    // Serial.printf("handle MQTT message running on Core %d\n", xPortGetCoreID());
    if ((isCloudConnected || isGeneralMQTTEnabled) && (wiFiMode == 'W' || ethernetConnected)) {
#ifdef VOLT_LOG_ENABLED
        // Collect voltage reading every VOLT_LOG_INTERVAL ms
        unsigned long now = millis();
        if (now - lastVoltLogTime >= VOLT_LOG_INTERVAL) {
            lastVoltLogTime = now;
            if (voltLogCount < VOLT_LOG_MAX_SAMPLES) {
                voltLogBuffer[voltLogCount].voltage = round(voltageToOutput * 100) / 100.0;
                voltLogBuffer[voltLogCount].captureMillis = now;
                voltLogCount++;
            }
            // When buffer is full, send the batch as timestamped telemetry
            if (voltLogCount >= VOLT_LOG_MAX_SAMPLES) {
                DateTime rtcNow = getDateTimeFromRTC();
                unsigned long nowMs = millis();
                // Convert RTC local time to UTC milliseconds
                int64_t rtcUTCms = ((int64_t)rtcNow.unixtime() - (int64_t)(deviceTZOffset * 60)) * 1000LL;

                // Build ThingsBoard timestamped array: [{"ts":..,"values":{"volt2":..}}, ...]
                char voltLogPayload[1200];
                int offset = 0;
                offset += snprintf(voltLogPayload + offset, sizeof(voltLogPayload) - offset, "[");
                for (uint8_t i = 0; i < voltLogCount; i++) {
                    int64_t entryTs = rtcUTCms - (int64_t)(nowMs - voltLogBuffer[i].captureMillis);
                    if (i > 0)
                        offset += snprintf(voltLogPayload + offset, sizeof(voltLogPayload) - offset, ",");
                    offset += snprintf(voltLogPayload + offset, sizeof(voltLogPayload) - offset,
                                       "{\"ts\":%lld,\"values\":{\"volt2\":%.2f}}", entryTs, voltLogBuffer[i].voltage);
                }
                snprintf(voltLogPayload + offset, sizeof(voltLogPayload) - offset, "]");

                if (isCloudConnected) {
                    queueMQTTMessage(propTopic, voltLogPayload);
                }
                voltLogCount = 0;
            }
        }
#endif

        // When Relay is ON, publish data every few  seconds.
        // When relay is started, until a few seconds, publish it frequently

        // VSMR frame changed materially (>= 2 V on a single phase, or a
        // flags/relay change - detected in the RX callback): publish now,
        // bypassing the periodic throttles.
        static uint32_t lastVsmrChangePublished = 0;
        bool vsmrChanged = vsmrLinkEnabled() && (vsmrLinkChangeCount() != lastVsmrChangePublished);

        if ((relayState && (millis() - lastMQTTUpdateTime > MQTT_PUBLISH_INTERVAL || vsmrChanged))
            || (!relayState && countMQTTMessagesAfterRelayOff < 2)) {
            JsonDocument jsonDoc;
            char jsonBuffer[320];

            jsonDoc["voltage"] = round(voltageToOutput * 100) / 100.0;

            jsonDoc["current"] = round(currentToOutput * 1000) / 1000.0;
            jsonDoc["relayStatus"] = relayState ? true : false;
            jsonDoc["power"] = round(powerToOutput * 100) / 100.0;
            jsonDoc["energy"] = round(energyToOutput * 100.0) / 100.0;
            jsonDoc["cumEnergy"] = round((cumulativeEnergy + energyToOutput) * 100.0) / 100.0;
            addVsmrTelemetry(jsonDoc);
            lastVsmrChangePublished = vsmrLinkChangeCount();

            if (masterSlaveMode == 1) {
                jsonDoc["notifyRemotes"] = true;
                // Serial.println("notifyRemotes = true");
            }

            // remainingDuration is no longer streamed: widgets derive the countdown
            // from the ON event's `duration` telemetry (value + datapoint ts).
            // Todo - Add the following to the JSON, properly and test
            // if (relayState &&  calculatedScheduledOffTime !=
            // DateTime(0,0,0,0,0,0)){
            //   //jsonDoc["offTime"] = calculatedScheduledOffTime;
            //   jsonDoc["offTime"] =
            //   static_cast<int64_t>(calculatedScheduledOffTime.unixtime()-deviceTZOffset*60)
            //   * 1000; jsonDoc["offDuration"] =
            //   formatDuration(calculatedScheduledOffTime - getDateTimeFromRTC());
            // }
            ArduinoJson::serializeJson(jsonDoc, jsonBuffer);
            // Serial.println(jsonBuffer);

            size_t jsonDocLength = measureJson(jsonDoc);
            // Serial.println("Before publishing");

            if (isCloudConnected) {
                queueMQTTMessage(propTopic, jsonBuffer);
            } else if (isGeneralMQTTEnabled && mqttIntegrationType == "HA") {
                queueMQTTMessage(getHaStateTopic("sensor").c_str(), jsonBuffer);
            } else if (isGeneralMQTTEnabled) {
                queueMQTTMessage(mqttPublishTopic.c_str(), jsonBuffer);
            }
            // queueMQTTMessage(propTopic, (const char*)jsonBuffer);
            if (!relayState) {
                countMQTTMessagesAfterRelayOff++;
            }
            lastMQTTUpdateTime = millis();

        } else if (!relayState && (millis() - lastMQTTUpdateTime > MQTT_PUBLISH_INTERVAL * 2 || vsmrChanged)) {
            // Publish the OFF status to the MQTT broker
            static float lastSentVoltage = 0.0;
            static unsigned long lastSentTime = 0;
            float currentVoltage = round(voltageToOutput * 100) / 100.0;
            unsigned long now = millis();

            bool shouldSend = false;
            if (lastSentVoltage == 0.0 || highVoltageAlertState || lowVoltageAlertState) {
                shouldSend = true;
            } else if (abs(currentVoltage - lastSentVoltage) / lastSentVoltage > 0.01) {
                shouldSend = true;
            } else if (vsmrChanged) {
                // VSMR frame changed materially - push the new readings now
                shouldSend = true;
            } else if (now - lastSentTime > 10UL * 60UL * 1000UL) { // 10 minutes
                shouldSend = true;
            }

            if (shouldSend) {
                JsonDocument jsonDoc;
                char jsonBuffer[320];
                jsonDoc["relayStatus"] = false;
                jsonDoc["voltage"] = currentVoltage;
                addVsmrTelemetry(jsonDoc);
                lastVsmrChangePublished = vsmrLinkChangeCount();
                ArduinoJson::serializeJson(jsonDoc, jsonBuffer);
                // Serial.println(jsonBuffer);
                if (isCloudConnected) {
                    queueMQTTMessage(propTopic, jsonBuffer);
                } else if (isGeneralMQTTEnabled && mqttIntegrationType == "HA") {
                    queueMQTTMessage(getHaStateTopic("sensor").c_str(), jsonBuffer);
                } else if (isGeneralMQTTEnabled) {
                    queueMQTTMessage(mqttPublishTopic.c_str(), jsonBuffer);
                }
                lastSentVoltage = currentVoltage;
                lastSentTime = now;
                lastMQTTUpdateTime = now;
            }
        }

        if (sendMQTTAttrib == true) {
            sendMQTTAttribMessage();
            sendMQTTAttrib = false;
        }

        // The schedule status the cloud widget shows under the switch changes on a
        // relay start (including one a fault refused) or a stop; handleRelayOn() and
        // handleRelayOFF() raise the flag and it is published here, like
        // sendMQTTAttrib above. Both handlers run earlier in the same loop pass and
        // complete their own bookkeeping (manualOff, fault details) before
        // returning, so the code read here describes the finished event.
        // reconnectMQTT() raises it too, so a fresh connection resends the status.
        // Left pending while the cloud is down - whatever happened offline is
        // published once the link is back.
        if (schStatusPublishPending && isCloudConnected) {
            schStatusPublishPending = false;
            // Running: nothing is interrupted and no duration is owed.
            publishScheduleStatusToCloud(relayState ? "" : getScheduleStatusCode(getDateTimeFromRTC()),
                                         nextEvent.schEndTime);
        }

    } else {
        // Serial.println("MQTT disconnected");
    }
}

void publishScheduleAttributeToCloud() {
    if (!isCloudConnected || !mqttClient.connected()) {
        return;
    }
    Serial.println("inside publishScheduleAttributeToCloud");
    String scheduleJson = getScheduleJsonString(gScheduleArray);
    JsonDocument attrDoc;
    attrDoc["schedule"] = scheduleJson;
    String attribPayload;
    serializeJson(attrDoc, attribPayload);
    size_t len = attribPayload.length();
    if (len > 0 && len < ATTRIBUTE_MAX_PAYLOAD) {
        // Use dedicated buffer and flag so MQTT task publishes with larger buffer only when sending this
        strncpy(scheduleAttribPayload, attribPayload.c_str(), ATTRIBUTE_MAX_PAYLOAD - 1);
        scheduleAttribPayload[ATTRIBUTE_MAX_PAYLOAD - 1] = '\0';
        scheduleAttribLen = len;
        scheduleAttribPending = true;
    } else if (len >= ATTRIBUTE_MAX_PAYLOAD) {
        Serial.println("Schedule payload too long for attribute");
    }
}

// The current-schedule status the cloud widget shows under the switch - the same
// code the portal gets over the websocket. Published from handleMQTTMessaging()
// when schStatusPublishPending is raised - a relay start or stop, or a fresh
// connection - so every call here is already a wanted publish and no
// "has it changed" bookkeeping is kept.
//
// schStatusEnd is the window this status belongs to, as a UTC epoch (seconds).
// Attributes persist in the cloud long after the device stops reporting, so the
// widget needs to know when the status stopped describing "now" - without it, a
// fault from last Tuesday would still be on screen today.
void publishScheduleStatusToCloud(const char *status, DateTime windowEnd) {
    if (!isCloudConnected || mqttQueue == NULL) {
        return;
    }
    // "NEXT_SCHEDULE" is not a status - the widget works the upcoming window out
    // of the schedule attribute itself - so it clears the line like "" does.
    if (status == NULL || strcmp(status, "NEXT_SCHEDULE") == 0) {
        status = "";
    }
    // The RTC runs on local time; the logs publish UTC the same way.
    uint32_t endTs = (status[0] == '\0') ? 0 : (uint32_t)(windowEnd.unixtime() - deviceTZOffset * 60);

    JsonDocument jsonDoc;
    char jsonBuffer[160];
    jsonDoc["schStatus"] = status;
    jsonDoc["schStatusEnd"] = endTs;
    size_t len = serializeJson(jsonDoc, jsonBuffer);
    if (len > 0 && len < MAX_PAYLOAD_LENGTH) {
        queueMQTTMessage(attribTopic, jsonBuffer);
    }
}

void publishDeviceInfoAttributesOnConnect() {
    if (!isCloudConnected || mqttQueue == NULL) {
        return;
    }
    JsonDocument jsonDoc;
    char jsonBuffer[384];

    jsonDoc["firmwareMajor"] = (int)FIRMWARE_MAJOR;
    jsonDoc["firmwareMinor"] = (int)FIRMWARE_MINOR;
    jsonDoc["firmwarePatch"] = (int)FIRMWARE_PATCH;
    jsonDoc["firmwareVersion"] = getFirmwareVersion().c_str();
    jsonDoc["deviceName"] = getSwitchName();
    // Tri-state: 0 = single phase, 1 = 3-phase, 2 = 3-phase with VSMR.
    // Old dashboards treating this as boolean still work (1/2 are truthy).
    jsonDoc["3Phase"] = isThreePhaseDevice ? (vsmrLinkEnabled() ? 2 : 1) : 0;

    size_t len = serializeJson(jsonDoc, jsonBuffer);
    if (len > 0 && len < MAX_PAYLOAD_LENGTH) {
        if (queueMQTTMessage(attribTopic, jsonBuffer)) {
            Serial.println("Device info attributes queued (firmware, deviceName)");
        }
    }
}

void publishRtcHealthToCloud() {
    if (!isCloudConnected || mqttQueue == NULL)
        return;
    Serial.println("Sending publishRtcHealthToCloud");
    JsonDocument jsonDoc;
    char jsonBuffer[128];
    jsonDoc["rtcBattLow"] = rtcBatteryLow;       // true => coin cell needs replacement
    jsonDoc["rtcLostPwrCnt"] = rtcLostPowerCount; // lifetime oscillator-stop count
    jsonDoc["rtcDriftSec"] = rtcLastDriftSec;     // last NTP-vs-RTC delta (neg = RTC behind)
    size_t len = serializeJson(jsonDoc, jsonBuffer);
    if (len > 0 && len < MAX_PAYLOAD_LENGTH) {
        if (queueMQTTMessage(attribTopic, jsonBuffer)) {
            Serial.println("RTC health attributes queued");
        }
        else {
            Serial.print("Queue failed");

        }

    }
}

void publishAdminSettingsToCloud() {
    if (!isCloudConnected || mqttQueue == NULL)
        return;
    JsonDocument adoc;
    adoc["3p"] = isThreePhaseDevice ? (vsmrLinkEnabled() ? "2" : "1") : "0";
    adoc["vsmrMac"] = vsmrLinkPeerMacStr();
    adoc["tapR"] = tappingRequired;
    adoc["tapD"] = tappingDuration;
    adoc["ethE"] = ethernetEnabled ? "1" : "0";
    adoc["leakE"] = leakDetectionEnabled ? "1" : "0";
    adoc["ledT"] = ledType;
    adoc["sOrC"] = shuntOrCT;
    adoc["inRush"] = inRushExtraSecs;
    String admStr;
    serializeJson(adoc, admStr);
    JsonDocument attrDoc;
    attrDoc["admS"] = admStr;
    // The widgets switch their layout on the "3Phase" attribute, not on admS, and
    // publishDeviceInfoAttributesOnConnect() only sends it at connect time. Repeat it
    // here so a phase change made from the portal or the admin widget reaches the
    // dashboard straight away instead of waiting for the next MQTT reconnect.
    // Tri-state, same encoding as on connect: 0 = single phase, 1 = 3-phase,
    // 2 = 3-phase with VSMR.
    attrDoc["3Phase"] = isThreePhaseDevice ? (vsmrLinkEnabled() ? 2 : 1) : 0;
    char buf[512];
    size_t len = serializeJson(attrDoc, buf, sizeof(buf));
    if (len > 0 && len < MAX_PAYLOAD_LENGTH) {
        if (queueMQTTMessage(attribTopic, buf)) {
            Serial.println("admS attribute queued");
        }
    }
}

void publishTimingConfigToCloud() {
    if (!isCloudConnected || mqttQueue == NULL)
        return;
    String tmJson = getDeviceTimingConfig();
    JsonDocument attrDoc;
    attrDoc["tmS"] = tmJson;
    char buf[512];
    size_t len = serializeJson(attrDoc, buf, sizeof(buf));
    if (len > 0 && len < MAX_PAYLOAD_LENGTH) {
        if (queueMQTTMessage(attribTopic, buf)) {
            Serial.println("tmS attribute queued");
        }
    }
}

static bool verifyAdminPinFromJsonDoc(JsonDocument &doc) {
    if (!doc["pin"].is<const char *>())
        return false;
    const char *pass = doc["pin"];
#if SPECIAL_PASSWORD == 1
    if (strcmp(pass, SPECIAL_ADMIN_PASSWORD) == 0)
        return true;
#endif
    return strcmp(pass, ADMIN_PASSWORD) == 0;
}

// Clears a previously published admErr.
//
// admErr is a CLIENT attribute, so it persists in the cloud until something
// overwrites it - there is no expiry. Left alone, an error from one failed save
// (a mistyped admin PIN is the easy way in) is re-read by the admin widget on
// every LATER save and reported as a rejection, because the widget checks
// admErr before it compares the echoed settings. The device then looks broken
// while saving perfectly. Publishing an empty value on success retires it.
static void clearAdminErrorInCloud() {
    if (!isCloudConnected || mqttQueue == NULL)
        return;
    JsonDocument attrDoc;
    attrDoc["admErr"] = "";
    char buf[64];
    size_t len = serializeJson(attrDoc, buf, sizeof(buf));
    if (len > 0)
        queueMQTTMessage(attribTopic, buf);
}

static void publishAdminErrorToCloud(const char *key, const char *msg) {
    if (!isCloudConnected || mqttQueue == NULL)
        return;
    JsonDocument errDoc;
    errDoc["k"] = key;
    errDoc["m"] = msg;
    String errStr;
    serializeJson(errDoc, errStr);
    JsonDocument attrDoc;
    attrDoc["admErr"] = errStr;
    char buf[200];
    size_t len = serializeJson(attrDoc, buf, sizeof(buf));
    if (len > 0)
        queueMQTTMessage(attribTopic, buf);
}

static void applyAdminSettingsFromCloud(const String &admStr) {
    JsonDocument adoc;
    if (deserializeJson(adoc, admStr) != DeserializationError::Ok) {
        publishAdminErrorToCloud("admS", "Bad JSON");
        return;
    }
    if (!verifyAdminPinFromJsonDoc(adoc)) {
        publishAdminErrorToCloud("admS", "Bad pin");
        return;
    }
    // "3p": 0 = not 3-phase, 1 = 3-phase, 2 = 3-phase WITH VSMR link.
    bool threePhase = (adoc["3p"] == "1" || adoc["3p"] == "2");
    bool vsmrLinkOn = (adoc["3p"] == "2");
    String vsmrMac = adoc["vsmrMac"] | "";   // paired VSMR MAC ("" = any)
    uint8_t tapRequired = adoc["tapR"].as<uint8_t>();
    uint8_t tapDuration = adoc["tapD"].as<uint8_t>();
    uint8_t ledTypeTemp = adoc["ledT"].as<uint8_t>();
    String shuntOrCTTemp = adoc["sOrC"].as<String>();
    bool ethernetSet = (adoc["ethE"] == "1");
    bool leakDetSet = (adoc["leakE"] == "1");
    // Preserve the existing inrush value if a sender omits the key.
    uint8_t inRush = adoc["inRush"].isNull() ? inRushExtraSecs : adoc["inRush"].as<uint8_t>();
    if (inRush > 120) {
        publishAdminErrorToCloud("admS", "Invalid inrush settle duration (max 120)");
        return;
    }
    save3PhaseSettings(threePhase, tapRequired, tapDuration, ethernetSet, ledTypeTemp, shuntOrCTTemp, leakDetSet, inRush, vsmrLinkOn, vsmrMac);
    setVsmrLinkEnabled(vsmrLinkOn);       // apply runtime after persisting
    vsmrLinkSetPeerMac(vsmrMac.c_str());  // update MAC filter at runtime
    inRushExtraSecs = inRush;
    isThreePhaseDevice = threePhase;
    tappingRequired = tapRequired;
    tappingDuration = tapDuration;
    ethernetEnabled = ethernetSet;
    ledType = ledTypeTemp;
    shuntOrCT = shuntOrCTTemp;
    leakDetectionEnabled = leakDetSet;
    if (leakDetectionEnabled) {
        pinMode(LEAK_DETECTION, INPUT_PULLDOWN);
        attachInterrupt(digitalPinToInterrupt(LEAK_DETECTION), handleLeakagePinSpike, RISING);
    } else {
        detachInterrupt(digitalPinToInterrupt(LEAK_DETECTION));
    }
    adoc.remove("pin");
    String sanitizedData;
    serializeJson(adoc, sanitizedData);
    // Prefix marks the origin: api.cpp logs the same DebugAction for the local
    // portal path, so without this the log viewer cannot tell the two apart.
    sanitizedData = "Cloud: " + sanitizedData;
    logDebugAction(DebugAction::ADMIN_3PHASE_SETTINGS_SUCCESS, (char *)sanitizedData.c_str());
    clearAdminErrorInCloud(); // retire any error from an earlier failed save
    publishAdminSettingsToCloud();
}

static void applyTimingConfigFromCloud(const String &tmStr) {
    JsonDocument tdoc;
    if (deserializeJson(tdoc, tmStr) != DeserializationError::Ok) {
        publishAdminErrorToCloud("tmS", "Bad JSON");
        return;
    }
    if (!verifyAdminPinFromJsonDoc(tdoc)) {
        publishAdminErrorToCloud("tmS", "Bad pin");
        return;
    }
    String passthrough;
    serializeJson(tdoc, passthrough);
    if (!saveDeviceTimingConfig((unsigned char *)passthrough.c_str())) {
        publishAdminErrorToCloud("tmS", "Save failed");
        return;
    }
    tdoc.remove("pin");
    String sanitizedData;
    serializeJson(tdoc, sanitizedData);
    // See applyAdminSettingsFromCloud() - same DebugAction is used by the local
    // portal handler in api.cpp, so tag the cloud-originated one.
    sanitizedData = "Cloud: " + sanitizedData;
    logDebugAction(DebugAction::ADMIN_BUTTONTIME_SETTINGS_SUCCESS,  (char *)sanitizedData.c_str());
    clearAdminErrorInCloud(); // retire any error from an earlier failed save
    publishTimingConfigToCloud();
}

void publishPowerSettingsToCloud() {
    if (!isCloudConnected || mqttQueue == NULL)
        return;
    JsonDocument pdoc = getCurSettings();
    String pStr;
    serializeJson(pdoc, pStr);
    JsonDocument attrDoc;
    attrDoc["pSettings"] = pStr;
    attrDoc["hVoltLt"] = highVoltageLimit;
    attrDoc["lVoltLt"] = lowVoltageLimit;
    char buf[1024];
    size_t len = serializeJson(attrDoc, buf, sizeof(buf));
    if (len > 0 && len < MAX_PAYLOAD_LENGTH) {
        if (queueMQTTMessage(attribTopic, buf)) {
            Serial.println("pSettings attribute queued");
        }
    }
}

static void publishPowerErrorToCloud(const char *msg) {
    if (!isCloudConnected || mqttQueue == NULL)
        return;
    JsonDocument errDoc;
    errDoc["k"] = "pSettings";
    errDoc["m"] = msg;
    String errStr;
    serializeJson(errDoc, errStr);
    JsonDocument attrDoc;
    attrDoc["pSetErr"] = errStr;
    char buf[200];
    size_t len = serializeJson(attrDoc, buf, sizeof(buf));
    if (len > 0)
        queueMQTTMessage(attribTopic, buf);
}

static void applyPowerSettingsFromCloud(const String &pStr) {
    JsonDocument pdoc;
    if (deserializeJson(pdoc, pStr) != DeserializationError::Ok) {
        publishPowerErrorToCloud("Bad JSON");
        return;
    }
    if (pdoc["drCur"].isNull() || pdoc["orCur"].isNull() || pdoc["lVol"].isNull() || pdoc["hVol"].isNull()) {
        publishPowerErrorToCloud("Missing current/voltage limits");
        return;
    }
    float drCur = pdoc["drCur"].as<float>();
    float orCur = pdoc["orCur"].as<float>();
    float lVol = pdoc["lVol"].as<float>();
    float hVol = pdoc["hVol"].as<float>();
    float thCur = pdoc["thCur"].as<float>();
    uint8_t drMin = pdoc["drMin"].as<uint8_t>();
    uint8_t orMin = pdoc["orMin"].as<uint8_t>();
    uint8_t lVolMin = pdoc["lVolMin"].as<uint8_t>();
    uint8_t hVolMin = pdoc["hVolMin"].as<uint8_t>();
    uint8_t stDly = pdoc["stDly"].as<uint8_t>();
    uint16_t maxRunTimeMins = pdoc["mR"].as<uint16_t>();
    uint16_t breakTimeMins = pdoc["bT"].as<uint16_t>();

    if (drCur < 0 || orCur < 0 || lVol < 0 || hVol < 0 || thCur < 0) {
        publishPowerErrorToCloud("Invalid current/voltage values");
        return;
    }
    if (drCur < thCur) {
        publishPowerErrorToCloud("Dryrun threshold must be less than dryrun current");
        return;
    }
    if (drMin > 240 || orMin > 240 || lVolMin > 240 || hVolMin > 240) {
        publishPowerErrorToCloud("Invalid retry minutes");
        return;
    }
    if (stDly > 30) {
        publishPowerErrorToCloud("Invalid restart delay");
        return;
    }
    if (maxRunTimeMins > 1440 || breakTimeMins > 1440) {
        publishPowerErrorToCloud("Invalid max-run/break time (max 1440)");
        return;
    }

    saveCurLimit(pdoc);
    isPLimitSet = true;
    savePreference((char *)"isPLimitSet", true);
    // "Cloud: " marks the origin - api.cpp logs the same DebugAction for the
    // local portal path. The values go in as a bare CSV rather than the full
    // JSON: quotes and field names cost ~60 of the 200-byte DEBUG_MESSAGE_SIZE
    // budget and would truncate the tail, while the order below is fixed.
    // The fields follow the Protection Settings screen top to bottom, so a log
    // line can be read straight against the form:
    //   drCur,isRstDr,drMin,thCur, orCur,isRstOl,orMin, lVol,isRstLv,lVolMin,
    //   hVol,isRstHv,hVolMin, isRst,stDly, mR,bT
    // The restart/retry enables come from the globals saveCurLimit() has just
    // written, so they log what was actually applied.
    char dbgPayload[DEBUG_MESSAGE_SIZE];
    snprintf(dbgPayload, sizeof(dbgPayload), "P-Cld: %g,%u,%u,%g-%g,%u,%u-%g,%u,%u-%g,%u,%u-%u,%u,%u,%u", drCur,
             (unsigned)(restartOptions >> 1 & 0x01), (unsigned)drMin, thCur, orCur,
             (unsigned)(restartOptions >> 2 & 0x01), (unsigned)orMin, lVol, (unsigned)(restartOptions >> 3 & 0x01),
             (unsigned)lVolMin, hVol, (unsigned)(restartOptions >> 4 & 0x01), (unsigned)hVolMin,
             (unsigned)isRestartEnabled, (unsigned)stDly, (unsigned)maxRunTimeMins, (unsigned)breakTimeMins);
    logDebugAction(DebugAction::API_SET_CURRENT_LIMIT, dbgPayload);
    publishPowerSettingsToCloud();
}

void sendMQTTAttribMessage() {
    // The following codes need to checked and enabled when we are ready to shwo the remaining time and next schedule in
    // cloud UI. In some cases empty message is sent to the cloud UI, need to check and fix it.
    /*
    if ((isCloudConnected || isGeneralMQTTEnabled) && (wiFiMode == 'W' || ethernetConnected)) {
        // When Relay is ON, publish data every few  seconds.
        // When relay is started, until a few seconds, publish it frequently
        JsonDocument jsonDoc;
        char jsonBuffer[256];

        if (relayState ) {
            if (calculatedScheduledOffTime != DateTime(0, 0, 0, 0, 0, 0)) {
                String offTime = formatDateTime(calculatedScheduledOffTime);
                jsonDoc["offTime"] = offTime;
                //jsonDoc["offDuration"] = formatDuration(calculatedScheduledOffTime - getDateTimeFromRTC());
            }
        } else if (!relayState ) {
            // Publish the OFF status to the MQTT broker
            if (isScheduleSet && nextEvent.startTime != DateTime(0, 0, 0, 0, 0, 0)) {
                jsonDoc["upSchST"] = formatDateTime(nextEvent.startTime);
                jsonDoc["upSchET"] = formatDateTime(nextEvent.endTime);
            }
        }
        ArduinoJson::serializeJson(jsonDoc, jsonBuffer);
        // Serial.println(jsonBuffer);
        if (isCloudConnected) {
            queueMQTTMessage(attribTopic, jsonBuffer);
        } else if (isGeneralMQTTEnabled) {
            queueMQTTMessage(mqttPublishTopic.c_str(), jsonBuffer);
        }
    } else {
        // Serial.println("MQTT disconnected");
    }
    */
}

void sendMQTTRemoteSwitchMessage(String switchStatus, uint8_t buttonNo, bool shouldLog) {
    // Serial.printf("handle MQTT message running on Core %d\n", xPortGetCoreID());
    if ((isCloudConnected || isGeneralMQTTEnabled) && (wiFiMode == 'W' || ethernetConnected)) {
        JsonDocument jsonDoc;
        char jsonBuffer[256];

        jsonDoc["remoteSwitching"] = switchStatus;
        // Only add buttonNo for ON commands if it's a valid button number (1-4 or 11)
        // buttonNo 0 is only valid for OFF commands
        if (switchStatus == "ON" && ((buttonNo >= 1 && buttonNo <= 4) || buttonNo == 11)) {
            jsonDoc["buttonNo"] = buttonNo;
        } else if (buttonNo == 0) {
            logDebugAction(DebugAction::SYSTEM_FAILURE, (char *)"0 buttonNo is not valid for ON commands");
        } else {
            String msg = "Invalid Button No : " + buttonNo;
            logDebugAction(DebugAction::SYSTEM_FAILURE, (char *)msg.c_str());
        }
        ArduinoJson::serializeJson(jsonDoc, jsonBuffer);
        // Serial.println(jsonBuffer);

        size_t jsonDocLength = measureJson(jsonDoc);
        // Serial.println(jsonBuffer);

        if (isCloudConnected) {
            queueMQTTMessage(propTopic, jsonBuffer);
            // Serial.println(jsonBuffer);
        } else if (isGeneralMQTTEnabled && mqttIntegrationType == "HA") {
            queueMQTTMessage(getHaStateTopic("sensor").c_str(), jsonBuffer);
        } else if (isGeneralMQTTEnabled) {
            queueMQTTMessage(mqttPublishTopic.c_str(), jsonBuffer, true);
        }

        // Log the message sent to master via MQTT only if shouldLog is true
        if (shouldLog) {
            if (switchStatus == "ON") {
                logAction(ModeOfAction::START_MASTER_MQTT_SUCCESS, 0, 0, 0, ScheduleType::SCH_NONE);
            } else if (switchStatus == "OFF") {
                logAction(ModeOfAction::STOP_MASTER_MQTT_SUCCESS, 0, 0, 0, ScheduleType::SCH_NONE);
            }
        }
    } else {
        // Serial.println("MQTT disconnected");
    }
}
String getSwitchNameForHa() {
    String switchName = getSwitchName();
    switchName.toLowerCase();
    switchName.replace(" ", "_");
    switchName.replace("-", "_");
    return switchName;
}
String getUniqueSwitchNameForHa() {
    String switchName = getSwitchName();
    switchName.toLowerCase();
    switchName.replace(" ", "_");
    switchName.replace("-", "_");

    String macAddress = WiFi.macAddress();
    String macLast4 = macAddress.substring(macAddress.length() - 5);
    macLast4.replace(":", "");

    return switchName + "_" + macLast4;
}
// void sendAbnormalEventMessage() {
//     if ((isCloudConnected || isGeneralMQTTEnabled) && (wiFiMode == 'W' || ethernetConnected)) {
//         // When Relay is ON, publish data every few  seconds.
//         // When relay is started, until a few seconds, publish it frequently
//         JsonDocument jsonDoc;
//         char jsonBuffer[256];

//         if (relayState ) {
//             if (calculatedScheduledOffTime != DateTime(0, 0, 0, 0, 0, 0)) {
//                 String offTime = formatDateTime(calculatedScheduledOffTime);
//                 jsonDoc["offTime"] = offTime;
//                 jsonDoc["offDuration"] = formatDuration(calculatedScheduledOffTime - getDateTimeFromRTC());
//             }
//         } else if (!relayState ) {
//             // Publish the OFF status to the MQTT broker
//             if (isScheduleSet && nextEvent.startTime != DateTime(0, 0, 0, 0, 0, 0)) {
//                 jsonDoc["upSchST"] = formatDateTime(nextEvent.startTime);
//                 jsonDoc["upSchET"] = formatDateTime(nextEvent.endTime);
//             }
//         }
//         ArduinoJson::serializeJson(jsonDoc, jsonBuffer);
//         Serial.println(jsonBuffer);
//         if (isCloudConnected) {
//             queueMQTTMessage(attribTopic, jsonBuffer);
//         } else if (isGeneralMQTTEnabled) {
//             queueMQTTMessage(mqttPublishTopic.c_str(), jsonBuffer);
//         }
//     } else {
//         // Serial.println("MQTT disconnected");
//     }
// }

void mqttCallback(char *topic, byte *payload, unsigned int length) {
    // Serial.print("MQTT Message arrived [");
    // Serial.print(topic);
    // Serial.print("] ");

    // Build the payload as a String in one allocation. The previous
    // byte-by-byte `+=` loop did O(n) reallocations under fragmented heap;
    // reserve() + concat() makes it a single grow.
    String incoming;
    incoming.reserve(length + 1);
    incoming.concat((const char *)payload, length);
    // Serial.println(incoming);
    JsonDocument doc;
    deserializeJson(doc, incoming);
    static uint8_t runHours, runMinutes = 0;
    static unsigned long durationHoursMillis, durationMinutesMillis = 0;

    // Shared attribute update from ThingsBoard (schedule changed at server)
    if (strcmp(topic, attribListenTopic) == 0) {
        // ThingsBoard schedule attribute can arrive as:
        // - {"schedule":"{\"s\":[...]}" }  (schedule stored as escaped JSON string)
        // - {"schedule":{"s":[...]}}      (schedule stored as JSON object)
        // - {"s":[...]}                  (raw schedule object)
        if (!doc["schedule"].isNull()) {
            JsonDocument schedDoc;
            if (doc["schedule"].is<const char *>()) {
                DeserializationError err = deserializeJson(schedDoc, doc["schedule"].as<const char *>());
                if (err == DeserializationError::Ok && schedDoc["s"].is<JsonArray>()) {
                    saveScheduleToEEPROM(schedDoc);
                    logDebugActionPartSplit(payload, length, "Cloud");
                } else {
                    Serial.print("schedule parse failed or missing array 's'. err=");
                    Serial.println(err.c_str());
                }
            } else if (doc["schedule"].is<JsonObject>()) {
                // Copy object into its own doc so we can pass it to saveScheduleToEEPROM()
                String tmp;
                serializeJson(doc["schedule"], tmp);
                DeserializationError err = deserializeJson(schedDoc, tmp);
                if (err == DeserializationError::Ok && schedDoc["s"].is<JsonArray>()) {
                    saveScheduleToEEPROM(schedDoc);
                    logDebugActionPartSplit(payload, length, "Cloud");
                    
                } else {
                    Serial.print("schedule object copy/parse failed or missing array 's'. err=");
                    Serial.println(err.c_str());
                }
            }
        } else if (doc["s"].is<JsonArray>()) {
            saveScheduleToEEPROM(doc);
            logDebugActionPartSplit(payload, length, "Cloud");

        }

        // Check for OTA firmware shared attributes (fw_title, fw_version, etc.)
        if (doc.containsKey("fw_title") || doc.containsKey("fw_version") || doc.containsKey("fw_size")
            || doc.containsKey("fw_checksum")) {
            processOTASharedAttributes(doc);
        }

        // admS — admin/circuit settings update from cloud (PIN required inside payload)
        if (!doc["admS"].isNull()) {
            String admStr;
            if (doc["admS"].is<const char *>()) {
                admStr = doc["admS"].as<const char *>();
            } else if (doc["admS"].is<JsonObject>()) {
                serializeJson(doc["admS"], admStr);
            }
            if (admStr.length() > 0)
                applyAdminSettingsFromCloud(admStr);
        }

        // tmS — timing config update from cloud (PIN required inside payload)
        if (!doc["tmS"].isNull()) {
            String tmStr;
            if (doc["tmS"].is<const char *>()) {
                tmStr = doc["tmS"].as<const char *>();
            } else if (doc["tmS"].is<JsonObject>()) {
                serializeJson(doc["tmS"], tmStr);
            }
            if (tmStr.length() > 0)
                applyTimingConfigFromCloud(tmStr);
        }

        // pSettings — protection/power settings update from cloud (no PIN, regular user setting)
        if (!doc["pSettings"].isNull()) {
            String pStr;
            if (doc["pSettings"].is<const char *>()) {
                pStr = doc["pSettings"].as<const char *>();
            } else if (doc["pSettings"].is<JsonObject>()) {
                serializeJson(doc["pSettings"], pStr);
            }
            if (pStr.length() > 0)
                applyPowerSettingsFromCloud(pStr);
        }
        return;
    }

    // Check if the topic is an RPC request
    if (String(topic).startsWith("v1/devices/me/rpc/request/")) {
        // Extract requestId
        String requestId = String(topic).substring(26); // "v1/devices/me/rpc/request/" is 26 chars
        // Parse payload and execute the requested method

        String method = doc["method"];
        if (method == "turnOn") {
            String state = doc["params"];
            bool stateBool = doc["params"];
            if (state == "true" || stateBool) {
                if (!relayState) {
                    if ((doc["params"]["durationHours"].is<int>() || doc["params"]["durationHours"].is<const char *>())
                        && (doc["params"]["durationMinutes"].is<int>()
                            || doc["params"]["durationMinutes"].is<const char *>())) {
                        // This part - iot.EVIQtech sends duration along with the turnOn command
                        uint8_t runHours = doc["params"]["durationHours"].is<int>()
                                               ? doc["params"]["durationHours"].as<uint8_t>()
                                               : String(doc["params"]["durationHours"].as<const char *>()).toInt();
                        uint8_t runMinutes = doc["params"]["durationMinutes"].is<int>()
                                                 ? doc["params"]["durationMinutes"].as<uint8_t>()
                                                 : String(doc["params"]["durationMinutes"].as<const char *>()).toInt();
                        TimeSpan duration(runHours * 3600 + runMinutes * 60);
                        if (duration.totalseconds() < MAX_RUNTIME_HOURS_WEB * 60 * 60 && duration.totalseconds() > 0) {
                            onTriggerFromMQTT = 1;
                            onTriggerDurationFromMQTT = duration.totalseconds();
                        } else {
                            // Send failure acknowledgment
                            String responseTopic = "v1/devices/me/rpc/response/" + requestId;
                            // mqttClient.publish(responseTopic.c_str(), "{\"success\":false, \"message\":\"Invalid
                            // Duration\"}");
                            queueMQTTMessage(responseTopic.c_str(),
                                             "{\"success\":false, \"message\":\"Invalid Duration\"}");
                        }
                    } else {
                        // Things board cloud does not send the duration along with the turnOn command, so need to
                        // request
                        String requestPayload = "{\"sharedKeys\":\"durationMinutes, durationHours\"}";
                        String topic = attribRequestTopic + requestId + "1";
                        Serial.println(topic);
                        if (queueMQTTMessage(topic.c_str(), requestPayload.c_str())) {
                            Serial.println("Attribute request sent");
                        } else {
                            Serial.println("Failed to send attribute request");
                        }
                        requestPayload = "{\"sharedKeys\":\"durationHours, durationMinutes\"}";
                        topic = attribRequestTopic + requestId + "2";
                        Serial.println(topic);
                        if (queueMQTTMessage(topic.c_str(), requestPayload.c_str())) {
                            Serial.println("Attribute request sent");
                        } else {
                            Serial.println("Failed to send attribute request");
                        }
                        onTriggerFromMQTTStart = millis();
                    }
                    String responseTopic = "v1/devices/me/rpc/response/" + requestId;
                    Serial.println(responseTopic);
                } else {
                    // Send failure acknowledgment
                    String responseTopic = "v1/devices/me/rpc/response/" + requestId;
                    // mqttClient.publish(responseTopic.c_str(), "{\"success\":false, \"message\":\"Invalid state\"}");
                    queueMQTTMessage(responseTopic.c_str(), "{\"success\":false, \"message\":\"Device already ON\"}");
                }
            }
        } else if (method == "turnOff") {
            String state = doc["params"];
            if (state == "false") {
                if (relayState) {
                    offTriggerFromMQTT = 1;
                    String responseTopic = "v1/devices/me/rpc/response/" + requestId;
                    Serial.println(responseTopic);
                } else {
                    // Send failure acknowledgment
                    String responseTopic = "v1/devices/me/rpc/response/" + requestId;
                    // mqttClient.publish(responseTopic.c_str(), "{\"success\":false, \"message\":\"Invalid state\"}");
                    queueMQTTMessage(responseTopic.c_str(), "{\"success\":false, \"message\":\"Device already OFF\"}");
                }
            }
        } else if (method == "remoteSwitch") {
            String state = doc["params"];
            bool stateBool = doc["params"];
            if (state == "true" || stateBool) {
                if ((doc["params"]["remoteSwitching"].is<const char *>()) && (masterSlaveMode == 1)) {
                    // This part - iot.EVIQtech sends duration along with the turnOn command
                    String remoteSwitching = doc["params"]["remoteSwitching"].as<const char *>();
                    uint8_t buttonNo =
                        doc["params"]["buttonNo"].is<uint8_t>() ? doc["params"]["buttonNo"].as<uint8_t>() : 0;
                    if (remoteSwitching == "ON" && ((buttonNo > 0 && buttonNo < 5) || buttonNo == 11)) {
                        if (!relayState) {
                            // Consider this as a Remote Switch trigger
                            //    TimeSpan duration = getDuration(buttonNo);
                            onTriggerDurationFromWebhook = getDuration(buttonNo);
                            if (onTriggerDurationFromWebhook.totalseconds() > 0)
                                onTriggerFromWebhook = 1;
                            if (onTriggerDurationFromWebhook.totalseconds() == 0) {
                                logDebugAction(DebugAction::SYSTEM_FAILURE,
                                               (char *)"onTriggerDurationFromWebhook is 0");
                            }
                        } else {
                            // Send failure acknowledgment
                            String responseTopic = "v1/devices/me/rpc/response/" + requestId;
                            // mqttClient.publish(responseTopic.c_str(), "{\"success\":false, \"message\":\"Invalid
                            // state\"}");
                            queueMQTTMessage(responseTopic.c_str(),
                                             "{\"success\":false, \"message\":\"Device already ON\"}");
                        }
                    } else if (remoteSwitching == "OFF") {
                        if (relayState) {
                            // Consider this as a Remote Switch trigger
                            offTriggerFromWebhook = 1;
                        } else {
                            // Send failure acknowledgment
                            String responseTopic = "v1/devices/me/rpc/response/" + requestId;
                            // mqttClient.publish(responseTopic.c_str(), "{\"success\":false, \"message\":\"Invalid
                            // state\"}");
                            queueMQTTMessage(responseTopic.c_str(),
                                             "{\"success\":false, \"message\":\"Device already OFF\"}");
                        }
                    }
                } else {
                    // Send failure acknowledgment
                    String responseTopic = "v1/devices/me/rpc/response/" + requestId;
                    // mqttClient.publish(responseTopic.c_str(), "{\"success\":false, \"message\":\"Invalid state\"}");
                    queueMQTTMessage(responseTopic.c_str(),
                                     "{\"success\":false, \"message\":\"Invalid remote switching or button number\"}");
                }
            }
        } else if ((method == "notifyRemotes") && (masterSlaveMode == 2)) {
            String state = doc["params"];
            bool stateBool = doc["params"];
            if (state == "true" || stateBool) {
                if ((doc["params"]["targetStatus"].is<const char *>())) {
                    // This part - iot.EVIQtech sends duration along with the turnOn command
                    String targetStatus = doc["params"]["targetStatus"].as<const char *>();

                    if (targetStatus == "ON") {
                        if (!masterSwitchOnStatusMqtt) {
                            masterSwitchOnStatusMqtt = true;
                            setAllIndicatorsOff();
                        }
                        masterSwitchOnTimeMqtt = millis();
                    } else if (targetStatus == "OFF") {
                        masterSwitchOnStatusMqtt = false;
                    }
                } else {
                    // Send failure acknowledgment
                    String responseTopic = "v1/devices/me/rpc/response/" + requestId;
                    // mqttClient.publish(responseTopic.c_str(), "{\"success\":false, \"message\":\"Invalid state\"}");
                    queueMQTTMessage(responseTopic.c_str(),
                                     "{\"success\":false, \"message\":\"Invalid notifyRemotes message\"}");
                }
            }
        } else if (method == "startOTA") {
            handleStartOTARpc(requestId);
        } else if (method == "resendRecentLogs") {
            String responseTopic = "v1/devices/me/rpc/response/" + requestId;
            if (isReplayPending()) {
                queueMQTTMessage(responseTopic.c_str(), "{\"success\":false,\"message\":\"Busy, try later\"}");
            } else {
                time_t from = 0, to = 0;
                if (doc["params"]["from"].is<int64_t>()) {
                    from = (time_t)doc["params"]["from"].as<int64_t>();
                } else if (doc["params"]["from"].is<long>() || doc["params"]["from"].is<int>()) {
                    from = (time_t)doc["params"]["from"].as<long>();
                }
                if (doc["params"]["to"].is<int64_t>()) {
                    to = (time_t)doc["params"]["to"].as<int64_t>();
                } else if (doc["params"]["to"].is<long>() || doc["params"]["to"].is<int>()) {
                    to = (time_t)doc["params"]["to"].as<long>();
                }
                String t = doc["params"]["type"] | "operation";
                LogType logType = LOG_TYPE_OPERATION;
                if (t == "debug")
                    logType = LOG_TYPE_DEBUG;
                else if (t == "energy")
                    logType = LOG_TYPE_ENERGY;
                uint16_t n = queueReplayRequest(logType, from, to, 100);
                char resp[80];
                snprintf(resp, sizeof(resp), "{\"success\":true,\"queued\":%u}", (unsigned)n);
                queueMQTTMessage(responseTopic.c_str(), resp);
            }
        } else if (method == "fetchCoreDump") {
            String responseTopic = "v1/devices/me/rpc/response/" + requestId;
            if (!hasCoreDumpFile()) {
                queueMQTTMessage(responseTopic.c_str(), "{\"success\":false,\"message\":\"No dump available\"}");
            } else if (isCoreDumpReplayPending()) {
                queueMQTTMessage(responseTopic.c_str(), "{\"success\":false,\"message\":\"Already streaming\"}");
            } else if (queueCoreDumpReplay()) {
                char resp[96];
                snprintf(resp, sizeof(resp), "{\"success\":true,\"size\":%u}", (unsigned)coreDumpFileSize());
                queueMQTTMessage(responseTopic.c_str(), resp);
            } else {
                queueMQTTMessage(responseTopic.c_str(), "{\"success\":false,\"message\":\"Failed to queue\"}");
            }
        } else if (method == "deleteCoreDump") {
            String responseTopic = "v1/devices/me/rpc/response/" + requestId;
            if (isCoreDumpReplayPending()) {
                queueMQTTMessage(responseTopic.c_str(), "{\"success\":false,\"message\":\"Streaming in progress\"}");
            } else {
                bool removed = LittleFS.remove("/logs/gen2-dump.bin");
                publishDumpAvailableAttribute();
                queueMQTTMessage(responseTopic.c_str(),
                                 removed ? "{\"success\":true}" : "{\"success\":false,\"message\":\"No dump\"}");
            }
        }
        // // ============================================================
        // // TEMPORARY: REMOVE BEFORE RELEASE
        // // Fires a deliberate ESP_RST_PANIC so the core-dump capture +
        // // upload pipeline can be verified end-to-end. Search for
        // // "TEMPORARY: REMOVE BEFORE RELEASE" to find and delete this block.
        // // Trigger from TB dashboard:
        // //   RPC method: "triggerPanic"
        // //   params: {"mode":"null"}   // null-pointer store (StoreProhibited)
        // //   params: {"mode":"abort"}  // abort() — clearest reason in dump
        // //   params: {"mode":"div0"}   // integer division by zero
        // // ============================================================
        // else if (method == "triggerPanic") {
        //     String responseTopic = "v1/devices/me/rpc/response/" + requestId;
        //     String mode = doc["params"]["mode"] | "abort";
        //     queueMQTTMessage(responseTopic.c_str(),
        //                      "{\"success\":true,\"message\":\"panic incoming\"}");
        //     // Give the response a moment to actually leave the queue.
        //     vTaskDelay(pdMS_TO_TICKS(500));
        //     Serial.printf("[PANIC-TEST] triggering panic via mode=%s\n", mode.c_str());
        //     Serial.flush();
        //     if (mode == "null") {
        //         volatile int *p = (int *)0x00000000;
        //         *p = 0xDEADBEEF;
        //     } else if (mode == "div0") {
        //         volatile int a = 1;
        //         volatile int b = 0;
        //         volatile int c = a / b;
        //         (void)c;
        //     } else {
        //         abort();
        //     }
        // }
        // // ============================================================
        // // END TEMPORARY
        // // ============================================================
    } else if (String(topic).startsWith("v1/devices/me/attributes/response/")) {
        // This part is to support thingsboard cloud service
        String rpcRequestId = String(topic).substring(34, String(topic).length() - 1);

        if (millis() - onTriggerFromMQTTStart < 2000) {
            if (doc["shared"]["durationMinutes"].is<int>()) {
                runMinutes = round(doc["shared"]["durationMinutes"].as<float>());
                durationMinutesMillis = millis();

            } else if (doc["shared"]["durationHours"].is<int>()) {
                runHours = round(doc["shared"]["durationHours"].as<float>());
                durationHoursMillis = millis();
            }
            if (durationHoursMillis > onTriggerFromMQTTStart && durationMinutesMillis > onTriggerFromMQTTStart) {
                if (runHours * 60 + runMinutes < MAX_RUNTIME_HOURS_WEB * 60) {
                    TimeSpan duration(runHours * 3600 + runMinutes * 60);
                    onTriggerFromMQTT = 1;
                    onTriggerDurationFromMQTT = duration.totalseconds();

                } else {
                    // Send failure acknowledgment
                    String responseTopic = "v1/devices/me/rpc/response/" + rpcRequestId;
                    // mqttClient.publish(responseTopic.c_str(), "{\"success\":false, \"message\":\"Invalid
                    // Duration\"}");
                    queueMQTTMessage(responseTopic.c_str(), "{\"success\":false, \"message\":\"Invalid Duration\"}");
                }
            }
        }

        // Check for OTA firmware shared attributes in attribute response
        if (doc.containsKey("shared")
            && (doc["shared"].containsKey("fw_title") || doc["shared"].containsKey("fw_version"))) {
            processOTASharedAttributes(doc);
        }
    } else if (String(topic).startsWith("homeassistant/")) {
        Serial.println("Inside Homeassistant callback");
        // Split the topic with "/"
        String topicStr = String(topic);
        String parts[6];
        int partIndex = 0;
        int lastIndex = 0;

        for (int i = 0; i < topicStr.length(); i++) {
            if (topicStr[i] == '/') {
                parts[partIndex++] = topicStr.substring(lastIndex, i);
                lastIndex = i + 1;
            }
        }
        parts[partIndex++] = topicStr.substring(lastIndex);
        // Serial.println(parts[1]);
        // Serial.println(parts[2]);
        // Serial.println(parts[3]);
        Serial.println(getSwitchName());

        // Check if the third part matches the switch name
        if (parts[1] == "switch" && parts[2] == getUniqueSwitchNameForHa() && parts[3] == "set") {
            // Serial.println("Inside Homeassistant switchname");

            String relayStatusTemp = incoming;
            String method = doc["method"];
            if (method == "turnOn") {
                if (!relayState) {
                    uint8_t runMinutes = doc["durationMinutes"].as<uint8_t>();
                    uint8_t runHours = doc["durationHours"].as<uint8_t>();
                    Serial.println(runMinutes);

                    if (runHours * 60 + runMinutes < MAX_RUNTIME_HOURS_WEB * 60 && (runHours > 0 || runMinutes > 0)) {
                        TimeSpan duration(runHours * 3600 + runMinutes * 60);
                        onTriggerFromMQTT = 1;
                        onTriggerDurationFromMQTT = duration.totalseconds();
                    } else {
                        // Send failure acknowledgment
                        Serial.println("Invalid duration");
                    }
                } else {
                    // Send failure acknowledgment
                    // Serial.println("Relay already ON");
                }
            } else if (method == "turnOff") {
                // Serial.println("TurnOff");

                if (relayState) {
                    offTriggerFromMQTT = 1;
                } else {
                    // Send failure acknowledgment
                    // Serial.println("Relay already OFF");
                }
            }
        }

    } else {
        // Support Quarqify
        uint8_t relStatusFromMQTT = doc["relayStatus"];
        Serial.println("Relay status from MQTT: " + String(relStatusFromMQTT));
        if (relStatusFromMQTT != relayState) {
            if (relStatusFromMQTT) {
                // onTriggerFromMQTT=1;
                uint8_t runMinutes = doc["durationMinutes"].as<uint8_t>();
                uint8_t runHours = doc["durationHours"].as<uint8_t>();
                if (runHours * 60 + runMinutes < MAX_RUNTIME_HOURS_WEB * 60) {
                    TimeSpan duration(runHours * 3600 + runMinutes * 60);
                    onTriggerFromMQTT = 1;
                    onTriggerDurationFromMQTT = duration.totalseconds();
                } else {
                    // Send failure acknowledgment
                    Serial.println("Invalid duration");
                }
            } else {
                offTriggerFromMQTT = 1;
            }
        }
    }
}
void loadMQTTSettings(JsonDocument &doc) {
    doc["bkr"] = mqttBroker;
    doc["port"] = mqttPort;
    // doc["user"] = getMQTTUsername();
    // doc["pass"] = getMQTTPassword();
    doc["pTopic"] = mqttPublishTopic;
    doc["sTopic"] = mqttSubscribeTopic;
    doc["type"] = mqttIntegrationType;
    doc["conStatus"] = mqttClient.connected();
}

String sensorNameForHa(String str) {
    str.toLowerCase();
    str.replace(" ", "_");
    str.replace("-", "_");
    return str;
}
String getHaStateTopic(String hAEntityType) {
    String switchName = getSwitchNameForHa();
    String uniqueName = getUniqueSwitchNameForHa();
    String topic;
    if (hAEntityType == "sensor") {
        topic = "homeassistant/sensor/" + uniqueName + "/state";
    } else if (hAEntityType == "status") {
        topic = "homeassistant/status/" + uniqueName;
    } else if (hAEntityType == "devicestatus") {
        topic = "homeassistant/sensor/" + uniqueName + "/status";
    } else if (hAEntityType == "binary_sensor") {
        topic = "homeassistant/binary_sensor/" + uniqueName + "/state";
    }

    return topic;
}
String getHaSwitchTopic(String hAEntityType, String parameter) {
    String uniqueName = getUniqueSwitchNameForHa();
    String topic;

    if (hAEntityType == "switch") {
        topic = "homeassistant/switch/" + uniqueName + "/" + parameter + "/state";
    }

    return topic;
}
String getHaCommandTopic(String hAEntityType, String parameter) {
    String uniqueName = getUniqueSwitchNameForHa();
    String topic;
    if (hAEntityType == "switch") {
        topic = "homeassistant/switch/" + uniqueName + "/set";
    } else if (hAEntityType == "binary_sensor") {
        topic = "homeassistant/binary_sensor/" + uniqueName + "/set";
    } else if (hAEntityType == "number") {
        topic = "homeassistant/number/" + uniqueName + "/" + parameter + "/set";
    }
    return topic;
}

String getHaSensorConfig(String parameter, String unit, String hAEntityType) {
    String switchName = getSwitchNameForHa();
    String uniqueName = getUniqueSwitchNameForHa();
    JsonDocument doc;

    if (parameter == "relayStatus") {
        doc["state_topic"] = getHaSwitchTopic("switch", parameter);
        // doc["state_topic"] = getHaStateTopic("sensor");
        doc["name"] = parameter;
    } else if (hAEntityType == "number") {
        // Optimistic mode: device never echoes durationHours/Minutes back on the
        // sensor telemetry, so don't set a state_topic — HA will remember the
        // value the user typed and `states(...)` in the switch command_template
        // will read it back correctly.
        doc["name"] = parameter;
    } else {
        doc["state_topic"] = getHaStateTopic("sensor");
        doc["name"] = parameter;
    }
    doc["availability_topic"] = getHaStateTopic("status");
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";

    if (unit != "") {
        doc["unit_of_measurement"] = unit;
    }
    if (parameter == "relayStatus") {
        doc["icon"] = "mdi:power";
    } else if (parameter == "current") {
        doc["icon"] = "mdi:current-ac";
    } else if (parameter == "voltage") {
        doc["icon"] = "mdi:sine-wave";
    } else if (parameter == "power") {
        doc["icon"] = "mdi:flash";
    } else if (parameter == "energy") {
        doc["icon"] = "mdi:eye";
    } else if (parameter == "cumEnergy") {
        doc["icon"] = "mdi:eye";
    }
    if (parameter == "relayStatus") {
        //   doc["value_template"] = "{{ 'ON' if value_json.relayStatusAux else 'OFF' }}";
    } else if (hAEntityType == "number") {
        // No value_template for number entities — they're optimistic, no state_topic to parse.
    } else {
        doc["value_template"] = "{{ value_json." + parameter + " }}";
    }
    if (parameter != "relayStatus" && hAEntityType != "number") {
        doc["state_class"] = "measurement";
    }
    if (parameter == "remainingDuration") {
        doc["device_class"] = "duration";
    }
    doc["unique_id"] = uniqueName + "_" + parameter;
    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(uniqueName);
    device["name"] = switchName;
    device["manufacturer"] = "EVIQtech";
    device["model"] = "SwiGO Supersmart";

    if (hAEntityType == "switch") {
        doc["command_topic"] = getHaCommandTopic(hAEntityType, "");
        // doc["value_template"] = "{{ '{\"method\":\"turnOn\", \"durationHours\":0, "
        //                         "\"durationMinutes\":5 }' if value_json.relayStatus else "
        //                         "'{\"method\":\"turnOff\"}' }}";
        if (parameter == "relayStatus") {
            // doc["payload_on"] = "{\"method\":\"turnOn\", \"durationHours\":0, \"durationMinutes\":5 }";
            // doc["payload_on"]= "{\"method\":\"turnOn\", \"durationHours\":{{ states('input_number."+uniqueName +
            // "_durationHours" + "') | int }}, \"durationMinutes\":{{ states('input_number."+uniqueName +
            // "_durationMinutes" + "') | int }}}"; doc["payload_off"] = "{\"method\":\"turnOff\"}";
            String entity_hours = "number." + uniqueName + "_durationhours";
            String entity_minutes = "number." + uniqueName + "_durationminutes";
            doc["command_template"] = "{% if value == 'ON' %}{\"method\":\"turnOn\", \"durationHours\":{{ states('"
                                      + entity_hours + "') | int(0) }}, \"durationMinutes\":{{ states('"
                                      + entity_minutes + "') | int(0) }}}{% else %}{\"method\":\"turnOff\"}{% endif %}";
        }
        doc["state_on"] = "ON";
        doc["state_off"] = "OFF";
    } else if (hAEntityType == "number") {
        doc["command_topic"] = getHaCommandTopic(hAEntityType, parameter);
    }

    if (parameter == "durationHours") {
        doc["name"] = "Duration Hours";
        doc["min"] = 0;
        doc["max"] = 23;
        doc["step"] = 1;
        doc["mode"] = "box";
        doc["unit_of_measurement"] = "hours";
        doc["object_id"] = uniqueName + "_" + parameter;
        doc["initial"] = 0;
    } else if (parameter == "durationMinutes") {
        doc["name"] = "Duration Minutes";
        doc["min"] = 0;
        doc["max"] = 59;
        doc["step"] = 1;
        doc["mode"] = "box";
        doc["unit_of_measurement"] = "minutes";
        doc["object_id"] = uniqueName + "_" + parameter;
        doc["initial"] = 15;
    }
    String config;
    serializeJson(doc, config);
    return config;
}
String getHaDiscoveryStateMessage() {
    // {
    //     "name": "RGBled Status",
    //     "state_topic": "homeassistant/status/RGBled",
    //     "payload_on": "online",
    //     "payload_off": "offline",
    //     "device_class": "connectivity",
    //     "unique_id": "RGBled_status_93E8",
    //     "device": {
    //       "identifiers": ["RGBled_93E8"],
    //       "name": "RGBled",
    //       "manufacturer": "EVIQtech",
    //       "model": "SwiGO Supersmart"
    //     }
    //   }
    String switchName = getSwitchNameForHa();
    String macAddress = WiFi.macAddress();
    String macLast4 = macAddress.substring(macAddress.length() - 5);
    macLast4.replace(":", "");
    JsonDocument doc;
    doc["name"] = switchName + " State";
    doc["state_topic"] = "homeassistant/status/" + switchName;
    doc["payload_on"] = "online";
    doc["payload_off"] = "offline";
    doc["device_class"] = "connectivity";
    doc["icon"] = "mdi:check-network-outline";

    doc["unique_id"] = switchName + "_status_" + macLast4;
    JsonObject device = doc["device"].to<JsonObject>(); // doc[key].to<JsonObject>()
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(switchName + "_" + macLast4);
    device["name"] = switchName;
    device["manufacturer"] = "EVIQtech";
    device["model"] = "SwiGO Supersmart";

    String config;
    serializeJson(doc, config);
    return config;
}
void sendHADiscoveryMessages() {
    Serial.println("Sending Home Assistant Discovery messages");
    if (isGeneralMQTTEnabled) {
        String discoveryTopic;
        String discoveryPayload;
        mqttClient.setBufferSize(1024);
        String uniqueName = getUniqueSwitchNameForHa();

        discoveryTopic = "homeassistant/sensor/" + uniqueName + "/current/config";
        discoveryPayload = getHaSensorConfig("current", "A", "sensor");
        mqttClient.publish(discoveryTopic.c_str(), discoveryPayload.c_str(), true);

        discoveryTopic = "homeassistant/sensor/" + uniqueName + "/voltage/config";
        discoveryPayload = getHaSensorConfig("voltage", "V", "sensor");
        mqttClient.publish(discoveryTopic.c_str(), discoveryPayload.c_str(), true);

        discoveryTopic = "homeassistant/sensor/" + uniqueName + "/power/config";
        discoveryPayload = getHaSensorConfig("power", "Watts", "sensor");
        mqttClient.publish(discoveryTopic.c_str(), discoveryPayload.c_str(), true);

        delay(100);
        discoveryTopic = "homeassistant/sensor/" + uniqueName + "/energy/config";
        discoveryPayload = getHaSensorConfig("energy", "kWh", "sensor");
        mqttClient.publish(discoveryTopic.c_str(), discoveryPayload.c_str(), true);

        discoveryTopic = "homeassistant/sensor/" + uniqueName + "/cumEnergy/config";
        discoveryPayload = getHaSensorConfig("cumEnergy", "kWh", "sensor");
        mqttClient.publish(discoveryTopic.c_str(), discoveryPayload.c_str(), true);

        // Ensure buffer is large enough for switch configurations

        discoveryTopic = "homeassistant/switch/" + uniqueName + "/relayStatus/config";
        discoveryPayload = getHaSensorConfig("relayStatus", "", "switch");
        mqttClient.publish(discoveryTopic.c_str(), discoveryPayload.c_str(), true);
        Serial.println("Main Relay Discovery Topic: " + discoveryTopic);
        Serial.println("Main Relay Discovery Payload: " + discoveryPayload);

        delay(100); // Add delay between switch discoveries

        discoveryTopic = "homeassistant/sensor/" + uniqueName + "/remainingDuration/config";
        discoveryPayload = getHaSensorConfig("remainingDuration", "s", "sensor");
        mqttClient.publish(discoveryTopic.c_str(), discoveryPayload.c_str(), true);
        Serial.println(discoveryTopic);
        Serial.println(discoveryPayload);

        discoveryTopic = "homeassistant/number/" + uniqueName + "/durationHours/config";
        discoveryPayload = getHaSensorConfig("durationHours", "", "number");
        mqttClient.publish(discoveryTopic.c_str(), discoveryPayload.c_str(), true);
        Serial.println(discoveryTopic);
        Serial.println(discoveryPayload);

        discoveryTopic = "homeassistant/number/" + uniqueName + "/durationMinutes/config";
        discoveryPayload = getHaSensorConfig("durationMinutes", "", "number");
        mqttClient.publish(discoveryTopic.c_str(), discoveryPayload.c_str(), true);
        Serial.println(discoveryTopic);
        Serial.println(discoveryPayload);
    }
}