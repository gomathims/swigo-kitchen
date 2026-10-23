#include "ble_provisioning.h"
#include "logdebug.h"
#include "mqtt.h"
#include <NimBLEDevice.h>

// Global variables
bool bleProvisioningActive = false;
unsigned long bleProvisioningStartTime = 0;
BLEProvisioningState bleProvisioningState = BLE_IDLE;
bool bleProvisioningComplete = false;  // Flag to indicate provisioning completed successfully
String bleProvisioningErrorCode = "";  // Error code to send from main loop (not from callback)
NimBLEServer* pBLEServer = nullptr;
NimBLECharacteristic* pStatusCharacteristic = nullptr;

// Custom BLE Server Callbacks Class
class MyBLEServerCallbacks: public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer);
    void onDisconnect(NimBLEServer* pServer);
};

// Custom BLE Characteristic Callbacks Class
class MyBLECharacteristicCallbacks: public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic);
};

// BLE Server Callbacks Implementation
void MyBLEServerCallbacks::onConnect(NimBLEServer* pServer) {
    Serial.println("BLE Client connected");
    bleProvisioningState = BLE_CONNECTED;
    if (pStatusCharacteristic) {
        pStatusCharacteristic->setValue("CONNECTED");
        pStatusCharacteristic->notify();
    }
}

void MyBLEServerCallbacks::onDisconnect(NimBLEServer* pServer) {
    Serial.println("BLE Client disconnected");
    if (bleProvisioningState == BLE_CONNECTED) {
        bleProvisioningState = BLE_ADVERTISING;
        // Restart advertising
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        pAdvertising->start();
        Serial.println("BLE Advertising restarted");
    }
}

// BLE Characteristic Callbacks Implementation
void MyBLECharacteristicCallbacks::onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    
    if (value.length() > 0) {
        Serial.println("Received BLE provisioning data");
        Serial.print("Data length: ");
        Serial.println(value.length());
        
        // Process the received JSON data
        // Returns empty string on success, or a specific error code
        String errorCode = processBLEProvisioningData(value.c_str());

        if (errorCode.isEmpty()) {
            bleProvisioningState = BLE_PROVISIONING_SUCCESS;
            Serial.println("BLE Provisioning successful");
            Serial.println("WiFi credentials saved.");

            // Set flag for main loop to handle notification + restart.
            // DO NOT notify or stop BLE from callback - causes deadlock
            // and the notification arrives before the app finishes the write.
            // isFirstTime is cleared and persisted inside
            // processBLEProvisioningData() - it must reach NVS before the
            // restart below, or the device comes back up on firstuse.html.
            bleProvisioningComplete = true;
        } else {
            bleProvisioningState = BLE_PROVISIONING_FAILED;
            // DO NOT notify from callback — same reason as SUCCESS path.
            // Sending a notification from onWrite callback is unreliable:
            // the app may not receive it because the write response hasn't
            // been sent yet. Defer to main loop via checkBLETimeout().
            bleProvisioningErrorCode = errorCode;
            Serial.print("BLE Provisioning failed: ");
            Serial.println(errorCode);
        }
    }
}

// Initialize BLE
void initBLE() {
    Serial.println("Initializing BLE...");
    
    // Print free heap before anything
    Serial.print("Free heap at start: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("Free PSRAM: ");
    Serial.println(ESP.getFreePsram());
    
    // Check if BLE is already initialized
    if (NimBLEDevice::getInitialized()) {
        Serial.println("BLE already initialized, deinitializing first...");
        NimBLEDevice::deinit(true);
        delay(200); // Give time for cleanup
    }
    
    // Get device name for BLE
    String deviceName = getBLEDeviceName();
    
    Serial.print("BLE Device Name: ");
    Serial.println(deviceName);
    
    // Free up memory before BLE init
    Serial.println("Forcing garbage collection...");
    ESP.getFreeHeap(); // Force heap check
    delay(100);
    
    Serial.print("Free heap before init: ");
    Serial.println(ESP.getFreeHeap());
    
    // Initialize NimBLE with minimal memory footprint
    Serial.println("Calling NimBLEDevice::init()...");
    
    // Try-catch won't work in Arduino, but we can check heap
    if (ESP.getFreeHeap() < 30000) {
        Serial.println("ERROR: Insufficient heap memory for BLE!");
        Serial.print("Free heap: ");
        Serial.println(ESP.getFreeHeap());
        return;
    }
    
    NimBLEDevice::init(deviceName.c_str());
    
    Serial.println("NimBLEDevice::init() completed successfully!");
    Serial.print("Free heap after init: ");
    Serial.println(ESP.getFreeHeap());
    
    // Create BLE Server
    Serial.println("Creating BLE Server...");
    pBLEServer = NimBLEDevice::createServer();
    if (!pBLEServer) {
        Serial.println("Failed to create BLE Server!");
        return;
    }
    pBLEServer->setCallbacks(new MyBLEServerCallbacks());
    Serial.println("BLE Server created successfully");
    
    // Create BLE Service
    Serial.println("Creating BLE Service...");
    NimBLEService* pService = pBLEServer->createService(BLE_SERVICE_UUID);
    if (!pService) {
        Serial.println("Failed to create BLE Service!");
        return;
    }
    Serial.println("BLE Service created");
    
    // Create WiFi Credentials Characteristic (Write)
    NimBLECharacteristic* pWifiCharacteristic = pService->createCharacteristic(
        BLE_WIFI_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE
    );
    pWifiCharacteristic->setCallbacks(new MyBLECharacteristicCallbacks());
    
    // Create Status Characteristic (Read + Notify)
    pStatusCharacteristic = pService->createCharacteristic(
        BLE_STATUS_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    pStatusCharacteristic->setValue("READY");
    
    // Start the service
    Serial.println("Starting BLE Service...");
    pService->start();
    Serial.println("BLE Service started");
    
    // Setup advertising
    Serial.println("Setting up BLE Advertising...");
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    // NOTE: Service UUID intentionally NOT added to advertisement packet.
    // The 128-bit UUID (18 bytes) + device name would exceed the 31-byte
    // advertisement limit, causing name truncation ("Su" instead of
    // "SuperSmart-<name>"). The UUID is discoverable via GATT service
    // discovery after connection — the app already knows which UUID to use.
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);

    // Build scan response with manufacturer data (typeCode + MAC) and device name.
    // Scan response has its own 31-byte limit:
    //   Manufacturer data: 2 (header) + 2 (company ID) + 1 (typeCode) + 6 (MAC) = 11 bytes
    //   Device name:       2 (header) + name length bytes
    //   Total must be <= 31 bytes -> name can be up to 18 chars
    //
    // typeCode identifies the product line (app maps code -> name):
    //   1 = "SuperSmart"
    // This avoids wasting BLE bytes on the "SuperSmart-" prefix in the name.
    // The BLE advertised name is just the device name (switchName).
    //
    // MAC uses ESP.getEfuseMac() with LSB-first byte extraction — same byte order
    // as formatMacAddress() in wifisettings.cpp, matching what the server expects.
    {
        uint64_t efuseMac = ESP.getEfuseMac();
        uint8_t wifiMac[6];
        for (int i = 0; i < 6; i++) {
            wifiMac[i] = (uint8_t)((efuseMac >> (8 * i)) & 0xFF);
        }

        std::string mfrData;
        mfrData += (char)0xFF;  // Company ID low byte (0xFFFF = testing/internal)
        mfrData += (char)0xFF;  // Company ID high byte
        mfrData += (char)0x01;  // typeCode: 1 = SuperSmart
        for (int i = 0; i < 6; i++) {
            mfrData += (char)wifiMac[i];
        }

        NimBLEAdvertisementData scanRespData;
        scanRespData.setManufacturerData(mfrData);
        scanRespData.setName(deviceName.c_str());
        pAdvertising->setScanResponseData(scanRespData);

        Serial.print("WiFi MAC in BLE advertisement: ");
        for (int i = 0; i < 6; i++) {
            if (i > 0) Serial.print(":");
            if (wifiMac[i] < 16) Serial.print("0");
            Serial.print(wifiMac[i], HEX);
        }
        Serial.println();
    }

    Serial.println("BLE initialization completed successfully");
}

// Start BLE Provisioning
void startBLEProvisioning() {
    if (bleProvisioningActive) {
        Serial.println("BLE Provisioning already active");
        return;
    }
    
    Serial.println("=== Starting BLE Provisioning ===");
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.print("WiFi status: ");
    Serial.println(WiFi.status());

    // CRITICAL: Set bleProvisioningActive BEFORE stopping WiFi.
    // The MQTT publisher task and checkWifiConnection() run on separate
    // cores/tasks and check this flag to avoid reconnecting WiFi.
    // Without this, there's a race condition: WiFi goes down, MQTT task
    // sees it's disconnected + bleProvisioningActive is still false,
    // and tries to reconnect WiFi — fighting BLE for the shared radio.
    bleProvisioningActive = true;
    bleProvisioningComplete = false;  // Reset completion flag

    // Disconnect MQTT before stopping WiFi to prevent the MQTT task
    // from trying to reconnect during the transition
    if (mqttClient.connected()) {
        mqttClient.disconnect();
        delay(100); // Give MQTT task time to see the disconnect
    }

    // IMPORTANT: Stop WiFi completely to free memory for BLE
    // BLE and WiFi share resources, and without PSRAM we need to free WiFi
    Serial.println("Stopping WiFi to free memory for BLE (NO PSRAM)...");

    // Save current WiFi mode
    wifi_mode_t currentMode = WiFi.getMode();
    Serial.print("Current WiFi mode: ");
    Serial.println(currentMode);

    // Disconnect AP if active
    if (currentMode == WIFI_AP || currentMode == WIFI_AP_STA) {
        WiFi.softAPdisconnect(true);
        delay(50);
    }

    // Disconnect STA if active
    if (currentMode == WIFI_STA || currentMode == WIFI_AP_STA) {
        WiFi.disconnect(true, true); // disconnect and erase credentials from memory
        delay(50);
    }

    // Turn off WiFi completely
    WiFi.mode(WIFI_OFF);
    delay(500); // Give WiFi time to fully shutdown and release radio resources
    
    Serial.print("Free heap after WiFi shutdown: ");
    Serial.println(ESP.getFreeHeap());
    
    // Check if we have enough memory now
    if (ESP.getFreeHeap() < 50000) {
        Serial.println("WARNING: Still low on memory even after stopping WiFi!");
        Serial.print("Free heap: ");
        Serial.println(ESP.getFreeHeap());
    }
    
    // Initialize BLE
    initBLE();
    
    Serial.print("Free heap after BLE init: ");
    Serial.println(ESP.getFreeHeap());
    
    // Start advertising
    Serial.println("Starting BLE Advertising...");
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising) {
        pAdvertising->start();
        Serial.println("BLE Advertising started");
    } else {
        Serial.println("Failed to get advertising object!");
        return;
    }
    
    // Set provisioning state (bleProvisioningActive was already set above
    // before WiFi shutdown to prevent race conditions with MQTT task)
    bleProvisioningStartTime = millis();
    bleProvisioningState = BLE_ADVERTISING;
    
    Serial.println("=== BLE Provisioning Active ===");
    Serial.println("Active for 10 minutes");
    Serial.print("BLE Device Name: ");
    Serial.println(getBLEDeviceName());
    Serial.print("Free heap: ");
    Serial.println(ESP.getFreeHeap());
}

// Stop BLE Provisioning
// restoreWiFi: bring the radio back up afterwards. Only the provisioning-success
// path passes false - it restarts the device a moment later, so reconnecting
// here would just be ~5 s of blocking work thrown away.
void stopBLEProvisioning(bool restoreWiFi) {
    if (!bleProvisioningActive) {
        return;
    }

    Serial.println("Stopping BLE Provisioning...");
    Serial.print("Free heap before stop: ");
    Serial.println(ESP.getFreeHeap());
    
    // Check if BLE is initialized before doing anything
    if (!NimBLEDevice::getInitialized()) {
        Serial.println("BLE not initialized - skipping cleanup");
        bleProvisioningActive = false;
        bleProvisioningState = BLE_IDLE;
        bleProvisioningComplete = false;
        // WiFi was still switched off by startBLEProvisioning() before this
        // point, so it has to come back even when there is no BLE to clean up.
        if (restoreWiFi) {
            restoreWiFiAfterBLE();
        }
        return;
    }
    
    // Stop advertising - with null check
    Serial.println("Stopping advertising...");
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    if (pAdvertising != nullptr) {
        pAdvertising->stop();
        delay(100);
        Serial.println("Advertising stopped");
    }
    
    // Disconnect any connected clients - with extensive null checks
    if (pBLEServer != nullptr) {
        Serial.print("Connected clients: ");
        uint16_t connCount = pBLEServer->getConnectedCount();
        Serial.println(connCount);
        
        if (connCount > 0) {
            Serial.println("Disconnecting clients...");
            try {
                std::vector<uint16_t> clients = pBLEServer->getPeerDevices();
                if (!clients.empty()) {
                    for (auto clientId : clients) {
                        pBLEServer->disconnect(clientId);
                    }
                    delay(200); // Give time for disconnection
                    Serial.println("Clients disconnected");
                }
            } catch (...) {
                Serial.println("Error disconnecting clients - continuing");
            }
        }
    }
    
    // Reset pointers BEFORE deinit to avoid use-after-free
    pBLEServer = nullptr;
    pStatusCharacteristic = nullptr;
    
    // Deinitialize BLE - final step
    Serial.println("Deinitializing BLE...");
    if (NimBLEDevice::getInitialized()) {
        NimBLEDevice::deinit(true);
        delay(300); // Give more time for cleanup
        Serial.println("BLE deinitialized");
    }
    
    // Reset state
    bleProvisioningActive = false;
    bleProvisioningState = BLE_IDLE;
    bleProvisioningComplete = false;
    bleProvisioningErrorCode = "";
    
    Serial.println("BLE Provisioning stopped successfully");
    Serial.print("Free heap after BLE stop: ");
    Serial.println(ESP.getFreeHeap());

    // Radio is free again - put WiFi/AP back the way it was before pairing.
    if (restoreWiFi) {
        restoreWiFiAfterBLE();
    }
}

// Check BLE Timeout and Completion
// This should be called from main loop
void checkBLETimeout() {
    if (!bleProvisioningActive) {
        return;
    }
    
    // Check if provisioning failed with an error (WRONG_PIN, FAILED, etc.)
    // Notification is sent from main loop — NOT from the onWrite callback —
    // so the app has finished processing the BLE write response first.
    if (bleProvisioningErrorCode.length() > 0) {
        // CRITICAL DELAY: Wait for the NimBLE task to finish the onWrite
        // callback and transmit the GATT write response to the app.
        // Without this, the notification can be sent BEFORE the write
        // response (race between Arduino loop task and NimBLE task),
        // causing flutter_blue_plus to drop the notification because
        // the write() future hasn't completed yet.
        delay(500);

        Serial.print("BLE provisioning error - sending error code: ");
        Serial.println(bleProvisioningErrorCode);

        if (pStatusCharacteristic) {
            pStatusCharacteristic->setValue(bleProvisioningErrorCode.c_str());
            pStatusCharacteristic->notify();
            Serial.println("Error notification sent via BLE");
        }

        delay(1000); // Give the app time to receive and process the notification
        bleProvisioningErrorCode = "";  // Reset so we don't resend

        // Don't stop BLE — user may retry with correct PIN
        return;
    }

    // Check if provisioning completed successfully
    if (bleProvisioningComplete) {
        // Same pre-notification delay as error path: wait for the NimBLE
        // task to finish the onWrite callback and transmit the write response.
        delay(500);

        Serial.println("BLE provisioning complete - sending SUCCESS and restarting...");

        // Send SUCCESS notification from the main loop (NOT from the onWrite
        // callback) so the app has finished processing the BLE write response
        // and is ready to receive the notification.
        if (pStatusCharacteristic) {
            pStatusCharacteristic->setValue("SUCCESS");
            pStatusCharacteristic->notify();
            Serial.println("SUCCESS notification sent via BLE");
        }

        delay(2000); // Give the app time to receive the notification

        #if BLE_SKIP_CLEANUP_ON_RESTART
            // Option 1: Skip BLE cleanup - just restart (safer, avoids crashes)
            Serial.println("Skipping BLE cleanup - restarting directly...");
            bleProvisioningActive = false;
            bleProvisioningComplete = false;
        #else
            // Option 2: Properly stop BLE before restart (cleaner but riskier)
            Serial.println("Starting BLE shutdown sequence...");
            stopBLEProvisioning(false); // restarting below - no point reconnecting WiFi
            Serial.println("BLE stopped, waiting before restart...");
            delay(1000);
        #endif

        Serial.println("Restarting device to apply new WiFi settings...");
        Serial.flush(); // Ensure message is sent
        delay(100);

        ESP.restart();
        return;
    }

    // Check for timeout
    unsigned long elapsed = millis() - bleProvisioningStartTime;
    if (elapsed >= BLE_PROVISIONING_TIMEOUT) {
        Serial.println("BLE Provisioning timeout reached (10 minutes)");
        bleProvisioningState = BLE_TIMEOUT;
        stopBLEProvisioning();
    }
}

// The last BLE provisioning failure that reached the debug log. A phone that
// retries a wrong PIN or keeps sending a malformed payload writes the same
// entry over and over, and the debug ring is only ~470 entries deep across 12
// files - a retry loop would push every other diagnostic out of it. So an
// identical failure repeated back-to-back is recorded once. Cleared on a
// successful save, so the same failure occurring again later IS recorded.
static String lastLoggedBleFailure;

// The reason strings passed here are finer-grained than the error codes
// returned to the app (several distinct causes all return "FAILED"), which is
// the point: the log says which one it was without changing the BLE protocol
// the app depends on.
static void logBleProvisioningFailure(const String &reason) {
    if (reason == lastLoggedBleFailure) {
        return; // Same failure back-to-back - already on record.
    }
    lastLoggedBleFailure = reason;
    char msg[DEBUG_MESSAGE_SIZE];
    snprintf(msg, sizeof(msg), "BLE: provisioning failed - %s", reason.c_str());
    logDebugAction(DebugAction::API_SET_WIFI_DETAILS, msg);
}

// Process BLE Provisioning Data
// Returns empty String on success, or an error code string on failure.
// Error codes match what the app expects: WRONG_PIN, FAILED, etc.
String processBLEProvisioningData(const char* jsonData) {
    Serial.println("Processing BLE provisioning data...");

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        logBleProvisioningFailure(String("bad JSON (") + error.c_str() + ")");
        return "FAILED";
    }

    // Extract fields - check if keys exist
    if (!doc["ssid"].is<const char*>() || !doc["password"].is<const char*>() || !doc["pop"].is<const char*>()) {
        Serial.println("Missing required fields in JSON");
        logBleProvisioningFailure("missing ssid/password/pop");
        return "FAILED";
    }

    const char* ssid = doc["ssid"];
    const char* password = doc["password"];
    const char* pop = doc["pop"];

    // Validate inputs
    if (strlen(ssid) == 0) {
        Serial.println("SSID cannot be empty");
        logBleProvisioningFailure("empty SSID");
        return "FAILED";
    }

    if (strlen(pop) != 4) {
        Serial.println("POP must be 4 digits");
        logBleProvisioningFailure("PIN not 4 digits");
        return "WRONG_PIN";
    }

    // Compare POP with device PIN
    if (strcmp(pop, devicePin) != 0) {
        Serial.println("POP verification failed - incorrect PIN");
        Serial.print("Expected: ");
        Serial.println(devicePin);
        Serial.print("Received: ");
        Serial.println(pop);
        // The PIN itself is not logged - the debug log is readable through the
        // log API, and a wrong guess is still a guess at the real one.
        logBleProvisioningFailure("incorrect PIN");
        return "WRONG_PIN";
    }
    
    Serial.println("POP verification successful");
    
    // Save WiFi credentials
    Serial.print("Saving WiFi credentials - SSID: ");
    Serial.println(ssid);
    
    // Update WiFi credentials
    strncpy(ssidBuffer, ssid, sizeof(ssidBuffer) - 1);
    ssidBuffer[sizeof(ssidBuffer) - 1] = '\0';
    
    strncpy(wifiPassBuffer, password, sizeof(wifiPassBuffer) - 1);
    wifiPassBuffer[sizeof(wifiPassBuffer) - 1] = '\0';
    
    // Save to preferences
    isWiFiSet = true;
    saveWifiDetails();
    
    Serial.println("WiFi credentials saved successfully");

    // What this write actually established, for the debug entry at the end -
    // the payload's optional fields mean two successful provisionings can do
    // quite different things.
    bool cloudLinked = false;
    bool nameUpdated = false;
    bool pinUpdated = false;

    // Handle optional accessToken from cloud registration
    if (doc.containsKey("accessToken")) {
        const char* accessToken = doc["accessToken"];
        if (accessToken && strlen(accessToken) > 0) {
            Serial.print("Access token received, saving cloud sync details...");
            String tokenStr = String(accessToken);
            String macStr = WiFi.macAddress();  // deviceId = WiFi MAC
            updateCloudSyncDetails(true, tokenStr, macStr);
            cloudLinked = true;
            Serial.println(" Done");
        }
    }

    // Handle optional switchName change
    if (doc.containsKey("switchName")) {
        const char* newSwitchName = doc["switchName"];
        if (newSwitchName && strlen(newSwitchName) > 0) {
            strncpy(switchName, newSwitchName, sizeof(switchName) - 1);
            switchName[sizeof(switchName) - 1] = '\0';
            isNameSet = true;
            nameUpdated = true;
            Serial.print("Switch name updated to: ");
            Serial.println(switchName);
        }
    }

    // Handle optional switchPin change
    if (doc.containsKey("switchPin")) {
        const char* newSwitchPin = doc["switchPin"];
        if (newSwitchPin && strlen(newSwitchPin) > 0) {
            strncpy(devicePin, newSwitchPin, sizeof(devicePin) - 1);
            devicePin[sizeof(devicePin) - 1] = '\0';
            pinUpdated = true;
            Serial.print("Switch pin updated to: ");
            Serial.println(devicePin);
        }
    }

    // A successful provisioning IS the first-time setup, exactly as completing
    // firstuse.html is - so clear the flag and persist it here, in the same
    // order the web path uses (flag first, then save). It has to be written to
    // NVS before checkBLETimeout() restarts the device: clearing it only in RAM
    // leaves "isFirstTime" true in NVS, and the device boots straight back into
    // firstuse.html even though the name and WiFi were saved.
    // This is also the only saveDeviceDetails() call on this path - the optional
    // switchName/switchPin above just update RAM, so one NVS write covers
    // deviceName, devicePin, isFirstTime and isNameSet together.
    isFirstTime = false;
    saveDeviceDetails();

    // Record the save. The WiFi password, the cloud access token and the new
    // PIN are deliberately absent - the debug log is persisted to flash and
    // readable through the log API, so it must never carry a credential.
    {
        char msg[DEBUG_MESSAGE_SIZE];
        snprintf(msg, sizeof(msg), "BLE: provisioned. ssid=%s cloud=%s name=%s pin=%s", ssidBuffer,
                 cloudLinked ? "linked" : "unchanged", nameUpdated ? switchName : "unchanged",
                 pinUpdated ? "changed" : "unchanged");
        logDebugAction(DebugAction::API_SET_WIFI_DETAILS, msg);
        // A good save re-arms failure logging, so a failure that recurs after
        // this point is recorded rather than suppressed as a duplicate.
        lastLoggedBleFailure = "";
    }

    // Don't set isWifiChanged here - we'll restart the device instead
    // This avoids conflicts with checkWifiConnection() trying to restart AP mode

    return "";  // Empty string = success
}

// Get BLE Device Name
// Returns just the device name (no type prefix). The product type is sent
// as a typeCode byte in manufacturer data instead, saving BLE bytes.
// App maps typeCode 1 -> "SuperSmart".
String getBLEDeviceName() {
    String name = String(switchName);

    if (name.length() == 0 || name == "SwiGOss") {
        // // Fallback: use "SwiGOss-XXXX" with last 4 hex chars of MAC for uniqueness
        // uint64_t chipid = ESP.getEfuseMac();
        // uint16_t chip = (uint16_t)(chipid >> 32);
        // char macSuffix[5];
        // snprintf(macSuffix, sizeof(macSuffix), "%04X", chip);
        // name = "SwiGOss-" + String(macSuffix);
        name = "SwiGOss";
    }

    return name;
}
