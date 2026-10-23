#include <WebServer_ESP32_SC_W5500.h>
#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

#include "wifisettings.h"
#include "buttonpress.h"
#include "logdebug.h"
#include <esp_task_wdt.h>

static void bringUpWiFiInterfaces();

// Defined in api.cpp. Declared locally rather than by including api.h, which
// includes this module's own header.
bool isFirmwareUploadActive();

char ssidBuffer[21];
char wifiPassBuffer[21];
char apPassBuffer[21];
char switchName[21];
char devicePin[9];
IPAddress staticIP;
IPAddress gateway;
IPAddress subnet;
IPAddress primaryDNS;
IPAddress secondaryDNS;
int16_t deviceTZOffset = 0;

volatile char wiFiMode = '0';

unsigned long switchTimeAP = 0;
IPAddress lastIP;
unsigned long lastMdnsRestart = 0;
unsigned long lastAsyncWebserverRestart = 0;
volatile bool parallelAPModeEnabled = false;  // Track if parallel AP mode is active
unsigned long parallelAPDisconnectCountdown = 0;  // When parallel AP mode was enabled
// millis() of a pending /api/removeWiFi request (0 = none); drained in core0Task
// after a short grace period. disableWiFi() re-configures the radio and mDNS,
// which is too heavy for the AsyncTCP task's stack, and the HTTP response has to
// reach the browser before the STA link is torn down.
volatile unsigned long removeWiFiRequestedAt = 0;

// Counts completed STA reconnect trials (each = 2 steps of switchToWifi).
// Reset on successful connect; logged once when it crosses 6 (~3 min outage).
static uint8_t wiFiConnectAttempt = 0;
static bool wiFiOutageReported = false;

SPIClass spi;
ESP32_W5500 eth;
bool ethernetEnabled = false;
volatile bool ethernetConnected = false;
bool ethernetDisconnected = false;
bool wifiEventHandlerRegistered = false;
uint32_t mdnsAnnounceRevision = 0;

static void addEviqtechServiceTxtRecords(const String &ipStr, const String &softApIpStr = "") {
    mdnsAnnounceRevision++;
    String deviceId = WiFi.macAddress();
    String revStr = String(mdnsAnnounceRevision);
    String groupedTxt = "rev=" + revStr + ",ip=" + ipStr + ",mfr=EVIQTech,id=" + deviceId;
    if (softApIpStr.length() > 0) {
        groupedTxt += ",apip=" + softApIpStr;
    }

    // Single grouped TXT record prevents Android NSD from mixing keys across services.
    // All fields are atomic inside one value — no cross-contamination possible.
    MDNS.addServiceTxt("eviqtech", "tcp", "meta", groupedTxt.c_str());
}

static void restartMdnsForCurrentMode(const char *reason) {
    wifi_mode_t mode = WiFi.getMode();
    bool staConnected = (WiFi.status() == WL_CONNECTED);

    if (mode == WIFI_MODE_NULL || (mode == WIFI_STA && !staConnected)) {
        return;
    }

    MDNS.end();
    delay(5);
    if (!MDNS.begin(getSwitchName())) {
        Serial.printf("Error setting up mDNS responder (%s)\n", reason);
        return;
    }

    MDNS.addService("eviqtech", "tcp", 80);
    String ipStr;
    String softApIpStr;

    if (mode == WIFI_AP) {
        ipStr = WiFi.softAPIP().toString();
    } else if (mode == WIFI_AP_STA) {
        ipStr = staConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        softApIpStr = WiFi.softAPIP().toString();
    } else {
        ipStr = WiFi.localIP().toString();
    }

    addEviqtechServiceTxtRecords(ipStr, softApIpStr);
    lastMdnsRestart = millis();
    // Serial.printf("mDNS announced (%s): %s\n", reason, ipStr.c_str());
}

// WiFi events fire on the WiFi stack task whose stack is small (~3 KB).
// Doing String allocations + MDNS.end()/begin() here has overflowed the stack
// in field reports, so the heavy work is deferred via these flags and run from
// serviceWiFiEventDeferred() on a task with a real stack.
// resetMqttBackoff() is just two integer writes — safe to call inline.
static volatile bool mdnsRestartPending = false;
static volatile bool mdnsEndPending     = false;

static void handleWiFiEvent(WiFiEvent_t event) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        mdnsRestartPending = true;
        resetMqttBackoff();
        break;
    case ARDUINO_EVENT_WIFI_AP_START:
    case ARDUINO_EVENT_WIFI_AP_STOP:
        mdnsRestartPending = true;
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        mdnsEndPending = true;
        break;
    default:
        break;
    }
#else
    switch (event) {
    case SYSTEM_EVENT_STA_GOT_IP:
        mdnsRestartPending = true;
        resetMqttBackoff();
        break;
    case SYSTEM_EVENT_AP_START:
    case SYSTEM_EVENT_AP_STOP:
        mdnsRestartPending = true;
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        mdnsEndPending = true;
        break;
    default:
        break;
    }
#endif
}

void serviceWiFiEventDeferred() {
    if (mdnsRestartPending) {
        mdnsRestartPending = false;
        mdnsEndPending     = false;  // restart implicitly tears down first
        restartMdnsForCurrentMode("WiFi event (deferred)");
    } else if (mdnsEndPending) {
        mdnsEndPending = false;
        MDNS.end();
    }
}

static void ensureWiFiEventHandlerRegistered() {
    if (!wifiEventHandlerRegistered) {
        WiFi.onEvent(handleWiFiEvent);
        wifiEventHandlerRegistered = true;
    }
}

char *getSwitchName() {
    if (strlen(switchName) > 0) {
        return switchName;
    }
    // Serial.println(" else dns_buffer - so swigo 3");
    return (char *)DEFAULT_DEVICE_NAME;
}

char getWiFiMode() {
    return wiFiMode;
}

void startAPMode() {
    char *deviceName = getSwitchName();
    // Serial.println("Starting AP mode...");

    // Keep the radio on the channel the STA was using so the VSMR ESP-NOW link
    // is undisturbed (ESP-NOW only hears frames on the parked channel; moving
    // to the softAP default would force the VSMR through a hop-and-probe
    // re-lock). 0 = not associated -> fall back to the softAP default (1).
    uint8_t apChannel = WiFi.channel();
    WiFi.disconnect();
    // AP_STA instead of AP: the idle STA interface transmits nothing, but keeps
    // frames unicast to the STA MAC (VSMR ESP-NOW link) received/ACKed, and
    // lets the router scan run without a mode flip.
    WiFi.mode(WIFI_AP_STA);
    String apName = String(deviceName) + "_AP";

    // Set the IP address for the AP
    IPAddress ip(192, 168, 4, 1); // Choose your desired IP address for the AP
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(ip, ip, subnet);
    if (isAPPassSet && (strlen(apPassBuffer) >= 8)) {
        WiFi.softAP(apName.c_str(), apPassBuffer, apChannel ? apChannel : 1);
    } else {
        WiFi.softAP(apName.c_str(), NULL, apChannel ? apChannel : 1);
    }

    // if (result){
        // Serial.print("AP IP address: ");
        // Serial.println(WiFi.softAPIP());
        // Serial.printf("AP started successfully\n");
    // }
    if (ethernetDisconnected) {
        stopWebServer();
        Serial.println("restarting webserver");
        delay(10);
        startWebServer();
        setupMQTT();
        ethernetDisconnected = false;
    }
    // Disable power-saving features
    esp_wifi_set_ps(WIFI_PS_NONE);
    wiFiMode = 'A';
    // //if (!MDNS.begin(deviceName)) { // Set mDNS domain name
    //   Serial.println("Error setting up MDNS responder!");
    // } else {
    //   Serial.println("mDNS responder started in AP mode!");
    // }
    MDNS.end();
    if (MDNS.begin(deviceName)) {
        MDNS.addService("eviqtech", "tcp", 80); // Advertise HTTP service
        addEviqtechServiceTxtRecords(WiFi.softAPIP().toString());
        Serial.println("mDNS responder started in AP mode!");
    } else {
        Serial.println("Error setting up MDNS responder!");
    }

    switchTimeAP = millis();
    // free(deviceName);
}

void startParallelAPMode() {
    char *deviceName = getSwitchName();
    // Serial.println("Starting Parallel AP mode...");

    String apName = String(deviceName) + "_AP";

    // Set the IP address for the AP
    IPAddress ip(192, 168, 4, 1); // Choose your desired IP address for the AP
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(ip, ip, subnet);
    bool result;
    if (isAPPassSet && (strlen(apPassBuffer) >= 8)) {
        result = WiFi.softAP(apName.c_str(), apPassBuffer);
    } else {
        result = WiFi.softAP(apName.c_str());
    }

    // Disable power-saving features
    esp_wifi_set_ps(WIFI_PS_NONE);
    if (result) {
        // Serial.print("Parallel AP started: ");
        // Serial.println(WiFi.softAPIP());
        parallelAPModeEnabled = true;
        parallelAPDisconnectCountdown = millis();
    } else {
        Serial.println("Failed to start parallel AP mode");
        // Revert to WiFi-only mode
        WiFi.mode(WIFI_STA);
    }
    
}
// #ifdef LED_RGB_FLOATRELAY
void reconnectToEthernet(bool serverRestart = false) {
    int retries = 3;
    while (retries > 0 && !ethernetConnected) {
        Serial.println("Ethernet begin attempt " + String(4 - retries));
        esp_task_wdt_reset();
        if (staticIP != IPAddress(0, 0, 0, 0) && gateway != IPAddress(0, 0, 0, 0) && subnet != IPAddress(0, 0, 0, 0)) {
            eth.config(staticIP, gateway, subnet);
        }
        uint8_t ethernetMacId[6];
        generateDynamicMac(ethernetMacId, 'E'); // wifi address+ethernet offset

        if (eth.begin(SPI_MISO, SPI_MOSI, SPI_SCLK, W5500_CS, W5500_INT, 14, SPI2_HOST, ethernetMacId)) {
            // Wait for valid IP

            unsigned long startTime = millis();
            while (eth.localIP() == IPAddress(0, 0, 0, 0) && millis() - startTime < 5000) {
                delay(500);
                Serial.print(".");
            }
            if (eth.localIP() != IPAddress(0, 0, 0, 0)) {
                Serial.print("\nEthernet connected. IP: ");
                Serial.println(eth.localIP());
                ethernetConnected = true;
                // Initialize mDNS
                delay(1000); // Wait for network stability
                if (serverRestart) {
                    esp_task_wdt_reset();
                    server.end();
                    server.begin();
                    Serial.println("Server restarted");
                }

                if (MDNS.begin(getSwitchName())) { // Set mDNS domain name
                    MDNS.addService("eviqtech", "tcp", 80);
                    addEviqtechServiceTxtRecords(eth.localIP().toString());
                    Serial.println("mDNS responder started in Ethernet mode!");
                } else {
                    Serial.println("Error setting up mDNS responder in Ethernet mode!");
                }
                resetMqttBackoff(); // Retry MQTT now on link recovery

            } else {
                Serial.println("\nDHCP failed to assign IP");
                // eth.
                // eth.end(); // Clean up
            }
        } else {
            Serial.println("Ethernet initialization failed");
        }
        retries--;
        if (!ethernetConnected && retries > 0) {
            Serial.println("Retrying Ethernet...");
            delay(2000);
        }
    }
}
// #endif
void initiateWifiSettings() {
    ensureWiFiEventHandlerRegistered();

    if (isNameSet) {
        // Serial.println("Initiate name - inside if isNameSet");
        // strcpy(switchName, readStringFromEEPROM(DEVICE_NAME_ADDRESS, 20));
        // deviceTZOffset = readInt16FromEEPROM(TIMEZONE_OFFSET);
    }
    Serial.println("Initiate Wifi settings");
    // #ifdef LED_RGB_FLOATRELAY
    if (ethernetEnabled) {
        Serial.println("Ethernet enabled");

        pinMode(W5500_RST, OUTPUT);
        digitalWrite(W5500_RST, LOW);
        delayMicroseconds(500); // 500µs low
        digitalWrite(W5500_RST, HIGH);
        delayMicroseconds(1000); // Wait 1ms
        Serial.println("W5500 reset complete");

        // Initialize Ethernet
        WiFi.mode(WIFI_OFF); // Disable WiFi
        Serial.println("Attempting Ethernet connection...");
        reconnectToEthernet();
    }
    // #endif
    bringUpWiFiInterfaces();
}

// The WiFi half of the boot bring-up: join the configured SSID with the softAP
// alongside as a local fallback, and drop to AP-only when that fails. Factored
// out of initiateWifiSettings() (Ethernet init stays there) so BLE provisioning
// can re-run exactly this sequence when it hands the radio back —
// see restoreWiFiAfterBLE().
static void bringUpWiFiInterfaces() {
    if (isWiFiSet && !ethernetConnected) {
        // Serial.println("inside initiateWifiSettings - isWiFiSet");

        // strcpy(ssidBuffer, readStringFromEEPROM(WIFI_SSID_ADDRESS, 20));
        // strcpy(wifiPassBuffer, readStringFromEEPROM(WIFI_PASSWORD_ADDRESS, 20));
        // ssidBuffer = readStringFromEEPROM(WIFI_SSID_ADDRESS, 20);
        // During startup startup mode is wifi

        if (strlen(ssidBuffer) > 0) {
            // int n = WiFi.scanNetworks();
            // Serial.print("scanNetworks count :");
            // Serial.println(n);

            // for (int i = 0; i < n; ++i) {
            //   Serial.println(WiFi.SSID(i).c_str());
            //   if (strcmp(WiFi.SSID(i).c_str(), ssidBuffer) == 0) {

            WiFi.mode(WIFI_AP_STA);
            // Set AP name immediately to avoid default ESP_xxxxxx showing
            String apName = String(getSwitchName()) + "_AP";
            if (isAPPassSet && (strlen(apPassBuffer) >= 8)) {
                WiFi.softAP(apName.c_str(), apPassBuffer);
            } else {
                WiFi.softAP(apName.c_str());
            }
            // Serial.printf("wifi connection on Core %d\n", xPortGetCoreID());
            if (staticIP != IPAddress(0, 0, 0, 0) && gateway != IPAddress(0, 0, 0, 0)) {
                // Use default subnet if not provided
                IPAddress subnetToUse = (subnet != IPAddress(0, 0, 0, 0)) ? subnet : IPAddress(255, 255, 255, 0);
                // Use default DNS if not provided
                IPAddress primDNSToUse = (primaryDNS != IPAddress(0, 0, 0, 0)) ? primaryDNS : IPAddress(8, 8, 8, 8);
                IPAddress secondDNSToUse = (secondaryDNS != IPAddress(0, 0, 0, 0)) ? secondaryDNS : IPAddress(1, 1, 1, 1);
                
                WiFi.config(staticIP, gateway, subnetToUse, primDNSToUse, secondDNSToUse);
                Serial.println("Static IP set");
                Serial.println(staticIP);
                Serial.println(gateway);
                Serial.println(subnetToUse);
            }
            WiFi.begin(ssidBuffer, wifiPassBuffer);
            uint8_t wifi_trial = MAX_WIFI_TRIALS;
            delay(100);
            while (WiFi.status() != WL_CONNECTED && wifi_trial > 0) {
                delay(500);
                wifi_trial--;
                esp_task_wdt_reset(); // also reached at runtime via restoreWiFiAfterBLE()
            }

            if (WiFi.status() == WL_CONNECTED) {
                wiFiMode = 'W';
                lastIP = WiFi.localIP();

                if (MDNS.begin(getSwitchName())) {
                    MDNS.addService("eviqtech", "tcp", 80); // Advertise HTTP service
                    String ipStr = WiFi.localIP().toString();
                    String softApIpStr = (WiFi.getMode() == WIFI_AP_STA) ? WiFi.softAPIP().toString() : "";
                    addEviqtechServiceTxtRecords(ipStr, softApIpStr);
                    Serial.println("mDNS responder started in WIFI mode!");
                } else {
                    Serial.println("Error setting up MDNS responder!");
                }
                resetMqttBackoff(); // Retry MQTT now on link recovery
                // AP is already running (WIFI_AP_STA) — track it with parallelAPModeEnabled
                // so the 5-minute auto-disable timeout works
                parallelAPModeEnabled = true;
                parallelAPDisconnectCountdown = millis();
            } else {
                wiFiMode = '-';
                delay(100);
            }

            // break;
            //   }
            // }
        }
    }

    if (WiFi.status() != WL_CONNECTED && !ethernetConnected) {
        Serial.println("Before calling startAPMode()");
        WiFi.mode(WIFI_OFF);
        delay(500);
        
        // Force complete WiFi stack restart
        // esp_wifi_stop();
        // esp_wifi_deinit();
        // delay(500);
        
        // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        // esp_wifi_init(&cfg);
        // esp_wifi_start();
        startAPMode();
    }
    // Setting high transmission power
    // To do - change it with configuration
    // WiFi.setTxPower(WIFI_POWER_18_5dBm);
}

// Bring the radio back after a BLE session gave it up.
// startBLEProvisioning() does softAPdisconnect() + disconnect() + WIFI_OFF to
// free the radio and the heap NimBLE needs, and nothing in the normal
// checkWifiConnection() path can recover from that on its own: it only calls
// startAPMode() when wiFiMode=='0', and the mode it was left in ('A' or 'W')
// routes to branches that never re-create the softAP. So the device stayed
// unreachable - no AP, no STA - until a power cycle.
void restoreWiFiAfterBLE() {
    if (ethernetConnected) {
        // The Ethernet link was never touched by BLE; nothing to restore.
        return;
    }
    Serial.println("Restoring WiFi/AP after BLE session...");
    esp_task_wdt_reset();

    // The softAP was torn down, so the "parallel AP is already up" bookkeeping
    // is stale - clear it or the reconnect paths skip bringing the AP back.
    parallelAPModeEnabled = false;
    parallelAPDisconnectCountdown = 0;
    wiFiMode = '0';

    bringUpWiFiInterfaces(); // sets wiFiMode to 'W' or 'A' on the way out
    esp_task_wdt_reset();
    Serial.print("WiFi restored - mode: ");
    Serial.println(wiFiMode);
}

void switchToWifi() {
    // Serial.printf("switchToWifi on Core %d\n", xPortGetCoreID());

    static uint8_t wiFiTrialState = 0;
    static char previousMode = '0';
    if (strlen(ssidBuffer) > 0 && wiFiTrialState == 0) {
        // Serial.println("Inside switchToWifi() - wiFiTrialState==0");
        // int n = WiFi.scanNetworks();
        previousMode = wiFiMode;
        // for (int i = 0; i < n; ++i) {
        //   if (strcmp(WiFi.SSID(i).c_str(), ssidBuffer) == 0) {
        if (wiFiMode == 'A') {
            WiFi.mode(WIFI_AP_STA);
        } else if (wiFiMode == 'W' || ethernetDisconnected) {
            WiFi.disconnect();
            // WiFi.mode(WIFI_STA);
            WiFi.mode(WIFI_AP_STA);
            
        }
        // WiFi.mode(WIFI_AP_STA);
        if (staticIP != IPAddress(0, 0, 0, 0) && gateway != IPAddress(0, 0, 0, 0)) {
            // Use default subnet if not provided
            IPAddress subnetToUse = (subnet != IPAddress(0, 0, 0, 0)) ? subnet : IPAddress(255, 255, 255, 0);
            // Use default DNS if not provided
            IPAddress primDNSToUse = (primaryDNS != IPAddress(0, 0, 0, 0)) ? primaryDNS : IPAddress(8, 8, 8, 8);
            IPAddress secondDNSToUse = (secondaryDNS != IPAddress(0, 0, 0, 0)) ? secondaryDNS :  IPAddress(1, 1, 1, 1);
            
            WiFi.config(staticIP, gateway, subnetToUse, primDNSToUse, secondDNSToUse);
            // Serial.println("Static IP set");
            // Serial.println(staticIP);
            // Serial.println(gateway);
            // Serial.println(subnetToUse);
        }
        WiFi.begin(ssidBuffer, wifiPassBuffer);
        // Serial.println("Inside switchToWifi() - Connecting to WiFi...");

        wiFiTrialState = 1;
        wiFiMode = 'B';
        // i++;
        return;
        // }
        // }
        // No networks found with given name. So skip checking for some time
        // switchTimeAP=millis();
    }

    if (wiFiTrialState == 1) {
        Serial.println("Inside switchToWifi() - wiFiTrialState==1");

        if (WiFi.status() == WL_CONNECTED) {
            //WiFi.softAPdisconnect(true);
            wiFiMode = 'W';
            lastIP = WiFi.localIP();
            // if (!MDNS.begin(getSwitchName())) {  // Set mDNS domain name
            //   Serial.println("Error setting up MDNS responder!");
            // } else {
            //   Serial.println("mDNS responder started in WIFI mode!");
            // }
            MDNS.end();
            if (MDNS.begin(getSwitchName())) {
                MDNS.addService("eviqtech", "tcp", 80); // Advertise HTTP service
                String ipStr = WiFi.localIP().toString();
                String softApIpStr = (WiFi.getMode() == WIFI_AP_STA) ? WiFi.softAPIP().toString() : "";
                addEviqtechServiceTxtRecords(ipStr, softApIpStr);
                Serial.println("mDNS responder started in WIFI mode!");
            } else {
                Serial.println("Error setting up MDNS responder!");
            }
            wiFiTrialState = 0;
            // AP is already running (WIFI_AP_STA) — track it for 5-min auto-disable
            parallelAPModeEnabled = true;
            parallelAPDisconnectCountdown = millis();
            isWifiChanged = false;
            wiFiConnectAttempt = 0;
            wiFiOutageReported = false;
            // {
            //     char dbgBuf[200];
            //     snprintf(dbgBuf, sizeof(dbgBuf),
            //              "WiFi connected. ssid=%s RSSI=%d ip=%s",
            //              ssidBuffer, WiFi.RSSI(), lastIP.toString().c_str());
            //     logDebugAction(DebugAction::CONNECTIVITY_SUCCESS, dbgBuf);
            // }
            if (ethernetDisconnected) {
                stopWebServer();
                Serial.println("restarting webserver");
                delay(10);
                startWebServer();

                setupMQTT();
                ethernetDisconnected = false;
            }
            countMQTTMessagesAfterRelayOff = 0;
            resetMqttBackoff(); // Retry MQTT now on link recovery

            // When WIFI is
            return;
        }

        else {
            Serial.println("Inside switchToWifi() - WiFi not connected");
            if (previousMode == 'A') {
                wiFiMode = 'A';
                // AP_STA (not AP): keep the idle STA interface up for the VSMR
                // ESP-NOW link and the periodic router scan.
                WiFi.mode(WIFI_AP_STA);
                switchTimeAP = millis();
            } else {
                startAPMode();
            }
            wiFiTrialState = 0;
            isWifiChanged = false;
        }
    }
}
// #ifdef LED_RGB_FLOATRELAY
void checkEthernetConnection() {
    unsigned long ethernetCheckDuration = (unsigned long)AP_MODE_MIN_TIME * 30000;
    static unsigned long lastEthernetCheck = 0;
    Serial.println("Ethernet checking.");

    static uint8_t reconnectAttempts = 0;
    if (ethernetEnabled && ethernetConnected) {
        Serial.println("Ethernet checking inside.");

        unsigned long startTime = millis();
        eth.linkUp();
        if (eth.localIP() == IPAddress(0, 0, 0, 0)) {
            Serial.println("Ethernet checking problem case 222");
            reconnectAttempts++;
            if (lastEthernetCheck == 0)
                lastEthernetCheck = millis();
        } else {
            if (reconnectAttempts > 1) {
                Serial.println("Ethernet re-connected 333");
                reconnectAttempts = 0;
                lastEthernetCheck = 0;
            }
        }
        // Serial.println(millis());

        Serial.println(lastEthernetCheck);
        if (lastEthernetCheck > 0 && millis() - lastEthernetCheck > 60000) {
            ethernetDisconnected = true;
            Serial.println("Ethernet failed to reconnect 444");

            ethernetConnected = false;
            lastEthernetCheck = 0;
            reconnectAttempts = 0;
        }
    }
    if (ethernetConnected && (reconnectAttempts == 0)) {
        if ((millis() - lastMdnsRestart > 60000)) { // Every 60 seconds
            MDNS.end();
            delay(5);
            if (MDNS.begin(getSwitchName())) {
                MDNS.addService("eviqtech", "tcp", 80);
                addEviqtechServiceTxtRecords(eth.localIP().toString());
            }
            lastMdnsRestart = millis();
        }
    }
    // Will need to finetune this
    // Also need to reconnect to mqtt

    if ((ethernetConnected) && (millis() - lastAsyncWebserverRestart > 60000)) {
        // Serial.println("webserver restart 66666");
        // Serial.print("Free Heap: ");
        // Serial.println(ESP.getFreeHeap());
        esp_task_wdt_reset();
        stopWebServer();
        Serial.println("restarting server");
        delay(10);
        startWebServer();
        lastAsyncWebserverRestart = millis();
        esp_task_wdt_reset();
    }
}
// #endif

void enableParallelAPMode() {
    // Enable AP mode while maintaining WiFi connection (AP+STA mode)
    if (wiFiMode == 'W' && WiFi.status() == WL_CONNECTED && !parallelAPModeEnabled) {
        // Serial.println("Enabling parallel AP mode...");
       
        WiFi.mode(WIFI_AP_STA);
        startParallelAPMode();
        restartMdnsForCurrentMode("Enable parallel AP");
       
    }
}

void disableParallelAPMode() {
    // Disable AP mode and return to WiFi-only mode
    if (parallelAPModeEnabled) {
        // Serial.println("Disabling parallel AP mode...");
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        parallelAPModeEnabled = false;
        parallelAPDisconnectCountdown = 0;
        wiFiMode = 'W';  // Return to WiFi-only mode
        restartMdnsForCurrentMode("Disable parallel AP");
        Serial.println("Parallel AP mode disabled, returned to WiFi-only mode");
    }
}

void checkWifiConnection() {
    // Once AP mode is enabled, it will wait for a while - based on AP_MODE_MIN_TIME
    // After AP_MODE_MIN_TIME it will check whether WIFI could be connected.
    // If will check whether the given wifi is available, if so it will try to connect. Once initiated the connection
    // In the next iteration, (iteration based on WIFI_CHECK_INTERVAL),
    //      if wifi is connected, it will schange mode or it will reset switchTimeAP, else it will reset  switchTimeAP

    // BLE provisioning owns the radio: startBLEProvisioning() switched WiFi off
    // deliberately to free the antenna and the heap NimBLE needs, and
    // restoreWiFiAfterBLE() brings it back when the session ends. Nothing in
    // here may touch WiFi before then.
    //
    // This guard has to be the FIRST thing in the function, not inside one of
    // the branches below. It used to sit in the (wiFiMode=='W' && !connected)
    // branch, which is reached only when the device was connected at the moment
    // the pairing combo was pressed. A device that was configured but NOT
    // connected sits in wiFiMode 'A'/'B'/'0' instead - and those branches
    // (switchToWifi() -> WiFi.begin(), the async router scan ->
    // WiFi.mode(WIFI_AP_STA), startAPMode()) were re-initialising the WiFi stack
    // 15-60 s into every BLE session. On the single shared antenna - and with
    // startAPMode() disabling modem sleep via esp_wifi_set_ps(WIFI_PS_NONE) -
    // that killed the BLE link, which is exactly the case where re-pairing is
    // needed. Returning here also keeps wiFiConnectAttempt from ticking up and
    // logging a bogus "configured but not connected" outage during pairing.
    if (bleProvisioningActive) {
        Serial.println("BLE provisioning active - skipping WiFi reconnect");
        return;
    }

    unsigned long apWaitDuration = (unsigned long)AP_MODE_MIN_TIME * 60000;

    // Tick-based outage log: at ~15s per tick, 12 ticks ≈ 3 min.
    // Fires once per outage; reset on successful connect (in branches below).
    bool wifiConnectedNow = (wiFiMode == 'W' && WiFi.status() == WL_CONNECTED);
    if (isWiFiSet && !wifiConnectedNow) {
        if (wiFiConnectAttempt < 255) wiFiConnectAttempt++;
        if (wiFiConnectAttempt == 12 && !wiFiOutageReported) {
            wiFiOutageReported = true;
            char dbgBuf[200];
            snprintf(dbgBuf, sizeof(dbgBuf),
                     "WiFi configured but not connected for ~3 min. mode=%c status=%d ticks=%u ssid=%s",
                     wiFiMode, (int)WiFi.status(), (unsigned)wiFiConnectAttempt, ssidBuffer);
            Serial.print("[WiFi] "); Serial.println(dbgBuf);
            logDebugAction(DebugAction::CONNECTIVITY_FAILURE, dbgBuf);
        }
    }

    // Check parallel AP mode timeout (5 minutes of no connections).
    // Suppressed while WiFi isn't connected: the parallel AP is the only
    // local fallback for the user, so keep it up until WiFi recovers.
    if (parallelAPModeEnabled && wifiConnectedNow) {
        if (WiFi.softAPgetStationNum() > 0) {
            // Device connected — reset timeout
            // Serial.println(WiFi.softAPgetStationNum());
            parallelAPDisconnectCountdown = 0;
        } else {
            // No connections — start timer if not already running
            if (parallelAPDisconnectCountdown == 0) {
                parallelAPDisconnectCountdown = millis();
                Serial.println("No devices on parallel AP, starting 5 min timeout");
            }
            // 5 minutes with no connection — disable AP
            if ((millis() - parallelAPDisconnectCountdown) > 300000) {
                Serial.println("Parallel AP timeout - disabling");
                disableParallelAPMode();
            } else {
            // Serial.println(millis() - parallelAPDisconnectCountdown);

            }
        }
    }
    
    // Async scan-based gate: while idle in AP mode, scan once per
    // apWaitDuration and only start a STA trial when the configured SSID is
    // actually visible. mid-trial (wiFiMode='B') or user-driven changes
    // (isWifiChanged / ethernetDisconnected) bypass the scan and fire
    // switchToWifi() immediately.
    static bool wifiScanInProgress = false;

    if (isWiFiSet && (isWifiChanged || ethernetDisconnected || wiFiMode == 'B')) {
        if (wifiScanInProgress) {
            WiFi.scanDelete();
            wifiScanInProgress = false;
        }
        switchToWifi();
    } else if (isWiFiSet && (wiFiMode == '0' || wiFiMode == 'A')) {
        if (wifiScanInProgress) {
            int n = WiFi.scanComplete();
            if (n != WIFI_SCAN_RUNNING) {
                // Done (n>=0) or failed (n==WIFI_SCAN_FAILED).
                wifiScanInProgress = false;
                bool ssidFound = false;
                for (int i = 0; i < n; i++) {
                    if (strcmp(WiFi.SSID(i).c_str(), ssidBuffer) == 0) {
                        ssidFound = true;
                        break;
                    }
                }
                WiFi.scanDelete();
                if (ssidFound) {
                    Serial.printf("[WiFi] SSID '%s' visible — starting trial\n", ssidBuffer);
                    switchToWifi();
                } else {
                    // SSID absent (or scan failed); back off before next scan.
                    switchTimeAP = millis();
                }
            }
            // else scan still running — wait for next tick
        } else if ((millis() - switchTimeAP) > apWaitDuration) {
            // STA interface needs to be up to scan; flip to AP+STA if needed.
            // AP stays advertised; we just enable STA alongside.
            if (WiFi.getMode() != WIFI_AP_STA) {
                WiFi.mode(WIFI_AP_STA);
            }
            WiFi.scanNetworks(true /*async*/);
            wifiScanInProgress = true;
        }
    } else if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != lastIP) {
        Serial.println("in WIFI IP changed - restart mDNS");
        restartMdnsForCurrentMode("WIFI IP changed");
        lastIP = WiFi.localIP();
        wiFiConnectAttempt = 0;
        wiFiOutageReported = false;
        resetMqttBackoff();
    } else if (WiFi.status() == WL_CONNECTED
        && WiFi.getMode() == WIFI_STA
        && (millis() - lastMdnsRestart > 300000)) { // Every 5 minutes, pure STA mode only
        MDNS.end();
        delay(5);
        if (MDNS.begin(getSwitchName())) {
            MDNS.addService("eviqtech", "tcp", 80);
            addEviqtechServiceTxtRecords(WiFi.localIP().toString());
            // Serial.println("mDNS re-announced in STA mode");
        }
        lastMdnsRestart = millis();
        wiFiConnectAttempt = 0;
        wiFiOutageReported = false;

    } else if ((wiFiMode == 'W') && WiFi.status() != WL_CONNECTED) {
        // No BLE check here any more - the whole function returns at the top
        // while a BLE session is running.

        // Bring up parallel AP on the first detected disconnect (≈15s after loss)
        // so the user has a local fallback. Then hand control back to the
        // outer AP-mode gate by setting wiFiMode='A'+switchTimeAP — that loop
        // already cycles switchToWifi() every apWaitDuration to keep STA trying.
        Serial.println("[WiFi] link down - starting parallel AP, handing off to AP-mode retry loop");
        // Stop STA scanning before flipping mode so the AP interface comes up
        // cleanly without channel hopping racing it.
        WiFi.disconnect(false);
        delay(50);
        WiFi.mode(WIFI_AP_STA);
        if (!parallelAPModeEnabled) {
            startParallelAPMode();
        }
        restartMdnsForCurrentMode("WiFi down - parallel AP up");
        wiFiMode = 'A';
        switchTimeAP = millis();
    } else if (wiFiMode == '0') {
        startAPMode();
        wiFiConnectAttempt = 0;

    } else if (wiFiMode == 'W') {
        wiFiConnectAttempt = 0;
    }

    // Re-announce mDNS in AP or AP+STA mode every 60 seconds.
    // In AP+STA mode, MDNS.begin() binds to BOTH interfaces automatically,
    // so the device is discoverable from both the AP and STA networks.
    // The WiFi STA block above only handles pure STA mode (WIFI_STA).
    //
    // Held off during a firmware upload: this tears mDNS down and brings it
    // back up (MDNS.end(), then MDNS.begin() plus multicast announcements) in
    // the middle of a ~1.8 MB transfer, and an upload that is interrupted is
    // far more costly than being briefly undiscoverable. A successful upload
    // reboots and re-announces anyway; a failed one is covered by the next
    // 60 s tick. Only this periodic re-announce is suppressed - the
    // transition-driven ones above (STA connect, and restartMdnsForCurrentMode()
    // when the link drops) still run, since those reflect a real state change.
    if ((WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
        && !isFirmwareUploadActive()
        && (millis() - lastMdnsRestart > 60000)) {
        MDNS.end();
        delay(5);
        if (MDNS.begin(getSwitchName())) {
            MDNS.addService("eviqtech", "tcp", 80);
            // In AP+STA mode, advertise BOTH IPs so the Flutter app can pick
            // the correct one based on which network the phone is connected to.
            // In AP-only mode, advertise the AP IP as the primary IPv4.
            String ipStr;
            if (WiFi.getMode() == WIFI_AP_STA && WiFi.status() == WL_CONNECTED) {
                ipStr = WiFi.localIP().toString();
                // Also advertise the AP IP so phones on the AP network can reach the device
                String softApIpStr = WiFi.softAPIP().toString();
                addEviqtechServiceTxtRecords(ipStr, softApIpStr);
            } else {
                ipStr = WiFi.softAPIP().toString();
                addEviqtechServiceTxtRecords(ipStr);
            }
            Serial.println("advertising MDNS in AP || WIFI_AP_STA mode");
        }
        lastMdnsRestart = millis();
    }
}

void disableWiFi() {
    if (isWiFiSet) {
        // Clear all WiFi-related fields
        ssidBuffer[0] = '\0';
        wifiPassBuffer[0] = '\0';
        staticIP = IPAddress(0, 0, 0, 0);
        gateway = IPAddress(0, 0, 0, 0);
        subnet = IPAddress(0, 0, 0, 0);
        primaryDNS = IPAddress(0, 0, 0, 0);
        secondaryDNS = IPAddress(0, 0, 0, 0);
        
        // Save cleared WiFi settings
        isWiFiSet = false;
        saveWifiDetails();

        // The other radio restart reachable from the same loop that runs
        // checkWifiConnection(): the Button-4 + OFF combo and the web portal's
        // removeWiFi both land here. startAPMode() would re-initialise WiFi on
        // top of a live BLE session - same damage as the branches above. The
        // credentials are already cleared, and restoreWiFiAfterBLE() brings the
        // AP up when the BLE session ends (isWiFiSet is false by then, so
        // bringUpWiFiInterfaces() goes straight to startAPMode()).
        if (!bleProvisioningActive) {
            startAPMode();
        } else {
            Serial.println("BLE provisioning active - deferring AP restart to end of BLE session");
        }
        parallelAPModeEnabled=false;
    }
}

JsonDocument getWiFiSettings() {
    // Create a new empty JsonDocument object
    JsonDocument doc;
    if (isWiFiSet) {
        doc["wiFiStatus"] = "Enabled";
        doc["wiFiName"] = (String)ssidBuffer;
        if (staticIP != IPAddress(0, 0, 0, 0)) {
            doc["prefIP"] = staticIP.toString();
            doc["gwIP"] = gateway.toString();
            doc["sbMask"] = subnet.toString();
            doc["pDNS"] = primaryDNS.toString();
            doc["sDNS"] = secondaryDNS.toString();
        }
    } else {
        doc["wiFiStatus"] = "Disabled";
    }
    if (getWiFiMode() == 'W') {
        doc["currMode"] = "WIFI";
        doc["currSSID"] = WiFi.SSID();
        doc["currIP"] = WiFi.localIP().toString();
        doc["macID"] = WiFi.macAddress();

    } else if (getWiFiMode() == 'A') {
        doc["currMode"] = "AP";
        doc["currSSID"] = WiFi.softAPSSID();
        doc["currIP"] = WiFi.softAPIP().toString();
        doc["macID"] = WiFi.macAddress();

    } else if (ethernetConnected) {
        doc["currMode"] = "Ethernet";
        // doc["currSSID"] = "N/A";
        doc["currIP"] = eth.localIP().toString();
        doc["macID"] = eth.macAddress();

    } else {
        doc["currMode"] = "N/A. Refresh to check";
        doc["currSSID"] = "N/A";
        doc["currIP"] = "N/A";
    }

    return doc;
}

void generateDynamicMac(byte *mac, char conType) {
    uint64_t chipId = ESP.getEfuseMac(); // Get the chip's base MAC address (e.g., CC:BA:97:36:67:08)
    Serial.println(ESP.getEfuseMac());

    // Set the first octet to a locally administered unicast address
    mac[0] = 0x02; // Locally administered (bit 1 = 1), unicast (bit 0 = 0)

    // Use the chip ID to fill the remaining bytes, modified based on connection type
    if (conType == 'E') {
        mac[0] = 0x02; // Set the first octet to a locally administered unicast address 0000 0010
                       // Locally administered (bit 1 = 1), unicast (bit 0 = 0)

    } else if (conType == 'B') {
        mac[0] = 0x06; // 0000 0110

    } else {
        // Default: Use chipId as is
        mac[0] = (chipId) & 0xFF;
    }
    mac[5] = (chipId >> 40) & 0xFF; // e.g., CC
    mac[4] = (chipId >> 32) & 0xFF; // e.g., BA
    mac[3] = (chipId >> 24) & 0xFF; // e.g., 97
    mac[2] = (chipId >> 16) & 0xFF; // e.g., 36
    mac[1] = (chipId >> 8) & 0xFF;  // e.g., 67

    // Debug output
    // Serial.print("Generated MAC for ");
    // Serial.print(conType);
    // Serial.print(": ");
    // for (int i = 0; i < 6; i++) {
    //     if (mac[i] < 0x10) Serial.print("0"); // Add leading zero for single-digit hex
    //     Serial.print(mac[i], HEX);
    //     if (i < 5) Serial.print(":");
    // }
    // Serial.println();
}

String formatMacAddress(uint64_t mac) {


    String result = "";
    for (int i = 0; i < 6; i++) {               // Changed: start from 0, go up to 5
        uint8_t byte = (mac >> (8 * i)) & 0xFF; // Changed: use i directly
        if (byte < 16)
            result += "0";
        result += String(byte, HEX);
        if (i < 5)
            result += ":"; // Changed: add colon except for last byte
    }
    result.toUpperCase();
    return result;
}