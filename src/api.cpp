#include "api.h"
#include <esp_heap_caps.h>
#include <esp_wifi.h>

#include "web_content.h"
#include "web_mappings.h"
#include "logging_manager.h"
#include "cert_management.h"
#include "ota_update.h"
#include "vsmr_link.h"
// using namespace ArduinoJson;
//  Global variables
bool updateError = false;
// Specific message + status code for upload-time validation failures.
// Sent from the completion handler so it isn't overwritten by the generic
// "Firmware update failed" path.
int updateErrorStatus = 500;
String updateErrorMessage;

// Web-portal firmware upload bookkeeping.
//
// The upload handler can only call Update.end()/abort() when it is told the
// transfer finished. A transfer that dies in between - WiFi blip, phone
// backgrounded, app webview torn down, browser navigated away - never reaches
// that point, so the Update object keeps _size > 0 and every later
// Update.begin() returns false ("already running"). The portal then reports
// "OTA Update Failed to Begin!" for every retry until the device is power
// cycled. serviceFirmwareUpload() below reaps such a transfer so the next
// attempt starts clean.
static volatile bool webUploadActive       = false;
static volatile uint32_t webUploadLastSeen = 0;
static volatile uint32_t webUploadBytes    = 0;
static volatile uint32_t webUploadStartMs  = 0;
// Byte offset of the last progress line, so one is printed per step rather than
// per chunk.
static volatile uint32_t webUploadLoggedAt = 0;
static const uint32_t WEB_UPLOAD_LOG_STEP  = 128 * 1024;
// How often a stalled upload is reported. Reported only - never acted on.
//
// Two measured runs went completely silent mid-transfer (20 s+ at ~900 KB, then
// 120 s+ at ~670 KB) and in both cases the browser resumed sending afterwards,
// so the client was never gone: its TCP was in zero-window/retransmission
// backoff while the device was busy in flash. Every silence threshold tried
// here turned one of those recoverable stalls into a failed upload, because the
// next chunk then hit an Update session this code had already torn down.
// AsyncWebServer's onDisconnect is the only trustworthy "client is gone"
// signal, and the library wires it unconditionally at request construction, so
// no timer is needed as a backstop.
static const uint32_t WEB_UPLOAD_STALL_NOTICE_MS = 15000;
static volatile uint32_t webUploadNoticeAt       = 0;
// AsyncWebServer arms a 3 s RX timeout on every accepted connection
// (WebServer.cpp: c->setRxTimeout(3)) and clears it only once the whole body is
// parsed, so it stays armed for the entire 1.8 MB upload. On expiry AsyncTCP
// just calls _close() - no response, no log - which reaches the browser as a
// bare "Failed to fetch". Being stateless, it behaves identically after every
// power cycle, which is why the original failures persisted through a reset.
//
// It is a race rather than a certainty: _rx_last_packet is refreshed by
// incoming ACKs and window probes as well as by data, so a stalled upload
// survives whenever the peer emits anything inside each window and dies when it
// goes fully quiet - hence the intermittent "Failed to fetch". 30 s sits
// comfortably past the stalls measured here (20 s, then 120 s+ with ACKs
// keeping the timer alive) while still detecting a genuinely dead client
// reasonably promptly, since onDisconnect only runs once the socket closes.
static const uint32_t WEB_UPLOAD_RX_TIMEOUT_S = 30;
// Restart after a successful upload is scheduled here instead of being run from
// the AsyncTCP disconnect callback, so the 200 response is fully flushed to the
// browser before the CPU resets. A restart that races the response is why a
// successful update can still surface as "Failed to fetch" in the portal.
static volatile uint32_t pendingRestartAt = 0;

// Update.begin() reports false for three different reasons and sets _error for
// only one of them, so printError() alone says "No Error". Spell out which one
// it was - that distinction is the whole difference between "retry" and
// "something is wrong with this unit".
static String updateBeginFailureReason() {
    if (Update.isRunning()) {
        // begin() bailed out at its "already running" guard without touching
        // _error, so a stale transfer is still holding the Update object.
        return String("previous upload still open at ") + Update.progress() + "/" + Update.size() + " bytes";
    }
    if (Update.hasError()) {
        // No OTA partition, bad size, bad argument.
        return String(Update.errorString());
    }
    // begin() only has one other exit: malloc(4096) for the sector buffer.
    return String("no memory for flash buffer (free heap ") + ESP.getFreeHeap() + "B, largest internal block "
           + heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) + "B)";
}

static void clearUploadErrorState() {
    updateError        = false;
    updateErrorStatus  = 500;
    updateErrorMessage = "";
}

void releaseFirmwareUpload(const char *why);

// True while a web-portal firmware upload is streaming in. Lets other modules
// hold off work that would compete with it for the radio and the AsyncTCP task.
bool isFirmwareUploadActive() {
    return webUploadActive;
}

// Called from loop(). Cleans up after an upload whose connection vanished and
// performs the deferred post-update restart.
void serviceFirmwareUpload() {
    if (pendingRestartAt != 0 && (uint32_t)millis() >= pendingRestartAt) {
        pendingRestartAt = 0;
        Serial.println(F("Restarting after firmware update"));
        ESP.restart();
    }

    if (!webUploadActive) return;

    // Observe, don't intervene. A stall here is the client's TCP backing off,
    // not a dead connection, and it recovers on its own.
    uint32_t silentMs = (uint32_t)(millis() - webUploadLastSeen);
    if (silentMs >= WEB_UPLOAD_STALL_NOTICE_MS
        && (uint32_t)(millis() - webUploadNoticeAt) >= WEB_UPLOAD_STALL_NOTICE_MS) {
        webUploadNoticeAt = millis();
        Serial.printf("Upload idle %lu s at %u bytes - still waiting | heap %u B, largest %u B\n",
                      (unsigned long)(silentMs / 1000), (unsigned)webUploadBytes, ESP.getFreeHeap(),
                      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    }
}

// Called from the connection-disconnect callback - the one signal that actually
// means the client is gone.
void releaseFirmwareUpload(const char *why) {
    if (!webUploadActive) return;
    webUploadActive = false;
    Serial.printf("Firmware upload %s after %u bytes - releasing OTA\n", why, (unsigned)webUploadBytes);
    if (Update.isRunning()) {
        Update.abort();
    }
    Update.clearError();
    clearUploadErrorState();

    char dbgPayload[DEBUG_MESSAGE_SIZE];
    snprintf(dbgPayload, sizeof(dbgPayload), "Upload %s after %u bytes", why, (unsigned)webUploadBytes);
    logDebugAction(DebugAction::API_UPDATE_FIRMWARE_FAILED, dbgPayload);
}

bool verifyCookie(AsyncWebServerRequest *request) {
    String cookieValue;
    if (request->hasHeader("Cookie")) { // Check if the Cookie header exists
        cookieValue = request->getHeader("Cookie")->value();
        // Serial.println("verifyCookie: Cookie=" + cookieValue);

        int idx = cookieValue.indexOf("SESSIONID=");
        if (idx != -1) {
            String token = cookieValue.substring(idx + 10);
            // Trim at semicolon if multiple cookies
            int semiIdx = token.indexOf(';');
            if (semiIdx != -1) {
                token = token.substring(0, semiIdx);
            }
            token.trim();
            Session *session = findSessionByToken(token);

            // if (session == nullptr) {
            //     // Serial.println("verifyCookie: session not found for token=" + token);
            // } else if (!session->active) {
            //     // Serial.println("verifyCookie: session found but NOT active");
            // }

            if (session != nullptr && session->active) {
                unsigned long currentTime = millis();
                unsigned long sessionTime = session->lastActivityTime;

                // Handle millis() overflow or deep sleep restart (millis resets to 0)
                if (currentTime < sessionTime) {
                    // millis() has overflowed or device restarted
                    session->lastActivityTime = currentTime;
                    // Serial.println("verifyCookie: OK (millis overflow/restart handled)");
                    return true;
                }

                if (currentTime - sessionTime < sessionTimeout) {
                    session->lastActivityTime = currentTime; // Update activity time
                    return true;
                }
            }
        }
    }

    return false;
}

// Body-handler `data` is a raw TCP buffer with NO null terminator, and these
// bodies routinely carry a credential: the admin "pin" every admin endpoint
// verifies, the master-device "mPin", the broker "pass", the cloud
// "mqttToken". The debug log is persisted to flash and readable through the
// log API, so the raw body must never reach it - parse, drop the secrets, and
// re-serialize. `originTag` distinguishes portal-originated entries from the
// cloud handlers in mqtt.cpp that log the same DebugAction values.
// If the body is not valid JSON, only the tag is logged: an unparseable body
// cannot be sanitized field-by-field, and it is not worth leaking to keep.
static void logDebugActionSanitized(DebugAction mode, const uint8_t *data, size_t len,
                                    const char *originTag = "Portal: ") {
    static const char *const kSecretKeys[] = {"pin", "mPin", "pass", "mqttToken"};
    String body;
    JsonDocument doc;
    if (deserializeJson(doc, (const char *)data, len) == DeserializationError::Ok) {
        for (const char *key : kSecretKeys) {
            doc.remove(key);
        }
        serializeJson(doc, body);
    } else {
        body = "<unparseable body>";
    }
    String out = String(originTag) + body;
    char msg[DEBUG_MESSAGE_SIZE];
    strncpy(msg, out.c_str(), sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
    logDebugAction(mode, msg);
}

bool verifyPinWithParameter(const char *pin) {
    if (pin == nullptr) {
        return false;
    }
    
    return strcmp(pin, devicePin) == 0;
}

bool verifyPinWithParameter(const String &pin) {
    if (pin.length() == 0) {
        return false;
    }
    
    return strcmp(pin.c_str(), devicePin) == 0;
}

int parseNumber(const char *str, int start, int length) {
    int result = 0;
    for (int i = start; i < start + length; ++i) {
        if (str[i] < '0' || str[i] > '9') {
            return -1; // Invalid character found
        }
        result = result * 10 + (str[i] - '0');
    }
    return result;
}

bool logoutSession(AsyncWebServerRequest *request) {
    String cookieValue;
    if (request->hasHeader("Cookie")) { // Check if the Cookie header exists
        cookieValue = request->getHeader("Cookie")->value();
        // Serial.println("cookieValue while logout");
        // Serial.println(cookieValue);

        int idx = cookieValue.indexOf("SESSIONID=");
        if (idx != -1) {
            String token = cookieValue.substring(idx + 10);
            // Trim at semicolon if multiple cookies
            int semiIdx = token.indexOf(';');
            if (semiIdx != -1) {
                token = token.substring(0, semiIdx);
            }
            token.trim();
            Session *session = findSessionByToken(token);
            if (session != nullptr) {
                session->active = false;
                return true;
            }
        }
    }
    return false;
}
bool verifyPin(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        if (doc["enteredPin"]) {
            const char *pass = doc["enteredPin"];

            if (strcmp(pass, devicePin) != 0) {
                request->send(402, "text/plain", "Wrong device Pin");
                return false;
            }
            return true;
        } else {
            request->send(400, "text/plain", "Pin not found");
            return false;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
    return false;
}
bool verifyAdminPin(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        if (doc["pin"]) {
            const char *pass = doc["pin"];

            if (strcmp(pass, ADMIN_PASSWORD) != 0 &&
                (SPECIAL_PASSWORD != 1 || strcmp(pass, SPECIAL_ADMIN_PASSWORD) != 0)) {
                request->send(401, "text/plain", "Wrong admin Pin");
                return false;
            }
            return true;
        } else {
            request->send(400, "text/plain", "Pin not found");
            return false;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
    return false;
}

void onRequest(AsyncWebServerRequest *request) {
    // dummy callback function for handling params, etc.
}

void onFileUpload(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len,
                  bool final) {
    // dummy callback function signature, not in used in our code
}

void onScheduleUpload(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    // The body handler is invoked once per TCP chunk for large bodies; only a
    // complete single-chunk body is parseable. data is NOT null-terminated, so
    // every access below is bounded by len - never strlen() it.
    if (index != 0 || len != total) {
        request->send(400, "text/plain", "Invalid request format");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);

    if (!error) {
        if (!doc.isNull() && doc.size() > 0) {
            // saveScheduleToEEPROM(scheduleStr.c_str());
            saveScheduleToEEPROM(doc);
            logDebugActionPartSplit(data, len, "Portal");


            request->send(200, "text/plain", "Schedule saved successfully");
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

void onDeviceDetails(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    // TODO - we need to verify the cookie
    // If isFirstTime flag is set do not check cookie otherwise do
    //  if (!verifyCookie(request)) {
    //          request->send(401, "text/plain", "Unauthorized");
    //          return;
    //        }
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    // Serial.println(data);
    // int errorCode = error.code();
    // Serial.println("System details API - deserializeJson errorcode");
    // Serial.println(errorCode);
    if (!error) {
        // Serial.println("System api - Deserialization successful");

        if (doc.size() > 0) {
            const char *name = doc["deviceName"];
            const char *pass = doc["devicePin"];
            // int offsetMins = doc["timezoneOffset"];

            int16_t offset = doc["timezoneOffset"];
            if (offset > 14 * 60 || offset < -12 * 60) {
                // Serial.println(F("Timezone Offset is not valid"));
                request->send(400, "text/plain", "Timezone Offset is not valid");
                return;
            }
            if (!name || strlen(name) > 20 || strlen(name) < 4) {
                // Serial.println(F("Device name should be between 4 and 20 characters
                // long "));
                request->send(400, "text/plain", "Device name should be between 4 and 20 characters long");
                return;
            }
            if (pass && strlen(pass) != 4) {
                // Serial.println(F("Pin should be entered(4 characters minimum) "));
                request->send(400, "text/plain", "Device Pin should be of 4 characters");
                return;
            }
            bool isValidName = true;
            for (int i = 0; i < strlen(name); i++) {
                if (!(isalpha(name[i]) || isdigit(name[i]) || name[i] == '-' || name[i] == '_' || name[i] == '#'
                      || name[i] == '@')) {
                    isValidName = false;
                    break;
                }
            }
            if (!isValidName) {
                // Serial.println(F("Device name should contain alphabets, numbers &
                // characters -/_/#/@"));
                request->send(400, "text/plain",
                              "Invalid device name. Device name should contain "
                              "alphabets, numbers & special characters "
                              "Hiphen(-)/Underscore(_)/Hash(#)/At(@) only_/#/@ only ");
                return;
            }
            
            // Check if device name has changed
            bool nameChanged = (strcmp(switchName, name) != 0);
            
            isNameSet = true;
            isFirstTime = false;
            deviceTZOffset = offset;
            strcpy(switchName, name);
            strcpy(devicePin, pass);
            saveDeviceDetails();

            // Serial.println(F("Device name and pin are  saved successfuly"));
            String str = "First time use - Device Name changed to - " + (String)switchName;
            
            // Only restart if device name has changed
            if (nameChanged) {
                request->onDisconnect([]() {
                    delay(500);
                    esp_sleep_enable_timer_wakeup(1 * 1000000);
                    esp_deep_sleep_start();
                });
            }
            
            request->send(200, "text/plain", "Device Name and Pin are saved successfully");
            logDebugAction(DebugAction::API_SET_DEVICE_DETAILS, (char *)str.c_str());
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

void onFirstUseSetup(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    
    if (!error) {
        if (doc.size() > 0) {
            // Get device details
            const char *name = doc["deviceName"];
            const char *pass = doc["devicePin"];
            int16_t offset = doc["timezoneOffset"];
            
            // Validate device details
            if (offset > 14 * 60 || offset < -12 * 60) {
                request->send(400, "text/plain", "Timezone Offset is not valid");
                return;
            }
            if (name && (strlen(name) > 20 || strlen(name) < 4)) {
                request->send(400, "text/plain", "Device name should be between 4 and 20 characters long");
                return;
            }
            if (pass && strlen(pass) != 4) {
                request->send(400, "text/plain", "Device Pin should be of 4 characters");
                return;
            }
            bool isValidName = true;
            for (int i = 0; i < strlen(name); i++) {
                if (!(isalpha(name[i]) || isdigit(name[i]) || name[i] == '-' || name[i] == '_' || name[i] == '#'
                      || name[i] == '@')) {
                    isValidName = false;
                    break;
                }
            }
            if (!isValidName) {
                request->send(400, "text/plain",
                              "Invalid device name. Device name should contain "
                              "alphabets, numbers & special characters "
                              "Hiphen(-)/Underscore(_)/Hash(#)/At(@) only");
                return;
            }

            // Save device details
            isNameSet = true;
            isFirstTime = false;
            deviceTZOffset = offset;
            strcpy(switchName, name);
            strcpy(devicePin, pass);
            saveDeviceDetails();

            // Handle WiFi details if provided
            if (doc["wifiSSID"].is<String>()) {
                Serial.println("inside Wifi saving");

                const char *ssid = doc["wifiSSID"];
                const char *wifiPass = doc["wifiPassword"];
                Serial.println("wifiSSID");
                Serial.println(ssid);
                // Validate WiFi details
                if (ssid && strlen(ssid) > 0) {
                    if (strlen(ssid) > 20) {
                        request->send(400, "text/plain", "WiFi SSID exceeds 20 characters");
                        return;
                    }
                    if (wifiPass && strlen(wifiPass) > 20) {
                        request->send(400, "text/plain", "WiFi password exceeds 20 characters");
                        return;
                    }
                  

                    
                    // Save WiFi details
                    isWiFiSet = true;
                    strcpy(ssidBuffer, ssid);
                    strcpy(wifiPassBuffer, wifiPass ? wifiPass : "");
                    staticIP = IPAddress(0, 0, 0, 0);
                    gateway = IPAddress(0, 0, 0, 0);
                    subnet = IPAddress(255, 255, 255, 0);
                    primaryDNS = IPAddress(0, 0, 0, 0);
                    secondaryDNS = IPAddress(0, 0, 0, 0);
                    
                    saveWifiDetails();
                    String wifiStr = "WIFI details saved; SSID- " + (String)ssid;
                      Serial.println("wifiname");
                    Serial.println(wifiStr);
                    logDebugAction(DebugAction::API_SET_WIFI_DETAILS, (char *)wifiStr.c_str());
                    isWifiChanged = true;
                }
            }

            // Log and prepare for restart
            String str = "First time use - Device Name changed to - " + (String)switchName;
            logDebugAction(DebugAction::API_SET_DEVICE_DETAILS, (char *)str.c_str());
            
            // Schedule restart after response is sent
            request->onDisconnect([]() {
                delay(500);
                esp_sleep_enable_timer_wakeup(1 * 1000000);
                esp_deep_sleep_start();
            });
            
            request->send(200, "text/plain", "Device and WiFi settings saved successfully");
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

void onDeviceName(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    if (!verifyPin(request, data, len))
        return;

    if (relayState != 0) {
        request->send(400, "text/plain", "Device is ON, Please try when the device is OFF");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);

    if (!error) {
        if (doc.size() > 0) {
            const char *name = doc["deviceName"];

            if (!name || strlen(name) > 20 || strlen(name) < 4) {
                // Serial.println(F("Device name should be between 4 and 20 characters
                // long "));
                request->send(400, "text/plain", "Device name should be between 4 and 20 characters long");
                return;
            }
            bool isValidName = true;
            for (int i = 0; i < strlen(name); i++) {
                if (!(isalpha(name[i]) || isdigit(name[i]) || name[i] == '-' || name[i] == '_' || name[i] == '#'
                      || name[i] == '@')) {
                    isValidName = false;
                    break;
                }
            }
            if (!isValidName) {
                // Serial.println(F("Device name should contain alphabets, numbers &
                // characters -/_/#/@"));
                request->send(400, "text/plain",
                              "Invalid device name. Device name should contain "
                              "alphabets, numbers & special characters "
                              "Hiphen(-)/Underscore(_)/Hash(#)/At(@) only_/#/@ only ");
                return;
            }
            strcpy(switchName, name);
            saveDeviceDetails();

            // Serial.println(F("Device name is saved successfuly"));
            request->onDisconnect([]() {
                delay(500);
                esp_sleep_enable_timer_wakeup(1 * 1000000);
                esp_deep_sleep_start();
            });
            request->send(200, "text/plain", "Device Name is saved successfully");
            String str = "Device Name changed to - " + (String)switchName;
            logDebugAction(DebugAction::API_SET_DEVICE_NAME, (char *)str.c_str());
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

void onDevicePin(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);

    if (!error) {
        // Serial.println("Pin api - Deserialization successful");
        if (doc.size() > 0) {
            const char *pass = doc["newPin"];
            
            if (!verifyPin(request, data, len))
                    return;
            if (pass && strlen(pass) != 4) {
                // Serial.println(F("Pin should be entered(4 characters ) "));
                request->send(400, "text/plain", "Device Pin should be of a 4 digit number");
                return;
            }

            // Serial.println(F("Device pin is being saved now"));
            // memcpy(EEPROM.getDataPtr() + DEVICE_PIN_ADDRESS, pass, 4);
            // EEPROM.commit();
            strcpy(devicePin, pass);
            saveDeviceDetails();

            // Serial.println(F("Device pin  saved successfuly"));
            request->send(200, "text/plain",
                          "Device Pin is saved successfully. Please use the new pin "
                          "to login next time");
            logDebugAction(DebugAction::API_SET_DEVICE_PIN, (char *)"Device pin changed");
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}
void onCloudSyncSettings(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        // Serial.println("cloud sync api - Deserialization successful");
        if (doc.size() > 0) {
            if (!doc["syncEnabled"]) {
                // Serial.println("Missing MQTT details");
                request->send(400, "text/plain", "Missing SYNC/MQTT details");
                logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS,
                               (char *)"Portal: cloud sync rejected - missing syncEnabled");
                return;
            }
            bool syncEnabled = false;
            if (doc["syncEnabled"] == "1") {
                syncEnabled = true;
            }
            if (syncEnabled && (!doc["mqttToken"] || !doc["deviceId"])) {
                // Serial.println("Missing MQTT details");
                request->send(400, "text/plain", "Missing MQTT details");
                logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS,
                               (char *)"Portal: cloud sync rejected - missing mqttToken/deviceId");
                return;
            }

            // Serial.println(syncEnabled);
            updateCloudSyncDetails(syncEnabled, doc["mqttToken"] ? (const char *)doc["mqttToken"] : (const char *)"",
                                   doc["deviceId"] ? (const char *)doc["deviceId"] : (const char *)"");

            // Serial.println(F("MQTT  saved successfuly"));
            if (syncEnabled) {
                request->send(200, "text/plain",
                              "Cloud Sync settings saved successfully, However, device "
                              "restart is required for the changes to take effect");
            } else {
                disconnectMQTT();
                request->send(200, "text/plain", "Cloud Sync disabled.");
            }
            // Say which way it went in the tag - "enabled" and "disabled" are
            // the fact you want when reading back why the device stopped
            // reporting, and the sanitized body still carries the deviceId.
            logDebugActionSanitized(DebugAction::API_CLOUD_SYNC_DETAILS, data, len,
                                    syncEnabled ? "Portal: cloud sync enabled " : "Portal: cloud sync disabled ");
            requestMQTTReconfigure(); // deferred: runs on mqttPublisherTask (see mqtt.h)
            return;
        }
    }
    // Reached on a malformed body or an empty JSON object - both leave cloud
    // sync untouched, which is worth recording next to the accepted changes.
    request->send(400, "text/plain", "Invalid request format");
    logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS,
                   (char *)"Portal: cloud sync rejected - malformed or empty body");
}
void onAPPassSet(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    if (!verifyPin(request, data, len))
            return;
    
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        // Serial.println("AP password api - Deserialization successful");

        if (doc.size() > 0) {
            const char *pass = doc["APPass"];
            // Serial.println(pass);
            if (!pass || strlen(pass) < 8 || strlen(pass) > 10) {
                // Serial.println(F("AP Password length issue "));
                request->send(400, "text/plain", "AP password should be between 8 and 10 characters long");
                return;
            }

            // Serial.println(F("Device password is being saved now"));
            // memcpy(EEPROM.getDataPtr() + AP_PASSWORD_ADDRESS, pass, 10);
            // EEPROM.commit();
            // savePreference("isAPPassSet", true);

            strcpy(apPassBuffer, pass);
            isAPPassSet = true;
            saveApPassDetails();
            // Serial.println(F("Device AP password is saved successfuly"));
            request->send(200, "text/plain", "Device AP password is saved successfuly");
            logDebugAction(DebugAction::API_SET_AP_PASSWORD, (char *)"Device AP password is saved");
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

void processHTTPOutput(int httpResponseCode, String response, AsyncWebServerRequest *request) {
// Check response
    String outputMessage;

    if (httpResponseCode == 200 || httpResponseCode == 201 || httpResponseCode == 202) {
        
        // Parse the response to extract accessKey
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, response);
        if (!error && doc["accessToken"].is<String>()) {
            String accessToken = doc["accessToken"].as<String>();
            // Serial.print("accessToken: "); Serial.println( accessToken);
            updateCloudSyncDetails(true, accessToken, "");
            Serial.println("Cloud sync details updated successfully");
            // Serial.println("Cloud sync status: " + String(isCloudConnected));
            // Serial.println("Cloud sync token: " + String(token_buffer));
            if (httpResponseCode == 200) {
                outputMessage = "Cloud is connected. You will be getting an email, click on the link to "
                                "activate your account.";
            } else if (httpResponseCode == 201){
                outputMessage = "Cloud is connected to your existing account, You will be able to see the new "
                                "device in your dashboard.";
            } else if (httpResponseCode == 202) {
                outputMessage = "Cloud is reconnected to your existing account, Your device will sync data with cloud.";
            }
            logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS, (char *)outputMessage.c_str());
            request->send(200, "text/plain", outputMessage);
            requestMQTTReconfigure(); // deferred: runs on mqttPublisherTask (see mqtt.h)
            return;

        } else {
            Serial.println("Failed to parse response or accessKey not found.");
            request->send(500, "text/plain", "Error communicating with the server, please try again later");
            logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS,
                        (char *)"Failed to parse response or accessKey not found");
            return;
        }
    } else if (httpResponseCode == 420) {
        outputMessage = "Device already registered with another email, Please contact EVIQtech support, with your device MAC address: " + formatMacAddress(ESP.getEfuseMac());
        Serial.println(outputMessage);
        request->send(420, "text/plain", outputMessage);
        logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS,
                    (char *)outputMessage.c_str());
        return;
    } else if (httpResponseCode == 400) {
        outputMessage = "Device is not registered with the server, Please contact EVIQtech support, with your device MAC address: " + formatMacAddress(ESP.getEfuseMac());
        Serial.println(outputMessage);
        request->send(400, "text/plain", outputMessage);
        logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS,
                    (char *)outputMessage.c_str());
        return;

    } else if (httpResponseCode == 409) {
        outputMessage = "Not able to register with this email, Please try with another email, if the problem persists, please contact EVIQtech support, with your device MAC address: " + formatMacAddress(ESP.getEfuseMac());
        Serial.println(outputMessage);
        request->send(409, "text/plain", outputMessage);
        logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS,
                    (char *)outputMessage.c_str());
        return;

    } else {
        outputMessage = "There is a problem communicating with Server, Please try after sometime, if the problem persists, please contact EVIQtech support, with your device MAC address: "  + formatMacAddress(ESP.getEfuseMac());
        // Serial.println(outputMessage);
        // Serial.println("HTTP Response code: " + String(httpResponseCode));
        request->send(500, "text/plain", outputMessage);
        outputMessage = "HTTP Response code: " + String(httpResponseCode) + " and error: " + response ;
        logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS,
                    (char *)outputMessage.c_str());
        return; 
    }
    
}
void sendPostRequest(String url, String uri, String payload, AsyncWebServerRequest *request) {
    String response;
    if (WiFi.status() == WL_CONNECTED || ethernetConnected) {
        HTTPClient http;

        // Parse URL
        bool isHttps = url.startsWith("https://");
        String fullUrl = url + (uri.startsWith("/") ? "" : "/") + uri;
        Serial.println("Full URL: " + fullUrl);

        // Initialize HTTPClient
        HTTPClient hClient;
        // hClient.setTimeout(20000);
        // hClient.addHeader("Content-Type", "application/json");
        // hClient.addHeader("Mac_id", WiFi.macAddress());
        // Initialised: the plain-HTTP branch below leaves this unset when
        // hClient.begin() fails, and the success test would then read an
        // indeterminate value and try to getString() a client that never connected.
        int httpResponseCode = -1;
        if (isHttps) {
           // WiFiClientSecure wClient;
                hClient.setTimeout(20000);
                
                hClient.begin(fullUrl, getRootCACert());
                hClient.addHeader("Content-Type", "application/json");
                hClient.addHeader("Mac_id", formatMacAddress(ESP.getEfuseMac())); // Ensure correct header name
        
                // Serial.println("Free heap: " + String(ESP.getFreeHeap()));
                // Serial.println("Mac address: "+ formatMacAddress(ESP.getEfuseMac()));
                // Serial.println(ESP.getEfuseMac());
                esp_task_wdt_reset();
                httpResponseCode = hClient.POST(payload);

        } else {
            WiFiClient client;
          
            if (hClient.begin(client, fullUrl)) {
                hClient.addHeader("Content-Type", "application/json");
                String macAddress = WiFi.macAddress(); 
                hClient.addHeader("Mac_id", macAddress); 
                httpResponseCode = hClient.POST(payload);
            } else {
                Serial.println("Unable to connect to server");
            }
        }
        if (httpResponseCode > 0) {
            response = hClient.getString();
            // Serial.println("HTTP Response code: " + String(httpResponseCode));
            // Serial.println("Response: " + response);
        } else {
            Serial.println("Error on sending POST: " + String(httpResponseCode));
            if (isHttps && httpResponseCode == -11) {
                Serial.println("Possible certificate mismatch. Check root CA certificate.");
            } else {
                String errorMsg = "Error on sending POST: " + String(httpResponseCode);
                Serial.println(errorMsg.c_str());
            }
        }
        hClient.end();
        processHTTPOutput(httpResponseCode, response, request);
        // Free resources
    
    } else {
        Serial.println("WiFi Disconnected");
    }
}
void onCloudConnect(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;

    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        Serial.println("cloud connect - Deserialization successful");

        if (doc.size() > 0) {
            const char *email = doc["email"];

            if (WiFi.status() == WL_CONNECTED|| ethernetConnected) {
                String payload = "{\"email\":\"" + String(email) + "\", \"deviceName\":\"" + String(switchName) + "\", \"deviceType\":\"SuperSmart\"}";
                sendPostRequest(THINGSBOARD_SERVER_URL, THINGSBOARD_REGISTER_URI, payload, request);
                return;
             
            } else {
                Serial.println("WiFi/Ethernet not connected");
                request->send(500, "text/plain", "Could not be conencted to the server, please check your WiFi connection");
                return;
            }            
        }
    }
    //request->send(400, "text/plain", "Invalid request format");
}
void onSetRootCACert(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (error) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
    }

    if (doc["reset"].is<bool>() && doc["reset"].as<bool>()) {
        if (resetRootCACert()) {
            request->send(200, "text/plain", "Root CA reverted to embedded default");
        } else {
            request->send(500, "text/plain", "Failed to reset root CA");
        }
        return;
    }

    if (!doc["cert"].is<const char *>()) {
        request->send(400, "text/plain", "Missing 'cert' field");
        return;
    }

    String pem = doc["cert"].as<const char *>();
    if (!updateRootCACert(pem)) {
        request->send(400, "text/plain", "Invalid PEM certificate");
        return;
    }

    request->send(200, "text/plain", "Root CA updated");
}

void onPauseCloud(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;

    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        Serial.println("cloud connect - Deserialization successful");

        if (doc.size() > 0) {
            const char *operation = doc["operation"];

            // A body that omits "operation" - or sends it as a number rather
            // than a string - yields nullptr here, which strcmp() would
            // dereference. Fall through to the 400 below instead of crashing.
            if (operation != nullptr && strcmp(operation, "disconnect") == 0) {
                updateCloudSyncDetails(false, "", "");
                disconnectMQTT();
                request->send(200, "text/plain", "Successfully disconnected from the cloud");
                Serial.println("Successfully disconnected from the cloud, sync status: " + String(isCloudConnected));
                logDebugAction(DebugAction::API_CLOUD_SYNC_DETAILS,
                               (char *)"Portal: cloud sync disconnected by user");
                return;
            }
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

// Add IPAddress validation function if it doesn't exist already
bool parseIPAddress(String ipString, IPAddress &ip) {
    return ip.fromString(ipString);
}

void onWiFiDetails(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        if (doc.size() > 0) {
            const char *ssid = doc["ssid"];
            const char *pass = doc["pass"];
            const char *prefIP = doc["prefIP"];
            const char *gwIP = doc["gwIP"];
            const char *sbMask = doc["sbMask"];
            const char *pDNS = doc["pDNS"];
            const char *sDNS = doc["sDNS"];
            
            IPAddress preferredIP(0, 0, 0, 0), gatewayIP(0, 0, 0, 0), subnetMask(255, 255, 255, 0);
            IPAddress primDNS(8, 8, 8, 8), secondDNS(1, 1, 1, 1); // Default DNS values
            
            // Validate IP addresses if provided
            if (prefIP && strlen(prefIP) > 0) {
                // Validate required IP addresses (preferredIP and gatewayIP)
                if (!parseIPAddress(String(prefIP), preferredIP) || !parseIPAddress(String(gwIP), gatewayIP)) {
                    request->send(400, "text/plain", "Invalid IPv4 address format");
                    return;
                }
                // Validate subnet mask if provided, otherwise use default (255.255.255.0)
                if (sbMask && strlen(sbMask) > 0) {
                    if (!parseIPAddress(String(sbMask), subnetMask)) {
                        request->send(400, "text/plain", "Invalid IPv4 address format");
                        return;
                    }
                }
                // Validate DNS if provided, otherwise use defaults (8.8.8.8, 8.8.4.4)
                if (pDNS && strlen(pDNS) > 0) {
                    if (!parseIPAddress(String(pDNS), primDNS) || !parseIPAddress(String(sDNS), secondDNS)) {
                        request->send(400, "text/plain", "Invalid IPv4 address format");
                        return;
                    }
                }
            }

            if (!ssid || !pass || strlen(ssid) > 20 || strlen(pass) > 20) {
                request->send(400, "text/plain", "SSID or password is missing or exceeds 20 characters");
                return;
            }

            if (strcmp(ssid, ssidBuffer) == 0 && strcmp(pass, wifiPassBuffer) == 0 &&
                preferredIP == staticIP && gatewayIP == gateway && subnetMask == subnet && primDNS == primaryDNS && secondDNS == secondaryDNS) {
                request->send(201, "text/plain", "WiFi details already match the existing details");
                return;
            }

            isWiFiSet = true;
            strcpy(ssidBuffer, ssid);
            strcpy(wifiPassBuffer, pass);
            // if (preferredIP != IPAddress()) {
            //     Serial.println("Setting static IP");
            //     Serial.println(preferredIP);
                staticIP = preferredIP;
                gateway = gatewayIP;
                subnet = subnetMask;
                primaryDNS = primDNS;
                secondaryDNS = secondDNS;
            // }

            saveWifiDetails();
            request->send(200, "text/plain", "WiFi details saved successfully");
            String str = "WIFI details saved; SSID- " + (String)ssid;
            logDebugAction(DebugAction::API_SET_WIFI_DETAILS, (char *)str.c_str());
            isWifiChanged = true;
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

// Forgets the stored WiFi credentials and brings the device back up in AP mode,
// the same teardown the Button4 + Off press combination performs.
void onRemoveWiFi(AsyncWebServerRequest *request) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    if (!isWiFiSet) {
        request->send(400, "text/plain", "No WiFi configuration to remove");
        return;
    }
    // Only flag it here: disableWiFi() re-configures the radio and mDNS, which
    // needs more stack than the AsyncTCP task has, and dropping the STA link
    // from inside the handler would kill this response before it is sent.
    removeWiFiRequestedAt = millis();
    if (removeWiFiRequestedAt == 0) removeWiFiRequestedAt = 1;  // 0 means "nothing pending"
    logDebugAction(DebugAction::DISABLE_WIFI, (char *)"WIFI details removed through web portal");
    request->send(200, "text/plain", "WiFi configuration removed");
}

void onLogin(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data, len);

    String cookieValue;
    //Serial.println("inside login 1111");
    if (!error) {
        if (doc["devicePin"].is<const char*>() &&
            doc["deviceTime"].is<const char*>() &&
            doc["timezoneOffset"].is<int>()) {
            //Serial.println("inside login 2222");
            const char *pin = doc["devicePin"];
            String deviceTime = doc["deviceTime"];
            if (pin == nullptr || pin[0] == '\0') {
                request->send(400, "application/json", "{\"success\": false, \"message\": \"Empty PIN\"}");
                // logDebugAction(DebugAction::API_LOGIN, (char *)"Login end : Empty PIN");
                return;
            }
            //Serial.println("inside login 3333");
            if (strcmp(pin, devicePin) == 0) {
                //Serial.println("inside login 4444");
                String token;
                // Serial.println("inside login devicePin match ");
                if (request->hasHeader("Cookie")) { // Check if the Cookie header exists
                    //Serial.println("inside login aaaaaa");
                    const AsyncWebHeader *header = request->getHeader("Cookie");
                    //Serial.println("inside login bbbbb");
                    if (header != nullptr) {
                        //Serial.println("inside login ccccc");
                        cookieValue = header->value();
                        //Serial.println("inside login 55555");
                        // Serial.println("Cookie: " + cookieValue);
                        int idx = cookieValue.indexOf("SESSIONID=");
                        //Serial.println("inside login 66666");
                        if (idx != -1) {
                            //Serial.println("inside login 7777");
                            String token = cookieValue.substring(idx + 10);
                            // Trim at semicolon if multiple cookies
                            int semiIdx = token.indexOf(';');
                            if (semiIdx != -1) {
                                token = token.substring(0, semiIdx);
                            }
                            token.trim();
                            Session *session = findSessionByToken(token);
                            //Serial.println("inside login 88888");
                            if (session != nullptr) {
                                //Serial.println("inside login 9999");
                                session->active = true; // Re-activate (important after deep sleep restart)
                                session->lastActivityTime = millis(); // Update activity time
                                AsyncWebServerResponse *response =
                                    request->beginResponse(200, "application/json", "{\"success\": true}");
                                response->addHeader("Set-Cookie", "SESSIONID=" + token + "; Path=/; HttpOnly");
                                request->send(response);
                                setLocalTimeFromBrowser(deviceTime);
                                //Serial.println("inside login 10101010");
                                return;
                            }
                        }
                    }
                }
                if (startNewSession(token)) {
                    AsyncWebServerResponse *response =
                        request->beginResponse(200, "application/json", "{\"success\": true}");
                    response->addHeader("Set-Cookie", "SESSIONID=" + token + "; Path=/; HttpOnly");
                    request->send(response);
                    setLocalTimeFromBrowser(deviceTime);
                    logDebugAction(DebugAction::API_LOGIN, (char *)"Login end : Success");

                    return;
                } else {
                    request->send(200, "application/json",
                                  "{\"success\": false, \"message\": \"Maximum sessions "
                                  "reached.\"}");
                    logDebugAction(DebugAction::API_LOGIN, (char *)"Login end : Max sessions");
                }
            } else {
                request->send(200, "application/json", "{\"success\": false, \"message\": \"Incorrect PIN\"}");
                // logDebugAction(DebugAction::API_LOGIN, (char *)"Login end : Pin incorrect");
            }
        }
    } else {
        request->send(400, "application/json", "{\"success\": false, \"message\": \"Bad Request\"}");
        logDebugAction(DebugAction::API_LOGIN, (char *)"Login end : Bad request");
    }
}

void setLocalTimeFromBrowser(String isoTime) {
    // bool timeSet= false;
    // if ( WiFi.status() == WL_CONNECTED){
    //   timeSet = setRTCFromNTP();
    //   scheduleChanged=true;
    // }
    // if ( WiFi.status() != WL_CONNECTED || !timeSet){
    uint16_t year;
    uint8_t month, day, hour, minute, second;
    // Serial.println(isoTime);

    // Parse the ISO string (assumes format "YYYY-MM-DDTHH:MM:SSZ")
    if (sscanf(isoTime.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ", &year, &month, &day, &hour, &minute, &second) == 6) {
        sscanf(isoTime.c_str(), "%4d", &year);

        // Set the local time to RTC
        DateTime dt = DateTime((uint16_t)year, month, day, hour, minute, second);
        // int16_t timezoneOffset = readInt16FromEEPROM(TIMEZONE_OFFSET);
        // int16_t timezoneOffset = ;
        // Serial.println(deviceTZOffset);
        int8_t offsetHours = deviceTZOffset / 60;
        int8_t offsetMins = deviceTZOffset % 60;
        // Serial.println(offsetHours);
        // Serial.println(offsetMins);
        dt = dt + TimeSpan(0, offsetHours, offsetMins, 0);
        // rtc.adjust(dt);
         DateTime currentRTC = getDateTimeFromRTC();
        if (xSemaphoreTake(rtcMutex, portMAX_DELAY) == pdTRUE) {
            rtc.adjust(dt);
            xSemaphoreGive(rtcMutex);
        }
        invalidateRtcCache(); // clock moved - takes rtcMutex, so only after the give above
        // Keep espStartTime in the timescale the clock now runs in.
        rebaseEspStartTime(currentRTC, dt);
        // Login time sync (phone time) - rtc.adjust() cleared OSF, so complete the
        // OSF baseline. This is the primary sync path for internet-less devices.
        onRtcTimeValidated();
        scheduleChanged = true;

        // Verify the local time
        // Serial.print("Local time set: ");
        // Serial.println(formatDateTime( getDateTimeFromRTC()));
    } else {
        // Serial.println("Failed to parse ISO time.");
    }
    //}
    //  Serial.print("after time adj -  ");
}
void onSetCurLimit(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    // Serial.println(data);
    // int errorCode = error.code();
    if (!error) {
        // Serial.println("Current Limit API - Deserialization successful");
        if (!doc["drCur"] || !doc["orCur"] || !doc["lVol"] || !doc["hVol"]) {
            // Serial.println("Missing current limit values");
            request->send(400, "text/plain", "Missing current limit values");
            return;
        }
        float drCur, orCur, lVol, hVol, thCur;
        uint8_t drMin, orMin, lVolMin, hVolMin, stDly;
        uint16_t maxRunTimeMins, breakTimeMins;
        drCur = doc["drCur"].as<float>();
        orCur = doc["orCur"].as<float>();
        lVol = doc["lVol"].as<float>();
        hVol = doc["hVol"].as<float>();
        thCur = doc["thCur"].as<float>();
        drMin = doc["drMin"].as<uint8_t>();
        orMin = doc["orMin"].as<uint8_t>();
        lVolMin = doc["lVolMin"].as<uint8_t>();
        hVolMin = doc["hVolMin"].as<uint8_t>();
        stDly = doc["stDly"].as<uint8_t>();
        maxRunTimeMins = doc["mR"].as<uint16_t>();
        breakTimeMins = doc["bT"].as<uint16_t>();

        if (drCur < 0 || orCur < 0 || lVol < 0 || hVol < 0 || thCur < 0) {
            // Serial.println("Invalid current limit values");
            request->send(400, "text/plain", "Invalid current limit values");
        } else if (drCur < thCur) {
            // Serial.println("thCur should be less");
            request->send(400, "text/plain", "Dryrun Threshold limit should be less than Dryrun current");
        } else if (drMin < 0 || orMin < 0 || lVolMin < 0 || hVolMin < 0 || drMin > 240 || orMin > 240 || lVolMin > 240
                   || hVolMin > 240) {
            request->send(400, "text/plain", "Invalid retry time values");
        } else if (stDly < 0 || stDly > 30) {
            request->send(400, "text/plain", "Invalid delay time values");
        } else if (maxRunTimeMins > 1440 || breakTimeMins > 1440) {
            request->send(400, "text/plain", "Invalid max run time or break time values (max 1440 minutes)");
        } else {
            saveCurLimit(doc);
            isPLimitSet = true;
            savePreference((char *)"isPLimitSet", true);
            request->send(200, "text/plain", "Current Limit saved successfully");
            // Bare CSV of the already-parsed values instead of echoing the raw
            // request body: quotes and field names cost ~60 of the 200-byte
            // DEBUG_MESSAGE_SIZE budget and would truncate the tail. "P-Ptl: "
            // marks the origin - applyPowerSettingsFromCloud() in mqtt.cpp logs
            // the same DebugAction with a "P-Cld: " prefix and identical order.
            // The fields follow the Protection Settings screen top to bottom -
            // one dash-separated group per card - so a log line can be read
            // straight against the form:
            //   drCur,isRstDr,drMin,isSlave,thCur - orCur,isRstOl,orMin -
            //   lVol,isRstLv,lVolMin - hVol,isRstHv,hVolMin - isRst,stDly,mR,bT
            // The restart/retry enables and isSlave come from the globals
            // saveCurLimit() has just written, so they log what was applied.
            char dbgPayload[DEBUG_MESSAGE_SIZE];
            snprintf(dbgPayload, sizeof(dbgPayload), "P-Prtl: %g,%u,%u,%u,%g-%g,%u,%u-%g,%u,%u-%g,%u,%u-%u,%u,%u,%u",
                     drCur, (unsigned)(restartOptions >> 1 & 0x01), (unsigned)drMin,
                     (unsigned)isSlaveControlDevice, thCur, orCur, (unsigned)(restartOptions >> 2 & 0x01),
                     (unsigned)orMin, lVol, (unsigned)(restartOptions >> 3 & 0x01), (unsigned)lVolMin, hVol,
                     (unsigned)(restartOptions >> 4 & 0x01), (unsigned)hVolMin, (unsigned)isRestartEnabled,
                     (unsigned)stDly, (unsigned)maxRunTimeMins, (unsigned)breakTimeMins);
            logDebugAction(DebugAction::API_SET_CURRENT_LIMIT, dbgPayload);
            // Mirror change to cloud as client attribute (pSettings) so dashboard reflects it
            publishPowerSettingsToCloud();
        }
    } else {
        // Serial.print(F("Current Limit API  - deserializeJson() failed with code
        // ")); Serial.println(error.code());
        request->send(400, "text/plain", "Invalid request format");
    }
}
void onClearLogs(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    if (!verifyPin(request, data, len))
        return;
    clearLogs();

    logDebugAction(DebugAction::API_CLEAR_LOGS, (char *)"Device operational logs cleared.");
    request->send(200, "text/plain", "Logs cleared successfully");
}

void onResetCumulativeEnergy(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    if (!verifyPin(request, data, len))
        return;
    // char tempStr[100];
    // sprintf(tempStr, "Device cumulative energy is reset. Before reset : %f",
    // cumulativeEnergy); logDebugAction(DebugAction::API_RESET_CUMULATIVE_ENERGY,
    // tempStr);
    char tempStr[100];
    char floatStr[15];
    dtostrf(cumulativeEnergy, 10, 2, floatStr); // Convert float to string
    snprintf(tempStr, sizeof(tempStr), "Device cumulative energy is reset. Before reset: %s", floatStr);
    logDebugAction(DebugAction::API_RESET_CUMULATIVE_ENERGY, tempStr);

    BL0939_setup();
    cumulativeEnergy = 0;
    energyOnStart = 0;
    resetEnergyData();
    request->send(200, "text/plain", "Cumulative energy is reset now.");
}

void onSystemTimeReset(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    // Serial.println(data);
    // int errorCode = error.code();
    // Serial.println("Reset Time API - deserializeJson errorcode");
    // Serial.println(errorCode);
    if (!error) {
        // Serial.println("Reset Time API - Deserialization successful");
        String resetTime;
        if (doc["resetTime"]) {
            const char *resetTime = doc["resetTime"];
            bool rtcSetSuccess = false;
            if ((getWiFiMode() == 'W' && WiFi.status() == WL_CONNECTED) || ethernetConnected) {
                if (setRTCFromNTP()) {
                    rtcSetSuccess = true;
                    String responseString = "Time adjusted successfully to : " + getTimeString();
                    scheduleChanged = true;
                    request->send(200, "text/plain", responseString);
                    return;
                }
            }

            if (getWiFiMode() != 'W' || !rtcSetSuccess) {
                // Parse the time string & set to RTC
                uint16_t year;
                bool valid = true;
                uint8_t month, day, hour, minute, second;
                year = parseNumber(resetTime, 0, 4);
                if (year == -1 || year < 1970 || year > 2100)
                    valid = false;

                month = parseNumber(resetTime, 5, 2);
                if (month == -1 || month < 1 || month > 12)
                    valid = false;

                day = parseNumber(resetTime, 8, 2);
                if (day == -1 || day < 1 || day > 31)
                    valid = false;

                hour = parseNumber(resetTime, 11, 2);
                if (hour == -1 || hour > 23)
                    valid = false;

                minute = parseNumber(resetTime, 14, 2);
                if (minute == -1 || minute > 59)
                    valid = false;

                second = parseNumber(resetTime, 17, 2);
                if (second == -1 || second > 59)
                    valid = false;

                if (!valid) {
                    // Serial.println("Error: Invalid date-time format or values out of
                    // range");
                    request->send(400, "text/plain", "Incorrect date-time format ");
                } else {
                    // rtc.adjust(DateTime(year, month, day, hour, minute, second));
                    DateTime currentRTC = getDateTimeFromRTC();
                    DateTime ntpTime = DateTime(year, month, day, hour, minute, second);
                    if (xSemaphoreTake(rtcMutex, portMAX_DELAY) == pdTRUE) {
                        rtc.adjust(ntpTime);
                        xSemaphoreGive(rtcMutex);
                    }
                    invalidateRtcCache(); // clock moved - takes rtcMutex, so only after the give above
                    // Keep espStartTime in the timescale the clock now runs in.
                    rebaseEspStartTime(currentRTC, ntpTime);
                    // A portal login setting the time is how internet-less devices are
                    // synced; rtc.adjust() above cleared OSF, so complete the OSF baseline.
                    onRtcTimeValidated();
                    request->send(200, "text/plain", "Time adjusted successfully to : " + getTimeString());
                }
                return;
            }
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

void onRtcBatteryReplaced(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    // User asserts the RTC backup cell (Lithium CR1220) has been replaced.
    //
    // Clearing the alarm FLAG alone is not enough: the hardware OSF (oscillator-stop)
    // flag is set during the battery-swap power-off (the old dead cell can't sustain
    // the oscillator), and if left set, the next boot reads lostPower()=true and
    // re-raises the alarm. Setting the time from the browser clock runs rtc.adjust(),
    // which clears OSF - and unlike setRTCFromNTP() it works WITHOUT internet, so
    // local/offline devices are covered too.
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error && doc["deviceTime"].is<const char *>()) {
        String deviceTime = doc["deviceTime"];
        setLocalTimeFromBrowser(deviceTime);
         // sets correct time + clears OSF (offline-safe)
    } 
    else {
        // No browser time supplied: still clear OSF by rewriting the current RTC time,
        // otherwise the next boot would re-raise the alarm.
        DateTime nowDt = getDateTimeFromRTC();
        if (xSemaphoreTake(rtcMutex, portMAX_DELAY) == pdTRUE) {
            rtc.adjust(nowDt);
            xSemaphoreGive(rtcMutex);
        }
        invalidateRtcCache(); // clock moved - takes rtcMutex, so only after the give above
    }

    // rtcLostPowerCount is a LIFETIME diagnostic of oscillator-stop events and is
    // deliberately NOT reset here - the history must survive cell replacements.
    // Current-cell state is carried by rtcBatteryLow.
    rtcBatteryLow = false;
    saveRtcHealth();
    logDebugAction(DebugAction::SYSTEM_INFO, (char *)"RTC backup battery marked as replaced by user");
    request->send(200, "text/plain", "RTC battery status cleared");
}

void onRestartSystem(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    if (relayState != 0) {
        request->send(400, "text/plain", "Device is ON, Please try when the device is OFF");
        return;
    }
    logDebugAction(DebugAction::API_RESTART, (char *)"System restart request");

    DeserializationError error = deserializeJson(doc, (char *)data, len);

    if (!error) {
        if (doc["enteredPin"]) {
            const char *pass = doc["enteredPin"];

            if (strcmp(pass, devicePin) != 0) {
                // Serial.println(F("Wrong device Pin"));
                request->send(403, "text/plain", "Wrong device Pin");
                return;
            }
            request->onDisconnect([]() {
                delay(500);
                esp_sleep_enable_timer_wakeup(1 * 1000000);
                esp_deep_sleep_start();
            });
            request->send(200, "text/plain", "Restarting...");
            return;
            // ESP.restart();
            //  esp_sleep_enable_timer_wakeup(1 * 1000000);
            //  esp_deep_sleep_start();
        } else {
            request->send(400, "text/plain", "Pin not found");
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

void onClearFaultConditions(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    logDebugAction(DebugAction::API_RESET_FAULT, (char *)"Clearing fault conditions");

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        // Serial.println("Reset fault - Deserialization successful");
        if (doc["enteredPin"]) {
            const char *pass = doc["enteredPin"];

            if (strcmp(pass, devicePin) != 0) {
                // Serial.println(F("Wrong device Pin"));
                request->send(403, "text/plain", "Wrong device Pin");
                return;
            }
            resetFaultConditionsRoutine();
            request->send(200, "text/plain", "Fault conditions are cleared");
        } else {
            request->send(400, "text/plain", "Pin not found");
        }
        return;
    } else {
        request->send(400, "text/plain", "Invalid request format");
    }
}

String getMimeType(String path) {
    if (path.endsWith(".html"))
        return "text/html";
    else if (path.endsWith(".css"))
        return "text/css";
    else if (path.endsWith(".js"))
        return "application/javascript";
    else if (path.endsWith(".json"))
        return "application/json";
    else if (path.endsWith(".png"))
        return "image/png";
    else if (path.endsWith(".svg"))
        return "image/svg+xml";
    else if (path.endsWith(".jpg") || path.endsWith(".jpeg"))
        return "image/jpeg";
    else if (path.endsWith(".gif"))
        return "image/gif";
    else if (path.endsWith(".webp"))
        return "image/webp";
    else if (path.endsWith(".avif"))
        return "image/avif";
    else if (path.endsWith(".ico"))
        return "image/x-icon";
    else
        return "text/plain";
}

void handleHTMLRequest(AsyncWebServerRequest *request) {
    // Todo Check verifycookie for all htmls
    String path = request->url();
    // Serial.println("Request URL: " + path);

    AsyncWebServerResponse *response;
    if (strcmp(path.c_str(), "/") == 0 || strcmp(path.c_str(), "/index.html") == 0) {
        if (isFirstTime) {
            path = "/firstuse.html";
        } else if (!verifyCookie(request)) {
            path = "/login.html";
        } else {
            path = "/index.html";
        }
    }
    if (path.startsWith("/")) {
        path = path.substring(1);
    }
    // Heap diagnostics for web serving
    unsigned long _serveStart = millis();
    size_t _heapBefore = ESP.getFreeHeap();

    // Serve pre-gzipped content from PROGMEM (large files)
    if (gzipMap.find(path) != gzipMap.end()) {
        BinaryFile gz = gzipMap[path];
        response = request->beginResponse(200, getMimeType(path), (const uint8_t *)gz.data, (size_t)gz.length);
        response->addHeader("Content-Encoding", "gzip");
        // Only dn.htm carries the live switch name. Every page fetches it
        // separately (see script.js), so stamping runtime state on the pages
        // themselves would only keep them from ever being cached.
        if (path.endsWith(".htm")) {
            response->addHeader("switchName", getSwitchName());
        } else if (path.endsWith(".js") || path.endsWith(".css") || path.endsWith(".svg") || path.endsWith(".json")) {
            // Every one of these is requested with ?<version> appended, so the
            // URL itself changes on update and the browser never has to ask.
            response->addHeader("Cache-Control", "max-age=31536000, public");
            response->addHeader("Expires", "Wed, 14 Jan 2032 05:00:00 GMT");
        }
        request->send(response);
    }
    // Serve small text files from PROGMEM (not gzipped)
    else if (stringMap.find(path) != stringMap.end()) {
        const char *content = stringMap[path];
        response = request->beginResponse(200, getMimeType(path), content);

        if (path.endsWith(".htm")) {
            response->addHeader("switchName", getSwitchName());
        } else if (path.endsWith(".js") || path.endsWith(".css") || path.endsWith(".svg") || path.endsWith(".json")) {
            response->addHeader("Cache-Control", "max-age=31536000, public");
            response->addHeader("Expires", "Wed, 14 Jan 2032 05:00:00 GMT");
        }
        request->send(response);
    }
    // Serve binary content (e.g., images) from PROGMEM
    else if (binaryMap.find(path) != binaryMap.end()) {
        BinaryFile file = binaryMap[path];
        response = request->beginResponse(200, getMimeType(path), (const uint8_t *)file.data, (size_t)file.length);
        response->addHeader("Cache-Control", "max-age=31536000, public");
        response->addHeader("Expires", "Wed, 14 Jan 2032 05:00:00 GMT");
        request->send(response);
    }
    // If the path is not found, return a 404 error
    else {
        request->send(404, "text/html", "<h1>404 Not Found</h1>");
        Serial.printf("[WEB] 404 %s | heap: %u\n", path.c_str(), ESP.getFreeHeap());
    }
}
void onSave3PhaseSettings(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyAdminPin(request, data, len)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        if (doc.size() > 0) {
            // "3p": 0 = not 3-phase, 1 = 3-phase, 2 = 3-phase WITH VSMR link.
            // Both 1 and 2 mean "3-phase device"; 2 additionally enables the
            // ESP-NOW VSMR downlink.
            bool threePhase = (doc["3p"] == "1" || doc["3p"] == "2");
            bool vsmrLinkOn = (doc["3p"] == "2");
            String vsmrMac = doc["vsmrMac"] | "";   // paired VSMR MAC ("" = any)
            uint8_t tapRequired = doc["tapR"].as<uint8_t>();
            uint8_t tapDuration = doc["tapD"].as<uint8_t>();
            uint8_t ledTypeTemp = doc["ledT"].as<uint8_t>();
            String shuntOrCTTemp  = doc["sOrC"].as<String>();
            bool ethernetSet = (doc["ethE"] == "1");
            bool leakDetSet = (doc["leakE"] == "1");
            uint8_t inRush = doc["inRush"].as<uint8_t>();
            if (inRush > 120) {
                request->send(400, "text/plain", "Invalid inrush settle duration (max 120 seconds)");
                return;
            }

            // Save the settings to EEPROM or any other storage
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
            // Re-initialize leak detection pin when config changes at runtime
            if (leakDetectionEnabled) {
                pinMode(LEAK_DETECTION, INPUT_PULLDOWN);
                attachInterrupt(digitalPinToInterrupt(LEAK_DETECTION), handleLeakagePinSpike, RISING);
            } else {
                detachInterrupt(digitalPinToInterrupt(LEAK_DETECTION));
            }
            request->send(200, "text/plain", "3-phase settings saved successfully");

            doc.remove("pin");
            String sanitizedData;
            serializeJson(doc, sanitizedData);
            // Prefix marks the origin: applyAdminSettingsFromCloud() in mqtt.cpp
            // logs the same DebugAction with a "Cloud: " prefix.
            sanitizedData = "Portal: " + sanitizedData;
            logDebugAction(DebugAction::ADMIN_3PHASE_SETTINGS_SUCCESS, (char *)sanitizedData.c_str());
            // Mirror change to cloud as client attribute (admS) so dashboard reflects it
            publishAdminSettingsToCloud();
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}
void onAdminTimingConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // if (!verifyCookie(request)) {
    //   request->send(401, "text/plain", "Unauthorized");
    //   return;
    // }
    if (!verifyAdminPin(request, data, len)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    if (!saveDeviceTimingConfig(data)) {
        request->send(400, "text/plain", "Invalid request");
        logDebugActionSanitized(DebugAction::ADMIN_SETTINGS_FAILURE, data, len);
        return;
    } else {
        logDebugActionSanitized(DebugAction::ADMIN_BUTTONTIME_SETTINGS_SUCCESS, data, len);
        request->send(200, "text/plain", "Button timing configured successfully");
        // Mirror change to cloud as client attribute (tmS) so dashboard reflects it
        publishTimingConfigToCloud();
    }
}

void getAdminTimingConfig(AsyncWebServerRequest *request) {
    // if (!verifyCookie(request)) {
    //     request->send(401, "text/plain", "Unauthorized");
    //     return;
    // }
    String json = getDeviceTimingConfig();

    request->send(200, "application/json", json);
}

void onScanNetworks(AsyncWebServerRequest *request) {
    // Note: This endpoint doesn't require authentication for first-time setup
    //
    // Async scan protocol: the first call starts a non-blocking scan and
    // returns 202 {"status":"scanning"}; the client polls until it gets the
    // network list (200). A blocking scan here stalls the async_tcp task and
    // keeps the single radio off the AP channel for seconds, which makes
    // phones drop off the setup AP.
    // The handler keeps its own scan status (apiScanStartedMs) instead of
    // trusting WiFi.scanComplete(): the Arduino wrapper reports
    // WIFI_SCAN_FAILED once its internal timeout (max_ms_per_chan * 20 =
    // 2.4s) expires even though the driver is still scanning, and calling
    // scanNetworks() again at that point clobbers the in-flight scan, which
    // then completes with an empty result list. So while a scan we started
    // is within its grace window, polls only report "scanning" and never
    // touch the scan state.
    static uint32_t apiScanStartedMs = 0;
    static bool discardNextScanResult = false;

    int16_t n = WiFi.scanComplete();

    if (n >= 0 && discardNextScanResult) {
        // Leftover completion from a scan we force-stopped - throw it away
        // and fall through to start a fresh one.
        discardNextScanResult = false;
        WiFi.scanDelete();
        n = WIFI_SCAN_FAILED;
    }

    if (n < 0) {
        if (n == WIFI_SCAN_RUNNING && !apiScanStartedMs) {
            // Adopt a scan started elsewhere (e.g. the wifisettings gate)
            apiScanStartedMs = millis();
        }
        if (apiScanStartedMs && (millis() - apiScanStartedMs) < 8000) {
            // A scan is (very likely still) in flight - just keep polling.
            request->send(202, "application/json", "{\"status\":\"scanning\"}");
            return;
        }
        if (apiScanStartedMs) {
            // Grace window expired without a completion: the done event was
            // lost. Stop the driver and discard any result that trickles in;
            // the next poll starts a fresh scan.
            Serial.println("API: scanNetworks - scan stuck, forcing stop");
            esp_wifi_scan_stop();
            WiFi.scanDelete();
            discardNextScanResult = true;
            apiScanStartedMs = 0;
            request->send(202, "application/json", "{\"status\":\"scanning\"}");
            return;
        }
        // Nothing in flight - start a fresh async scan. show_hidden=true,
        // passive=false, 120ms per channel keeps the radio's off-channel
        // gaps short so connected AP clients don't miss beacons and drop.
        int16_t started = WiFi.scanNetworks(true, true, false, 120);
        if (started == WIFI_SCAN_RUNNING) {
            apiScanStartedMs = millis();
            discardNextScanResult = false;
        } else {
            // Driver busy (e.g. STA connect attempt in progress); the start
            // is retried on the next poll.
            Serial.printf("API: scanNetworks - start failed (%d), will retry\n", (int)started);
        }
        request->send(202, "application/json", "{\"status\":\"scanning\"}");
        return;
    }

    apiScanStartedMs = 0;

    JsonDocument doc;
    JsonArray networks = doc.to<JsonArray>();

    if (n == 0) {
        // No networks found
        WiFi.scanDelete();
        request->send(200, "application/json", "[]");
        return;
    }
    
    // Limit to maximum 50 networks to avoid memory issues
    if (n > 50) n = 50;
    
    // Sort networks by signal strength (RSSI) - strongest first
    // Create a simple array of SSIDs with RSSI for sorting
    struct NetworkInfo {
        String ssid;
        int8_t rssi;
        uint8_t encryption;
    };
    
    // Use fixed-size array with maximum expected networks
    const int MAX_NETWORKS = 50;
    NetworkInfo networksList[MAX_NETWORKS];
    for (int i = 0; i < n && i < MAX_NETWORKS; i++) {
        networksList[i].ssid = WiFi.SSID(i);
        networksList[i].rssi = WiFi.RSSI(i);
        networksList[i].encryption = WiFi.encryptionType(i);
    }
    
    // Simple bubble sort by RSSI (strongest first)
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (networksList[j].rssi < networksList[j + 1].rssi) {
                NetworkInfo temp = networksList[j];
                networksList[j] = networksList[j + 1];
                networksList[j + 1] = temp;
            }
        }
    }
    
    // Remove duplicates and add to JSON array
    String seenSSIDs[MAX_NETWORKS];
    int uniqueCount = 0;
    
    for (int i = 0; i < n; i++) {
        bool isDuplicate = false;
        for (int j = 0; j < uniqueCount; j++) {
            if (networksList[i].ssid == seenSSIDs[j]) {
                isDuplicate = true;
                break;
            }
        }
        
        if (!isDuplicate && networksList[i].ssid.length() > 0) {
            JsonObject network = networks.add<JsonObject>();
            network["ssid"] = networksList[i].ssid;
            network["rssi"] = networksList[i].rssi;
            network["encrypted"] = (networksList[i].encryption != WIFI_AUTH_OPEN);
            seenSSIDs[uniqueCount] = networksList[i].ssid;
            uniqueCount++;
        }
    }
    
    String jsonString;
    serializeJson(doc, jsonString);

    request->send(200, "application/json", jsonString);

    // Free the results so the next request starts a fresh scan
    WiFi.scanDelete();
}

void getAdmin3PhaseSettings(AsyncWebServerRequest *request) {
    // if (!verifyAdminPin(request, nullptr)) {
    //   request->send(401, "text/plain", "Unauthorized");
    //   return;
    // }
    JsonDocument doc;
    doc["mac"] = WiFi.macAddress();
    doc["3p"] = isThreePhaseDevice ? (vsmrLinkEnabled() ? "2" : "1") : "0";
    doc["vsmrMac"] = vsmrLinkPeerMacStr();
    doc["tapR"] = tappingRequired;
    doc["tapD"] = tappingDuration;
    doc["ethE"] = ethernetEnabled ? "1" : "0";
    doc["leakE"] = leakDetectionEnabled ? "1" : "0";
    doc["ledT"] = ledType;
    doc["sOrC"] = (String)shuntOrCT;
    doc["inRush"] = inRushExtraSecs;
    String jsonStr;
    serializeJson(doc, jsonStr);
    request->send(200, "application/json", jsonStr);
}

void onSaveMasterSlaveSettings(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    if (!verifyPin(request, data, len)) {
        return;
    }
    // Prevent changing Remote settings when device is ON
    if (relayState) {
        request->send(400, "text/plain", "Cannot change Master / Remote settings when device is ON");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        if (doc.size() > 0) {
            uint8_t masterSlaveModeTemp = doc["masterSlaveMode"].as<uint8_t>();
            String masterDeviceUrlTemp, mPinTemp;
            String remoteMethodsTemp = "";

            if (masterSlaveModeTemp == 2) {
                if (masterSwitchOn || masterSwitchOnStatusMqtt) {
                    request->send(400, "text/plain", "Cannot change Remote settings when Master device is ON");
                    return;
                }
                // Extract remote methods (support both remoteR and rmtMethods for compatibility)
                if (doc["rmtMethods"].is<String>()) {
                    remoteMethodsTemp = doc["rmtMethods"].as<String>();
                }

                // Validate master device URL and PIN if remoteMethods contains "local"
                if (remoteMethodsTemp.indexOf("local") >= 0) {
                    masterDeviceUrlTemp = doc["masterDeviceUrl"].as<String>();
                    mPinTemp = doc["mPin"].as<String>();
                    if (masterDeviceUrlTemp.length() == 0) {
                        request->send(400, "text/plain",
                                      "Master Device URL is required when using local remote method");
                        return;
                    }
                    if (mPinTemp.length() == 0) {
                        request->send(400, "text/plain",
                                      "Master Device PIN is required when using local remote method");
                        return;
                    }
                }
            }

            // Save with updated master-slave settings including remote methods
            saveMasterRemoteSettings(masterSlaveModeTemp, masterDeviceUrlTemp, mPinTemp, remoteMethodsTemp);

            // Update global variables
            masterSlaveMode = masterSlaveModeTemp;
            masterDeviceUrl = masterDeviceUrlTemp;
            mPin = mPinTemp;
            remoteMethods = remoteMethodsTemp;
            if (masterSlaveModeTemp != 2) {
                masterSwitchOnStatusMqtt = false;
                masterSwitchOn =false;
                onTriggerFromInterface = 0;
                onTriggerFromMQTT = 0;
                onTriggerFromWebhook = 0;
                offTriggerFromInterface = 0;
                offTriggerFromMQTT = 0;
                offTriggerFromWebhook = 0;
            }


            request->send(200, "text/plain", "Master-Slave settings saved successfully");
            logDebugActionSanitized(DebugAction::ADMIN_3PHASE_SETTINGS_SUCCESS, data, len);
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

void onGetMasterSlaveSettings(AsyncWebServerRequest *request) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    JsonDocument doc;
    doc["masterSlaveMode"] = masterSlaveMode;
    doc["masterDeviceUrl"] = masterDeviceUrl;
    doc["mPin"] = mPin;
    doc["rmtMethods"] = remoteMethods;
    String jsonStr;
    serializeJson(doc, jsonStr);
    request->send(200, "application/json", jsonStr);
}

void onSaveMQTTSettings(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        if (doc.size() > 0) {
            const char *broker = doc["bkr"];
            int port = doc["port"];
            const char *username = doc["user"];
            const char *password = doc["pass"];
            const char *publishTopic = doc["pTopic"];
            const char *subscribeTopic = doc["sTopic"];
            const char *type = doc["type"]; // Newly added parameter

            if (!broker || !port || !username || !publishTopic || !subscribeTopic || !type) {
                request->send(400, "text/plain", "Missing required MQTT fields");
                return;
            }

            // Save the MQTT settings to persistent storage
            saveMQTTSettings(broker, port, username, password, publishTopic, subscribeTopic, type);

            request->send(200, "text/plain", "MQTT settings saved successfully");
            logDebugActionSanitized(DebugAction::API_CLOUD_SYNC_DETAILS, data, len);
            requestMQTTReconfigure(); // deferred: runs on mqttPublisherTask (see mqtt.h)
            return;
        }
    }
    request->send(400, "text/plain", "Invalid request format");
}

void onGetMQTTSettings(AsyncWebServerRequest *request) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    JsonDocument doc;
    loadMQTTSettings(doc);
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
}

void onListLogs(AsyncWebServerRequest *request) {
    String logType = request->getParam("type")->value();
    
    JsonDocument doc;
    JsonArray fileArray = doc.to<JsonArray>();
    
    String prefix;
    if (logType == "operation") {
        prefix = LOG_FILE_PREFIX_OP;
    } else if (logType == "debug") {
        prefix = LOG_FILE_PREFIX_DB;
    } else if (logType == "energy") {
        prefix = LOG_FILE_PREFIX_ED;
    } else {
        request->send(400, "application/json", "{\"error\":\"Invalid log type\"}");
        return;
    }
    
    File root = LittleFS.open("/logs");
    File file = root.openNextFile();
    
    while (file) {
        String filename = file.name();
        if (filename.startsWith(prefix + "_")) {
            fileArray.add(filename);
        }
        file = root.openNextFile();
    }
    
    String jsonString;
    serializeJson(doc, jsonString);
    request->send(200, "application/json", jsonString);
}

void onSendHADiscovery(AsyncWebServerRequest *request) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    Serial.println("Inside Send Home Assistant Discovery API");

    sendHADiscovery = true;
    request->send(200, "text/plain", "Home Assistant Discovery flag set to true");
}

// Unauthenticated identity endpoint for the EVIQtech app's QR-code device
// lookup flow. Returns the MAC, product type, current device name and
// firmware version so the app can verify "is this the right device?" after
// scanning a QR label, without needing a session cookie or PIN.
void onGetInfo(AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["mac"] = WiFi.macAddress();
    doc["type"] = "SuperSmart";
    doc["name"] = String(switchName);
    doc["ver"] = getFirmwareVersion();
    String jsonStr;
    serializeJson(doc, jsonStr);
    AsyncWebServerResponse *response =
        request->beginResponse(200, "application/json", jsonStr);
    response->addHeader("Access-Control-Allow-Origin", "*");
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
}
// #ifdef LED_RGB_FLOATRELAY
void onEnableFloat(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    JsonDocument doc;
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (!error) {
        if (doc.size() > 0) {
            uint8_t floatMode = doc["floatMode"].as<uint8_t>();
            const char *flTxt = doc["flTxt"];
            // Get flow detection delays (received in seconds from JavaScript)
            // Default to 10 seconds if not provided, but use 0 if explicitly set to 0
            uint16_t flowDetectDelay = 10;  // Default 10 seconds
            uint16_t flowStopDelay = 10;    // Default 10 seconds
            if (doc["flowDetectDelay"].as<uint16_t>()) {
                flowDetectDelay = doc["flowDetectDelay"].as<uint16_t>();
            }
            if (doc["flowStopDelay"].as<uint16_t>()) {
                flowStopDelay = doc["flowStopDelay"].as<uint16_t>();
            }
            Serial.println("inside float save");
            if (floatMode==1 || floatMode==2) {
                if (flTxt != nullptr && strcmp(flTxt, "220V is not connected to float switch") == 0) {
                    Serial.println(" float save condition matched");

                    saveFloatDetails((uint8_t)floatMode, flowDetectDelay, flowStopDelay);
                    // Update global variables (declared in globals.h, stored in seconds)
                    flowDetectionDelaySec = flowDetectDelay;
                    flowStopDelaySec = flowStopDelay;
                    request->send(200, "text/plain", "Float settings saved successfully");
                    {
                        // mode 1 = float switch, 2 = flow sensor. The delays are
                        // recorded because a wrong one shows up later as a
                        // "device won't start / won't stop" complaint.
                        char dbgBuf[DEBUG_MESSAGE_SIZE];
                        snprintf(dbgBuf, sizeof(dbgBuf),
                                 "Portal: float enabled. mode=%u detectDelay=%us stopDelay=%us", (unsigned)floatMode,
                                 (unsigned)flowDetectDelay, (unsigned)flowStopDelay);
                        logDebugAction(DebugAction::ADMIN_3PHASE_SETTINGS_SUCCESS, dbgBuf);
                    }
                    Serial.printf("the_float_flage is %d\n",floatMode);
                    // Switch the mode and the pin over FIRST, then clear the detect
                    // timers. This handler runs on the webserver task while loop() is
                    // concurrently running checkFloat()/checkFlow() on the other core -
                    // clearing first lets the outgoing mode's handler re-latch a timer
                    // that nothing will ever clear again (it stays non-zero until
                    // reboot and keeps notifyClients() firing every second).
                    // Serial.println("floatMode: " + String(floatMode));
                    isFloatEnabled = floatMode;
                    // Serial.println("isFloatEnabled: " + String(isFloatEnabled));
                    // Serial.printf("sw status %d\n",isFloatEnabled);
                    digitalWrite(RELAY_OUT_FLOAT, HIGH);
                    pinMode(FLOAT_SWITCH, INPUT_PULLDOWN);
                    // Any re-latch from here on belongs to the newly selected mode's own
                    // handler, which keeps maintaining it - so it is safe to reset.
                    if (floatMode == 1) {
                        flowIncomingState = false;
                        Flow_stopped_manual = false;
                        flow_start_time=0;
                        flow_stop_time=0;
                        floatLowStartTime=0;
                        floatingAlertState=false;

                    } else {
                        floatingAlertState=false;
                        floatLowStartTime=0;
                        flowIncomingState=false;
                        flow_stop_time=0;
                        flow_start_time=0;
                    }
                    return;
                } else {
                    request->send(400, "text/plain", "Invalid float settings");
                    // The wiring-confirmation string did not match, so nothing
                    // was saved - worth recording, since the user saw a failure
                    // and the settings stayed as they were.
                    logDebugAction(DebugAction::ADMIN_SETTINGS_FAILURE,
                                   (char *)"Portal: float rejected - wiring confirmation missing/mismatched");
                    return;
                }
            } else {
                saveFloatDetails(floatMode, flowDetectDelay, flowStopDelay);
                // Update global variables (declared in globals.h, stored in seconds)
                flowDetectionDelaySec = flowDetectDelay;
                flowStopDelaySec = flowStopDelay;
                // Disable the mode and the pin BEFORE clearing the detect timers.
                // saveFloatDetails() above commits to NVS (milliseconds), so clearing
                // first left a window in which loop() - still seeing the old mode - could
                // re-latch flow_start_time/floatLowStartTime. With the mode already 0 no
                // handler runs again, so the timer stayed non-zero until reboot and drove
                // notifyClients() at 1 Hz forever.
                isFloatEnabled = floatMode;
                Serial.printf("sw status %d\n", isFloatEnabled);
                digitalWrite(RELAY_OUT_FLOAT, LOW);
                pinMode(FLOAT_SWITCH, INPUT_PULLUP);
                flowIncomingState = false;
                Flow_stopped_manual = false;
                floatingAlertState = false;
                flow_start_time=0;
                flow_stop_time=0;
                floatLowStartTime=0;
                request->send(200, "text/plain", "Float disabled successfully");
                logDebugAction(DebugAction::ADMIN_3PHASE_SETTINGS_SUCCESS, (char *)"Portal: float disabled");
                return;
            }
        }
    }
    request->send(400, "text/plain", "Invalid request format");
    // Malformed body or empty JSON object: float settings were left untouched.
    logDebugAction(DebugAction::ADMIN_SETTINGS_FAILURE,
                   (char *)"Portal: float rejected - malformed or empty body");
}

void onGetFloatSetting(AsyncWebServerRequest *request) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    String jsonResponse = "{\"enableFloat\": " + String(isFloatEnabled ? "true" : "false") + "}";
    request->send(200, "application/json", jsonResponse);
}
// #endif

// Webhook trigger API
void onSetState(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Serial.println("millis() starting API");
    // Serial.println(millis());

    if (masterSlaveMode != 1 ){
        request->send(400, "text/plain", "The device is not setup as a Master");
        // Serial.println("Master-Slave mode");
        // Serial.println(masterSlaveMode);
        return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    
    if (error) {
        request->send(400, "text/plain", "Invalid request format");
        return;
    }
    
    if (!doc["pin"]) {
        request->send(400, "text/plain", "Pin not provided");
        return;
    }
    
    const char *pin = doc["pin"];
    if (!verifyPinWithParameter(pin)) {
        request->send(401, "text/plain", "Unauthorized - Invalid pin");
        return;
    }
    
    // Get state query parameter
    if (!request->hasParam("state")) {
        request->send(400, "text/plain", "State parameter is required");
        return;
    }
    
    String state = request->getParam("state")->value();
    state.toUpperCase();
    
    // Check for optional mode parameter
    String mode = "";
    int buttonNo = 0, durationMinutes = 0;
    TimeSpan duration;
    if (request->hasParam("mode")) {
        mode = request->getParam("mode")->value();
        mode.toLowerCase();
    }
    if (request->hasParam("buttonNo")) {
        buttonNo = request->getParam("buttonNo")->value().toInt();
        if ((buttonNo < 1 || buttonNo > 4) && buttonNo != 11) {
            request->send(400, "text/plain", "Invalid button number. Must be 1-4 or 11");
            return;
        }
    }
    if (request->hasParam("durationMinutes")) {
        durationMinutes = request->getParam("durationMinutes")->value().toInt();
        if (durationMinutes < 1 || durationMinutes > 24*60 ) {
            request->send(400, "text/plain", "Invalid duration minutes. Must be 1-1440");
            return;
        }
    }
    
    // Handle button press mode
  
        
        if (state == "ON") {
            if (relayState == 1){
                request->send(201, "text/plain", "Device is ON already");
                return;
            } else if ( floatingAlertState || highVoltageAlertState || lowVoltageAlertState || phaseFailureAlertState || phaseSequenceReverseAlertState || (isFloatEnabled == 2 && !flowIncomingState)){
                request->send(202, "text/plain", "Device is having some fault condition");
                return;
            }
            if (mode == "" || (mode == "buttonpress" && buttonNo == 0) || (mode == "duration" && durationMinutes == 0) || (mode != "buttonpress" && mode != "duration")) {
                    request->send(400, "text/plain", "Invalid mode or duration minutes");
                    return;
            }
            if (mode == "buttonpress") {
                 duration = getDuration(buttonNo);
            } else if (mode == "duration") {
                 duration = TimeSpan(durationMinutes * 60);
            }
            // Serial.println("millis() before setting onTriggerFromWebhook");
            // Serial.println(millis());

            onTriggerFromWebhook = 1;
            onTriggerDurationFromWebhook = duration.totalseconds();
            request->send(200, "text/plain", "Button " + String(buttonNo) + " press trigger set successfully");
        } else if (state == "OFF") {
            if (relayState == 0) {
                request->send(201, "text/plain", "Device is OFF already");
                return;
            }
            // For OFF, we can use manual button press logic
            offTriggerFromWebhook = true;
            request->send(200, "text/plain", "Button OFF trigger set successfully");
        } else {
            request->send(400, "text/plain", "Invalid state. Use ON or OFF");
        }
   
}

// Setup API endpoints
void setupAPIs() {
    // Serve the HTML form
    // Serial.println("setup APIs");

    server.on("/api/info", HTTP_GET, onGetInfo);
    server.on("/api/login", HTTP_POST, onRequest, onFileUpload, onLogin);
    server.on("/api/setCalendarTime", HTTP_POST, onRequest, onFileUpload, onSystemTimeReset);
    server.on("/api/setSchedule", HTTP_POST, onRequest, onFileUpload, onScheduleUpload);
    server.on("/api/setWiFi", HTTP_POST, onRequest, onFileUpload, onWiFiDetails);
    server.on("/api/removeWiFi", HTTP_POST, onRemoveWiFi);
    server.on("/api/setDevice", HTTP_POST, onRequest, onFileUpload, onDeviceDetails);
    server.on("/api/firstUseSetup", HTTP_POST, onRequest, onFileUpload, onFirstUseSetup);
    server.on("/api/setDeviceName", HTTP_POST, onRequest, onFileUpload, onDeviceName);
    server.on("/api/setDevicePin", HTTP_POST, onRequest, onFileUpload, onDevicePin);
    server.on("/api/setCloudSync", HTTP_POST, onRequest, onFileUpload, onCloudSyncSettings);
    server.on("/api/setAPPass", HTTP_POST, onRequest, onFileUpload, onAPPassSet);
    server.on("/api/connectCloud", HTTP_POST, onRequest, onFileUpload, onCloudConnect);
    server.on("/api/pauseCloud", HTTP_POST, onRequest, onFileUpload, onPauseCloud);
    // server.on("/api/resetDevice", HTTP_POST, onRequest, onFileUpload,
    // onFactoryResetDevice);
    server.on("/api/setCur", HTTP_POST, onRequest, onFileUpload, onSetCurLimit);
    server.on("/adminapi/setTimingConfig", HTTP_POST, onRequest, onFileUpload, onAdminTimingConfig);
    server.on("/adminapi/getTimingConfig", HTTP_GET, getAdminTimingConfig);
    server.on("/adminapi/3phasesettings", HTTP_POST, onRequest, onFileUpload, onSave3PhaseSettings);
    server.on("/adminapi/get3PhaseSettings", HTTP_GET, getAdmin3PhaseSettings);
    server.on("/api/saveMasterSlaveSettings", HTTP_POST, onRequest, onFileUpload, onSaveMasterSlaveSettings);
    server.on("/api/getMasterSlaveSettings", HTTP_GET, onGetMasterSlaveSettings);
    server.on("/api/saveMQTTSettings", HTTP_POST, onRequest, onFileUpload, onSaveMQTTSettings);
    server.on("/api/getMQTTSettings", HTTP_GET, onGetMQTTSettings);
    server.on("/api/setRootCACert", HTTP_POST, onRequest, onFileUpload, onSetRootCACert);
    server.on("/api/listLogs", HTTP_GET, onListLogs);
    server.on("/api/sendHADiscovery", HTTP_POST, onSendHADiscovery);
// #ifdef LED_RGB_FLOATRELAY
    if (ledType == 2 || ledType == 3 ) {
        server.on("/api/enableFloat", HTTP_POST, onRequest, onFileUpload, onEnableFloat);
        server.on("/api/getFloatSetting", HTTP_GET, onGetFloatSetting);
    }
// #endif
    server.on("/api/restartDevice", HTTP_POST, onRequest, onFileUpload, onRestartSystem);
    server.on("/api/rtcBatteryReplaced", HTTP_POST, onRequest, onFileUpload, onRtcBatteryReplaced);
    server.on("/api/clearFaultConditions", HTTP_POST, onRequest, onFileUpload, onClearFaultConditions);
    server.on("/api/setState", HTTP_POST, onRequest, onFileUpload, onSetState);
    server.on("/api/getState", HTTP_GET, [](AsyncWebServerRequest *request) {
        String state = (relayState == 1) ? "ON" : "OFF";
        request->send(200, "text/plain", state);
    });
    server.on("/api/getSchedule", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!verifyCookie(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        if (isScheduleSet) {
            String json = getScheduleJsonString(gScheduleArray);
            request->send(200, "application/json", json);
        } else {
            String json = "{}";
            request->send(200, "application/json", json);
        }
    });
    server.on("/api/getSwitchStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Serial.println("API: getSwitchStatus");
        if (!verifyCookie(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        String returnStr = (String) "{\"relayStatus\":\"" + (String)(relayState ? "ON" : "OFF") + (String) "\"}";
        request->send(200, "application/json", returnStr);
    });
    server.on("/api/getCloudSyncSettings", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Serial.println("API: getCloudSyncSettings");
        if (!verifyCookie(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        String returnStr = (String) "{\"syncEnabled\":\"" + (String)(isCloudConnected ? "1" : "0") + (String) "\"}";
        request->send(200, "application/json", returnStr);
    });
    server.on("/api/getWiFiStatus", HTTP_GET, [](AsyncWebServerRequest *request) {
        // Serial.println("API: getWiFiStatus");
        if (!verifyCookie(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        JsonDocument doc = getWiFiSettings();
        String jsonString;
        serializeJson(doc, jsonString);
        request->send(200, "text/json", jsonString);
    });
    server.on("/api/scanNetworks", HTTP_GET, onScanNetworks);
    server.on("/api/getRtcTime", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!verifyCookie(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        DateTime now = getDateTimeFromRTC();
        String currentTime = formatDateTime(now);
        request->send(200, "text/plain", currentTime);
    });
    // server.on("/api/getInternalVars1", HTTP_GET, [](AsyncWebServerRequest *request) {
    //     // if (!verifyCookie(request)) {
    //     //   request->send(401, "text/plain", "Unauthorized");
    //     //   return;
    //     // }
    //     String varsString = getGlobalScheduleValuesfromMain();
    //     request->send(200, "text/plain", varsString);
    // });
    // server.on("/api/getInternalVars2", HTTP_GET, [](AsyncWebServerRequest *request) {
    //     // if (!verifyCookie(request)) {
    //     //       request->send(401, "text/plain", "Unauthorized");
    //     //       return;
    //     //     }
    //     // String varsString = getGlobalOtherValuesfromMain();
    //     request->send(200, "text/plain", varsString);
    // });
    server.on("/api/logout", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (logoutSession(request)) {
            AsyncWebServerResponse *response = request->beginResponse(200, "application/json", "{\"success\": true, \"message\": \"Logout successful\"}");
            response->addHeader("Set-Cookie", "SESSIONID=; Path=/; Max-Age=0");
            request->send(response);
        } else {
            request->send(401, "application/json", "{\"success\": false, \"message\": \"Unauthorized\"}");
        }
    });
    server.on("/api/getLogs", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!verifyCookie(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        // Single-allocation String build: measure once, reserve, serialize.
        // Cheaper than doc.as<String>() (which reallocs as it grows) and
        // simpler/more compatible than AsyncResponseStream chunked encoding.
        JsonDocument doc;
        prepareLogsJson(doc);
        String body;
        body.reserve(measureJson(doc) + 1);
        serializeJson(doc, body);
        request->send(200, "application/json", body);
    });
    server.on("/api/getEnergyData", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!verifyCookie(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        JsonDocument doc;
        prepareEnergyDataJson(doc);
        String body;
        body.reserve(measureJson(doc) + 1);
        serializeJson(doc, body);
        request->send(200, "application/json", body);
    });
    server.on("/api/clearLogs", HTTP_POST, onRequest, onFileUpload, onClearLogs);
    // server.on("/api/migrateLogs", HTTP_POST, onRequest, onFileUpload, onMigrateLogs);
    server.on("/api/getDebugLogFiles", HTTP_GET, onGetDebugLogFiles);
    server.on("/api/getEnergyLogFiles", HTTP_GET, onGetEnergyLogFiles);
    server.on("/api/getOperationLogFiles", HTTP_GET, onGetOperationLogFiles);
    server.on("/api/getAllFiles", HTTP_GET, onGetAllFiles);
    // server.on("/api/getMigrationStatus", HTTP_GET, onGetMigrationStatus);
    server.on("/api/clearLogsByType", HTTP_POST, onRequest, onFileUpload, onClearLogsByType);
    server.on("/api/renameFile", HTTP_POST, onRequest, onFileUpload, onRenameFile);

    server.on("/api/resetCumulativeEnergy", HTTP_POST, onRequest, onFileUpload, onResetCumulativeEnergy);

    server.on("/api/getCur", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!verifyCookie(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        JsonDocument doc = getCurSettings();
        String jsonString;
        serializeJson(doc, jsonString);
        request->send(200, "text/json", jsonString);

        //  if (isPLimitSet) {
        //     JsonDocument doc=getCurSettings();
        //     String jsonString;
        //     serializeJson(doc, jsonString);
        //     request->send(200, "text/json", jsonString);
        //  }
        //  else
        //  {
        //     request->send(400, "text/plain", "Current settings not set");
        //  }
    });
    server.on("/api/getFirmwareVersion", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!verifyCookie(request)) {
            request->send(401, "text/plain", "Unauthorized");
            return;
        }
        String firmwareVersion = getFirmwareVersion();
        request->send(200, "text/plain", firmwareVersion);
    });
    
    server.on(
        "/updateSystem", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            String message;
            webUploadActive = false;
            if (Update.hasError() || updateError) {
                // Prefer the specific message captured during upload-time
                // validation (wrong PIN, device on, etc.). Falls back to the
                // generic message only when no specific reason was captured.
                int status  = (updateError && updateErrorMessage.length()) ? updateErrorStatus : 500;
                String body = (updateError && updateErrorMessage.length())
                                  ? updateErrorMessage
                                  : String("Firmware update failed - ") + Update.errorString();
                logDebugAction(DebugAction::API_UPDATE_FIRMWARE_FAILED, (char *)body.c_str());
                request->send(status, "text/plain", body);
                // Leave nothing behind for the next attempt: the Update object
                // keeps its error code until a successful begin(), and these
                // globals gate the whole upload path.
                if (Update.isRunning()) Update.abort();
                Update.clearError();
                clearUploadErrorState();
                return;
            } else {
                if (getWiFiMode() == 'A')
                    message = "Update Successful, Rebooting...  Wait for a few seconds "
                              "and connect again. Make sure to connect to the device AP "
                              "again, before connecting";
                else
                    message = "Update Successful, Rebooting... <br> Wait for a few seconds "
                              "and connect again.";

                    // char tempStr[100];
                    // sprintf(tempStr, "FIRMWARE UPDATE SUCCESSFUL. Previous firmware
                    // version : %s", getFirmwareVersion());
                    char tempStr[100];
                    snprintf(tempStr, sizeof(tempStr), "FIRMWARE UPDATE SUCCESSFUL. Previous firmware version : %s",
                            getFirmwareVersion().c_str());
                    logDebugAction(DebugAction::API_UPDATE_FIRMWARE_SUCCESS, tempStr);
                    // Restart from loop() rather than from the AsyncTCP
                    // disconnect callback. Resetting inside that callback races
                    // delivery of the 200 below, which makes a successful
                    // update show up in the portal as "Failed to fetch".
                    pendingRestartAt = (uint32_t)millis() + 2000;
                    if (pendingRestartAt == 0) pendingRestartAt = 1;
                request->send(200, "text/html", message);
                // delay(5000);
                // ESP.restart();
                // esp_sleep_enable_timer_wakeup(1 * 1000000);
                // esp_deep_sleep_start();
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                // Serial.printf("Upload Start: %s\n", filename.c_str());
                // Note: we don't call request->send() here. AsyncWebServer
                // would queue the response, then the completion handler
                // overwrites it with the generic "Firmware update failed"
                // once the body is fully consumed. Instead we stash the
                // specific status+message and let the completion handler
                // send it.

                // Clear whatever the previous attempt left behind. These are
                // globals and are only reset by the completion handler, which
                // never runs for an upload that was cut off mid-stream - so
                // without this a single interrupted upload makes every later
                // attempt fail with the *previous* attempt's message.
                clearUploadErrorState();
                if (Update.isRunning()) {
                    Serial.printf("Releasing stale OTA session (%u/%u bytes)\n", (unsigned)Update.progress(),
                                  (unsigned)Update.size());
                    Update.abort();
                }
                Update.clearError();
                webUploadActive   = false;
                webUploadBytes    = 0;
                webUploadLastSeen = millis();

                const AsyncWebHeader *pinHeader = request->getHeader("X-Upload-Pin");
                if (!pinHeader) {
                    updateErrorStatus  = 403;
                    updateErrorMessage = "PIN required";
                    Update.abort();
                    updateError = true;
                    return;
                }
                if (relayState != 0) {
                    updateErrorStatus  = 400;
                    updateErrorMessage = "Device is ON, Please try when the device is OFF";
                    Update.abort();
                    updateError = true;
                    return;
                }
                String enteredPin = pinHeader->value();
                if (strcmp(enteredPin.c_str(), devicePin) != 0) {
                    Serial.println(F("Wrong device Pin"));
                    updateErrorStatus  = 403;
                    updateErrorMessage = "Wrong device Pin";
                    Update.abort();
                    updateError = true;
                    return;
                }
                // A cloud OTA shares the one global Update object, so the two
                // cannot overlap. Say so instead of letting begin() fail with
                // an unexplained "already running".
                if (otaState == OTA_DOWNLOADING || otaState == OTA_UPDATING) {
                    updateErrorStatus  = 409;
                    updateErrorMessage = "A cloud firmware update is in progress. Try again in a few minutes.";
                    updateError        = true;
                    return;
                }
                // Validate filename
                if (filename.startsWith("firmware") && filename.endsWith(".bin")) {
                    // Baseline for the progress lines below. A transfer that
                    // dies mid-way leaves the last line printed at the byte
                    // offset where it died, next to the heap figures at that
                    // moment - which is what separates "the device ran out of
                    // memory" from "the link went away" from "it panicked"
                    // (a panic shows up as a reset reason on the next boot).
                    webUploadStartMs   = millis();
                    webUploadLoggedAt  = 0;
                    // Real abandonment, reported by the TCP stack rather than
                    // guessed from a silence timer. Fires for a mid-body drop
                    // too, and is a no-op once the upload has finished because
                    // releaseFirmwareUpload() checks webUploadActive.
                    request->onDisconnect([]() { releaseFirmwareUpload("disconnected"); });
                    if (request->client()) {
                        request->client()->setRxTimeout(WEB_UPLOAD_RX_TIMEOUT_S);
                    }
                    Serial.printf("Upload start: %s | heap %u B, largest %u B, min-ever %u B | WiFi mode %c, RSSI %d\n",
                                  filename.c_str(), ESP.getFreeHeap(),
                                  heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), ESP.getMinFreeHeap(),
                                  getWiFiMode(), WiFi.RSSI());
                    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                        String reason = updateBeginFailureReason();
                        Update.printError(Serial);
                        Serial.println("OTA begin failed: " + reason);
                        updateErrorStatus  = 500;
                        updateErrorMessage = "OTA Update Failed to Begin - " + reason;
                        Update.abort();
                        updateError = true;
                        char dbgPayload[DEBUG_MESSAGE_SIZE];
                        snprintf(dbgPayload, sizeof(dbgPayload), "Begin failed: %s", reason.c_str());
                        logDebugAction(DebugAction::API_UPDATE_FIRMWARE_FAILED, dbgPayload);
                        return;
                    }
                    webUploadActive = true;
                } else {
                    Serial.println("Invalid file name. Aborting update.");
                    updateErrorStatus  = 400;
                    updateErrorMessage = "Invalid file name. Upload aborted.";
                    Update.abort();
                    updateError = true;
                    return;
                }
            }

            // Serial.println("after index=0");
            if (updateError) {
                // Important: Still need to "consume" the data even if there's an error
                // Otherwise the library might keep calling this handler
                return;
            }
            // Keeps serviceFirmwareUpload() from reaping a transfer that is
            // merely slow.
            webUploadLastSeen = millis();
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
                // request->send(500, "text/plain", "OTA Update Failed!");
                // Flag it: without updateError the handler keeps calling write()
                // on an aborted Update for the rest of the body, and the
                // completion handler can only report the generic failure.
                updateErrorStatus  = 500;
                updateErrorMessage = String("Flash write failed at ") + (unsigned)(index + len) + " bytes - "
                                     + Update.errorString();
                updateError = true;
                Update.abort();
                webUploadActive = false;
                return;
            }
            webUploadBytes = index + len;
            if (webUploadBytes - webUploadLoggedAt >= WEB_UPLOAD_LOG_STEP) {
                webUploadLoggedAt = webUploadBytes;
                Serial.printf("Upload %u KB | heap %u B, largest %u B, min-ever %u B | %lu ms\n",
                              (unsigned)(webUploadBytes / 1024), ESP.getFreeHeap(),
                              heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), ESP.getMinFreeHeap(),
                              (unsigned long)(millis() - webUploadStartMs));
            }
            versionSetup();

            if (final) {
                webUploadActive = false;
                Serial.printf("Upload complete: %u bytes in %lu ms | heap %u B, min-ever %u B\n",
                              (unsigned)webUploadBytes, (unsigned long)(millis() - webUploadStartMs),
                              ESP.getFreeHeap(), ESP.getMinFreeHeap());
                if (Update.end(true)) {
                    // Serial.printf("Update Success: %u bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                    updateErrorStatus  = 500;
                    updateErrorMessage = String("Firmware image rejected after ") + (unsigned)(index + len)
                                         + " bytes - " + Update.errorString();
                    updateError = true;
                }
            }
        });

    // server.serveStatic("/logs", LittleFS, "/logs/");
    AsyncStaticWebHandler *handler = &server.serveStatic("/logs", LittleFS, "/logs/");
    handler->setCacheControl("no-cache, no-store, must-revalidate");

    server.onNotFound([](AsyncWebServerRequest *request) { handleHTMLRequest(request); });
}


void startWebServer() {
  // Clear previous handlers (optional, depending on your setup)
//   server.reset(); // Reset server to clear old routes
//    setupAPIs();

  server.begin();
  Serial.println("Web server started");
}

void stopWebServer() {
  server.end();
  Serial.println("Web server stopped");
}
/*
Size of char: 1
Size of bool: 1
Size of short: 2
Size of int: 4
Size of long: 4
Size of long long: 8
Size of float: 4
Size of double: 8

App Partition: app0, type: 0, subtype: 16, address: 0x010000, size: 0x330000
App Partition: app1, type: 0, subtype: 17, address: 0x340000, size: 0x330000
Data Partition: nvs, type: 1, subtype: 2, address: 0x009000, size: 0x005000
Data Partition: otadata, type: 1, subtype: 0, address: 0x00E000, size: 0x002000
Data Partition: spiffs, type: 1, subtype: 130, address: 0x670000, size: 0x180000
Data Partition: coredump, type: 1, subtype: 3, address: 0x7F0000, size: 0x010000
*/

void onGetDebugLogFiles(AsyncWebServerRequest *request) {
    // if (!verifyCookie(request)) {
    //     request->send(401, "text/plain", "Unauthorized");
    //     return;
    // }
    
    // Get debug log files
    std::vector<String> files = getLogFilesSorted(LOG_FILE_PREFIX_DB);
    
    JsonDocument doc;
    JsonArray fileArray = doc.to<JsonArray>();
    
    for (const String& filename : files) {
        fileArray.add(filename);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void onGetEnergyLogFiles(AsyncWebServerRequest *request) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    
    // Get energy log files
    std::vector<String> files = getLogFilesSorted(LOG_FILE_PREFIX_ED);
    
    JsonDocument doc;
    JsonArray fileArray = doc.to<JsonArray>();
    
    for (const String& filename : files) {
        fileArray.add(filename);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void onGetOperationLogFiles(AsyncWebServerRequest *request) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    
    // Get operation log files
    std::vector<String> files = getLogFilesSorted(LOG_FILE_PREFIX_OP);
    
    JsonDocument doc;
    JsonArray fileArray = doc.to<JsonArray>();
    
    for (const String& filename : files) {
        fileArray.add(filename);
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void onGetAllFiles(AsyncWebServerRequest *request) {
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    
    JsonDocument doc;
    JsonArray fileArray = doc.to<JsonArray>();
    
    File root = LittleFS.open("/logs/");
    if (!root) {
        request->send(500, "application/json", "{\"error\":\"Failed to open root directory\"}");
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        JsonObject fileObj = fileArray.add<JsonObject>();
        fileObj["name"] = file.name();
        fileObj["size"] = file.size();
        fileObj["isDirectory"] = file.isDirectory();
        
        file = root.openNextFile();
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void onClearLogsByType(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }

    
    if (!verifyPin(request, data, len)) {
        return;
    }
    
    String logType = request->getParam("type")->value();
    LogType type;
    
    if (logType == "debug") {
        type = LOG_TYPE_DEBUG;
    } else if (logType == "operation") {
        type = LOG_TYPE_OPERATION;
    } else if (logType == "energy") {
        type = LOG_TYPE_ENERGY;
    } else {
        request->send(400, "text/plain", "Invalid log type. Use 'debug', 'operation', or 'energy'");
        return;
    }
    
    clearLogsByType(type);
    
    String logTypeName = (type == LOG_TYPE_DEBUG) ? "debug" : 
                        (type == LOG_TYPE_OPERATION) ? "operation" : "energy";
    
    logDebugAction(DebugAction::API_CLEAR_LOGS, (char*)("Device " + logTypeName + " logs cleared").c_str());
    request->send(200, "text/plain", logTypeName + " logs cleared successfully");
}

void onRenameFile(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.println("onRenameFile");
    if (!verifyCookie(request)) {
        request->send(401, "text/plain", "Unauthorized");
        return;
    }
    
    if (!verifyPin(request, data, len)) {
        return;
    }
    
    // Parse JSON data
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data, len);
    if (error) {
        request->send(400, "text/plain", "Invalid JSON format");
        return;
    }
    
    // Extract old and new filenames
    if (!doc["oldName"].is<String>() || !doc["newName"].is<String>()) {
        request->send(400, "text/plain", "Missing 'oldName' or 'newName' parameter");
        return;
    }
    
    String oldName = doc["oldName"].as<String>();
    String newName = doc["newName"].as<String>();
    
    // Validate filenames
    if (oldName.length() == 0 || newName.length() == 0) {
        request->send(400, "text/plain", "Filename cannot be empty");
        return;
    }
    
    // Ensure paths are within /logs directory for security
    if (oldName.indexOf("..") != -1 || newName.indexOf("..") != -1) {
        request->send(400, "text/plain", "Invalid filename");
        return;
    }
    
    // Construct full paths
    String oldPath = "/logs/" + oldName;
    String newPath = "/logs/" + newName;
    
    // Check if source file exists
    if (!LittleFS.exists(oldPath)) {
        request->send(404, "text/plain", "Source file not found: " + oldName);
        return;
    }
    
    // Check if destination file already exists
    if (LittleFS.exists(newPath)) {
        request->send(409, "text/plain", "Destination file already exists: " + newName);
        return;
    }
    
    // Attempt to rename the file
    if (LittleFS.rename(oldPath, newPath)) {
        String successMsg = "File renamed successfully from " + oldName + " to " + newName;
        logDebugAction(DebugAction::API_RENAME_FILE, (char*)successMsg.c_str());
        request->send(200, "text/plain", successMsg);
    } else {
        String errorMsg = "Failed to rename file from " + oldName + " to " + newName;
        logDebugAction(DebugAction::SYSTEM_FAILURE, (char*)errorMsg.c_str());
        request->send(500, "text/plain", errorMsg);
    }
}
