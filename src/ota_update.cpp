#include "ota_update.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

#include "cert_management.h"
#include "globals.h"
#include "logdebug.h"
#include "mqtt.h"
#include "preference_management.h"

// Globals
FirmwareInfo targetFirmware;
volatile OTAState otaState = OTA_IDLE;
volatile uint8_t otaProgress = 0;
char otaErrorMessage[128] = {0};

static uint32_t otaRequestId = 9000;  // Unique ID range for OTA attribute requests

// Helper: get OTA state as string
static const char *otaStateStr(OTAState state) {
    switch (state) {
        case OTA_DOWNLOADING: return "DOWNLOADING";
        case OTA_UPDATING:    return "UPDATING";
        case OTA_UPDATED:     return "UPDATED";
        case OTA_FAILED:      return "FAILED";
        default:              return "IDLE";
    }
}

// Helper: URL-encode a string
static String urlEncode(const String &str) {
    String encoded = "";
    for (unsigned int i = 0; i < str.length(); i++) {
        char c = str.charAt(i);
        if (isAlphaNumeric(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
            encoded += buf;
        }
    }
    return encoded;
}

void initOTA() {
    otaState = OTA_IDLE;
    otaProgress = 0;
    memset(&targetFirmware, 0, sizeof(FirmwareInfo));
    targetFirmware.available = false;
    otaErrorMessage[0] = '\0';
    Serial.println("[OTA] Module initialized");
}

void confirmFirmwareValid() {
    // Mark the current firmware as valid so the bootloader won't roll back
    esp_ota_mark_app_valid_cancel_rollback();
    Serial.println("[OTA] Firmware marked as valid");
}

bool isOTAUpdateAvailable() {
    Serial.println("inside isOTAUpdateAvailable");
    Serial.println(targetFirmware.version[0]);
    if (targetFirmware.version[0] == '\0') return false;

    // Skip any non-digit prefix (e.g. "v.", "v", "V.")
    const char *vStr = targetFirmware.version;
    while (*vStr && !isdigit(*vStr)) vStr++;

    int tMajor = 0, tMinor = 0, tPatch = 0;
    sscanf(vStr, "%d.%d.%d", &tMajor, &tMinor, &tPatch);
    Serial.println(tMajor);
    Serial.println(tMinor);
    Serial.println(tPatch);
    if (tMajor > FIRMWARE_MAJOR) return true;
    if (tMajor == FIRMWARE_MAJOR && tMinor > FIRMWARE_MINOR) return true;
    if (tMajor == FIRMWARE_MAJOR && tMinor == FIRMWARE_MINOR && tPatch > FIRMWARE_PATCH) return true;

    return false;
}

void publishOTAAttributes() {
    if (!isCloudConnected || mqttQueue == NULL) return;

    JsonDocument jsonDoc;
    char jsonBuffer[256];

    jsonDoc["fw_update_available"] = targetFirmware.available;
    if (targetFirmware.available) {
        jsonDoc["target_fw_version"] = targetFirmware.version;
    }
    jsonDoc["fw_state"] = otaStateStr(otaState);
    jsonDoc["fw_progress"] = (int)otaProgress;

    size_t len = serializeJson(jsonDoc, jsonBuffer);
    if (len > 0 && len < MAX_PAYLOAD_LENGTH) {
        queueMQTTMessage(attribListenTopic, jsonBuffer);
        // Also publish fw_state as telemetry so widgets subscribed to telemetry pick it up
        queueMQTTMessage(propTopic, jsonBuffer);
        Serial.println("[OTA] Published OTA attributes+telemetry: " + String(jsonBuffer));
    }
}

static void publishOTAProgress() {
    if (!isCloudConnected || mqttQueue == NULL) return;

    JsonDocument jsonDoc;
    char jsonBuffer[128];

    jsonDoc["fw_state"] = otaStateStr(otaState);
    jsonDoc["fw_progress"] = (int)otaProgress;

    size_t len = serializeJson(jsonDoc, jsonBuffer);
    if (len > 0) {
        // Publish as both attribute and telemetry so the widget picks it up in real-time
        queueMQTTMessage(attribListenTopic, jsonBuffer);
        queueMQTTMessage(propTopic, jsonBuffer);
    }
}

// Parse fw_* keys from incoming shared attribute update or attribute response
static void parseFirmwareKeys(JsonObject obj) {
    bool hasUpdate = false;

    if (obj["fw_title"].is<const char *>()) {
        strncpy(targetFirmware.title, obj["fw_title"].as<const char *>(), sizeof(targetFirmware.title) - 1);
        targetFirmware.title[sizeof(targetFirmware.title) - 1] = '\0';
        hasUpdate = true;
    }
    if (obj["fw_version"].is<const char *>()) {
        strncpy(targetFirmware.version, obj["fw_version"].as<const char *>(), sizeof(targetFirmware.version) - 1);
        targetFirmware.version[sizeof(targetFirmware.version) - 1] = '\0';
        hasUpdate = true;
    }
    if (obj["fw_size"].is<uint32_t>()) {
        targetFirmware.size = obj["fw_size"].as<uint32_t>();
    }
    if (obj["fw_checksum"].is<const char *>()) {
        strncpy(targetFirmware.checksum, obj["fw_checksum"].as<const char *>(), sizeof(targetFirmware.checksum) - 1);
        targetFirmware.checksum[sizeof(targetFirmware.checksum) - 1] = '\0';
    }
    if (obj["fw_checksum_algorithm"].is<const char *>()) {
        strncpy(targetFirmware.checksumAlg, obj["fw_checksum_algorithm"].as<const char *>(), sizeof(targetFirmware.checksumAlg) - 1);
        targetFirmware.checksumAlg[sizeof(targetFirmware.checksumAlg) - 1] = '\0';
    }

    if (hasUpdate) {
        targetFirmware.available = isOTAUpdateAvailable();
        // Serial.println("[OTA] Firmware info updated - title: " + String(targetFirmware.title) +
        //                " version: " + String(targetFirmware.version) +
        //                " available: " + String(targetFirmware.available));
        publishOTAAttributes();
    }
}

void processOTASharedAttributes(JsonDocument &doc) {
    // Direct keys from attribute push: {"fw_title":"...","fw_version":"...",...}
    if (doc["fw_title"].is<const char *>() || doc["fw_version"].is<const char *>()) {
        JsonObject root = doc.as<JsonObject>();
        parseFirmwareKeys(root);
    }
    // Nested under "shared" from attribute request response: {"shared":{"fw_title":"...",...}}
    if (doc["shared"].is<JsonObject>()) {
        JsonObject shared = doc["shared"].as<JsonObject>();
        if (shared["fw_title"].is<const char *>() || shared["fw_version"].is<const char *>()) {
            parseFirmwareKeys(shared);
        }
    }
}

void requestOTASharedAttributes() {
    if (!isCloudConnected || mqttQueue == NULL) return;

    String requestPayload = "{\"sharedKeys\":\"fw_title,fw_version,fw_size,fw_checksum,fw_checksum_algorithm\"}";
    String topic = String(attribRequestTopic) + String(otaRequestId++);

    if (queueMQTTMessage(topic.c_str(), requestPayload)) {
        Serial.println("[OTA] Requested fw_* shared attributes");
    }
}

void handleStartOTARpc(const String &requestId) {
    String responseTopic = "v1/devices/me/rpc/response/" + requestId;

    // Check if update is available
    if (!targetFirmware.available) {
        queueMQTTMessage(responseTopic.c_str(), "{\"success\":false,\"message\":\"No firmware update available\"}");
        return;
    }

    // Check relay is OFF
    if (relayState != 0) {
        queueMQTTMessage(responseTopic.c_str(), "{\"success\":false,\"message\":\"Device is ON. Please turn off before updating.\"}");
        return;
    }

    // Check WiFi
    if (WiFi.status() != WL_CONNECTED) {
        queueMQTTMessage(responseTopic.c_str(), "{\"success\":false,\"message\":\"WiFi not connected\"}");
        return;
    }

    // Already in progress
    if (otaState == OTA_DOWNLOADING || otaState == OTA_UPDATING) {
        queueMQTTMessage(responseTopic.c_str(), "{\"success\":false,\"message\":\"OTA already in progress\"}");
        return;
    }

    // Acknowledge - update will begin
    queueMQTTMessage(responseTopic.c_str(), "{\"success\":true,\"message\":\"OTA update starting\"}");

    otaState = OTA_DOWNLOADING;
    otaProgress = 0;
    otaErrorMessage[0] = '\0';
    publishOTAAttributes();

    Serial.println("[OTA] Update initiated via RPC, download will start in main loop");
}

bool performOTAUpdate() {
    Serial.println("[OTA] Starting firmware download...");
    Serial.printf("[OTA] Target: %s v%s (%u bytes)\n", targetFirmware.title, targetFirmware.version, targetFirmware.size);

    // Build download URL
    String baseUrl = String(THINGSBOARD_SERVER_URL) + "/api/v1/" + String(token_buffer) + "/firmware";
    baseUrl += "?title=" + urlEncode(String(targetFirmware.title));
    baseUrl += "&version=" + urlEncode(String(targetFirmware.version));

    Serial.println("[OTA] Download URL: " + baseUrl);

    HTTPClient http;
    http.setTimeout(60000);  // 60 second timeout

    if (!http.begin(baseUrl, getRootCACert())) {
        otaState = OTA_FAILED;
        snprintf(otaErrorMessage, sizeof(otaErrorMessage), "Failed to begin HTTP connection");
        Serial.println("[OTA] " + String(otaErrorMessage));
        publishOTAAttributes();
        return false;
    }

    esp_task_wdt_reset();
    int httpCode = http.GET();

    if (httpCode != 200) {
        otaState = OTA_FAILED;
        snprintf(otaErrorMessage, sizeof(otaErrorMessage), "HTTP error: %d", httpCode);
        Serial.println("[OTA] " + String(otaErrorMessage));
        publishOTAAttributes();
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0 && targetFirmware.size > 0) {
        contentLength = targetFirmware.size;
    }

    Serial.printf("[OTA] Content length: %d bytes\n", contentLength);

    // Check partition size (app partitions are ~2.4MB = 0x250000)
    if (contentLength > 0 && contentLength > 0x250000) {
        otaState = OTA_FAILED;
        snprintf(otaErrorMessage, sizeof(otaErrorMessage), "Firmware too large: %d bytes (max ~2.4MB)", contentLength);
        Serial.println("[OTA] " + String(otaErrorMessage));
        publishOTAAttributes();
        http.end();
        return false;
    }

    // Set MD5 checksum if provided
    if (strlen(targetFirmware.checksum) > 0 && strcmp(targetFirmware.checksumAlg, "MD5") == 0) {
        Update.setMD5(targetFirmware.checksum);
        Serial.println("[OTA] MD5 checksum set for validation");
    }

    // Begin update
    size_t updateSize = contentLength > 0 ? (size_t)contentLength : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(updateSize)) {
        otaState = OTA_FAILED;
        Update.printError(Serial);
        snprintf(otaErrorMessage, sizeof(otaErrorMessage), "Update.begin failed");
        Serial.println("[OTA] " + String(otaErrorMessage));
        publishOTAAttributes();
        http.end();
        return false;
    }

    otaState = OTA_UPDATING;
    otaProgress = 0;
    publishOTAAttributes();
    publishOTAProgress();  // Publish initial 0% so widget shows progress bar

    // Stream firmware to Update library
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buffer[1024];
    int totalWritten = 0;
    uint8_t lastReportedProgress = 0;
    unsigned long lastWdtReset = millis();

    while (http.connected() && (contentLength <= 0 || totalWritten < contentLength)) {
        int available = stream->available();
        if (available > 0) {
            int bytesRead = stream->readBytes(buffer, min((int)sizeof(buffer), available));
            if (bytesRead > 0) {
                if (Update.write(buffer, bytesRead) != (size_t)bytesRead) {
                    otaState = OTA_FAILED;
                    Update.printError(Serial);
                    snprintf(otaErrorMessage, sizeof(otaErrorMessage), "Update.write failed at %d bytes", totalWritten);
                    Serial.println("[OTA] " + String(otaErrorMessage));
                    Update.abort();
                    publishOTAAttributes();
                    http.end();
                    return false;
                }
                totalWritten += bytesRead;

                // Update progress
                if (contentLength > 0) {
                    otaProgress = (uint8_t)(totalWritten * 100 / contentLength);
                }

                // Publish progress every ~5%
                if (otaProgress >= lastReportedProgress + 5) {
                    lastReportedProgress = otaProgress;
                    publishOTAProgress();
                    Serial.printf("[OTA] Progress: %d%% (%d/%d bytes)\n", otaProgress, totalWritten, contentLength);
                }
            }
        }

        // Feed watchdog periodically and log free RAM
        if (millis() - lastWdtReset > 2000) {
            esp_task_wdt_reset();
            lastWdtReset = millis();
            Serial.printf("[OTA] Free heap: %u bytes, Min free: %u bytes\n",
                          ESP.getFreeHeap(), ESP.getMinFreeHeap());
        }
        delay(1);  // Yield to other tasks
    }

    http.end();

    // Finalize update
    if (Update.end(true)) {
        otaState = OTA_UPDATED;
        otaProgress = 100;
        publishOTAAttributes();
        publishOTAProgress();

        char logMsg[100];
        snprintf(logMsg, sizeof(logMsg), "Cloud: OTA to v%s OK (%d bytes)", targetFirmware.version, totalWritten);
        logDebugAction(DebugAction::API_UPDATE_FIRMWARE_SUCCESS, logMsg);

        Serial.println("[OTA] Update successful! Restarting...");
        delay(1000);
        ESP.restart();
        return true;  // Won't reach here
    } else {
        otaState = OTA_FAILED;
        Update.printError(Serial);
        snprintf(otaErrorMessage, sizeof(otaErrorMessage), "Update.end failed - checksum mismatch or incomplete");
        Serial.println("[OTA] " + String(otaErrorMessage));
        publishOTAAttributes();

        char logMsg[100];
        snprintf(logMsg, sizeof(logMsg), "Cloud: OTA to v%s FAILED", targetFirmware.version);
        logDebugAction(DebugAction::API_UPDATE_FIRMWARE_FAILED, logMsg);

        return false;
    }
}
