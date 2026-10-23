#include <Arduino.h>

#include <NTPClient.h>
#include <RTClib.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include "esp_task_wdt.h"
// #include "ThingsBoard.h"
// #include <WiFiEspClient.h>
#include "api.h"
#include "ble_provisioning.h"
#include "buttonpress.h"
#include "buttontime.h"
#include "buttontiming.h"
#include "cert_management.h"
#include "energydata.h"
#include "esp_partition.h"
#include "globals.h"
#include "logdata.h"
#include "logdebug.h"
#include "logging_manager.h"
#include "mqtt.h"
#include "ota_update.h"
#include "pinmapping.h"
#include "power_monitoring.h"
#include "preference_management.h"
#include "preference_power.h"
#include "relay_led_handling.h"
#include "timesettings.h"
#include "vsmr_link.h"
#include "websocketsettings.h"
#include "wifisettings.h"

RTC_DS3231 rtc;
// RTC backup-battery health (DS3231 OSF diagnostics)
bool rtcBatteryLow = false;     // true when the coin cell can no longer hold time
int  rtcLostPowerCount = 0;     // lifetime count of oscillator-stop (OSF) events
long rtcLastDriftSec = 0;       // last NTP-vs-RTC delta in seconds (negative = RTC behind)
bool rtcOsfBaselined = false;   // first-boot OSF baseline done (avoids fleet false-positive)
// Preferences - first time settings checking
bool isFirstTime = true;
bool isScheduleSet = false;
bool isNameSet = false;
bool isPLimitSet = false;
bool isAPPassSet = false;
bool isWiFiSet = false;
uint8_t isFloatEnabled;
volatile bool isWifiChanged = false;

unsigned long previousMillis = 0;
unsigned long wifiCheckMillis = 0;

// Temporary Flags for the float function modification
volatile bool flowIncomingState;
volatile bool Flow_stopped_manual;

// May need to make relayState volatile
uint8_t relayState = 0;
uint8_t countMQTTMessagesAfterRelayOff = 0;

char runType = '0';
//  DateTime calculatedOffTime, calculatedScheduledOffTime;
DateTime calculatedScheduledOffTime;
uint8_t manualOff = 0;
uint8_t scheduledTime = 0;
uint8_t scheduledTimeOuter = 0;

// Create an AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

volatile bool dryRunAlertState, overRunAlertState, lowVoltageAlertState, highVoltageAlertState, floatingAlertState;
volatile bool lowVoltageAlertLogged, highVoltageAlertLogged, phaseAlertLogged;

DateTime espStartTime;
// Set once espStartTime holds a real boot instant. Until then rebaseEspStartTime() must
// leave it alone - setup() latches it from the already-corrected clock in that case.
bool espStartTimeLatched = false;
// Whether that boot instant can be believed, i.e. the RTC actually carried the time
// across the outage. False means espStartTime is a placeholder and no switching
// decision may be taken from it.
bool espStartTimeTrusted = false;
uint16_t dryRunRetryMinutes, overRunRetryMinutes, lowVoltageRetryMinutes, highVoltageRetryMinutes, startDelayMins, maxRunTimeMins, breakTimeMins;
uint8_t inRushExtraSecs;  // Extra seconds added to INRUSH_CURRENT_SETTLE_DURATION before overload protection arms

uint8_t isRestartEnabled;
uint8_t restartOptions;
uint8_t isSlaveControlDevice;

volatile bool buttonFactoryResetPressed;
volatile bool buttonDisableWiFiPressed;

volatile unsigned long relayOnTime = 0;
volatile unsigned long relayOffTime = 0;
TimeSpan relayOnDuration = 0;
bool runEndsOnDurationSpent = false;
bool schStatusPublishPending = false;

unsigned long relayOnRTCTime = 0;
unsigned long lastEnergySaveTime = 0;
float energyOnStart = 0;
float cumulativeEnergy = 0;

bool scheduleChanged = false;
SwitchingEvent nextEvent;
SwitchingEvent tempEvent;
char tempEventRunType;
uint8_t isTempSchedule = 0;

uint8_t onTriggerFromInterface = 0;
TimeSpan onTriggerDurationFromInterface = 0;
uint8_t offTriggerFromInterface = 0;

volatile uint8_t onTriggerFromMQTT = 0;
volatile uint8_t offTriggerFromMQTT = 0;
TimeSpan onTriggerDurationFromMQTT = 0;

unsigned long onTriggerFromMQTTStart = 0;
DateTime webOffTime;

volatile uint8_t onTriggerFromWebhook = 0;
volatile uint8_t offTriggerFromWebhook = 0;
TimeSpan onTriggerDurationFromWebhook = 0;
const uint8_t *active_pins;
void handleRemoteCommand();
#define WDT_TIMEOUT 30 // Set watchdog timeout to 30 seconds
SemaphoreHandle_t rtcMutex;

void zeroCrossRoutine(void);
volatile uint32_t zcrossTime = 0;

void setupPins() {
    pinMode(RELAY_OUT, OUTPUT);
    pinMode(RELAY_OUT_POWER_TAP, OUTPUT);
    pinMode(RELAY_OUT_POWER_TAP2, OUTPUT);
    // #if defined(LED_RGB_FLOATRELAY)
    if (ledType == 2 || ledType == 3) {
        pinMode(RELAY_OUT_FLOAT, OUTPUT);
    }
    // #endif
    pinMode(ZERO_CROSS, INPUT);
    attachInterrupt(digitalPinToInterrupt(ZERO_CROSS), zeroCrossRoutine, RISING);

    pinMode(SWITCH_1, INPUT_PULLUP);
    pinMode(SWITCH_2, INPUT_PULLUP);
    pinMode(SWITCH_3, INPUT_PULLUP);
    pinMode(SWITCH_4, INPUT_PULLUP);
    pinMode(SWITCH_OFF, INPUT_PULLUP);

#ifdef SPECIAL_DEBOUNCE_DELAY
    // CHANGE to capture both RISING (press) and FALLING (release) for duration check
    attachInterrupt(digitalPinToInterrupt(SWITCH_1), handleButton1Press, CHANGE);
    attachInterrupt(digitalPinToInterrupt(SWITCH_2), handleButton2Press, CHANGE);
    attachInterrupt(digitalPinToInterrupt(SWITCH_3), handleButton3Press, CHANGE);
    attachInterrupt(digitalPinToInterrupt(SWITCH_4), handleButton4Press, CHANGE);
    // Off button keeps FALLING - its press is detected by checkButtonOffLongPress() polling,
    // ISR fires on release for short press confirmation
    attachInterrupt(digitalPinToInterrupt(SWITCH_OFF), handleButtonOffPress, FALLING);
#else
    attachInterrupt(digitalPinToInterrupt(SWITCH_1), handleButton1Press, FALLING);
    attachInterrupt(digitalPinToInterrupt(SWITCH_2), handleButton2Press, FALLING);
    attachInterrupt(digitalPinToInterrupt(SWITCH_3), handleButton3Press, FALLING);
    attachInterrupt(digitalPinToInterrupt(SWITCH_4), handleButton4Press, FALLING);
    attachInterrupt(digitalPinToInterrupt(SWITCH_OFF), handleButtonOffPress, FALLING);
#endif

    // pinMode(FACTORY_RESET, INPUT_PULLUP);
    // pinMode(FLOAT_SWITCH, INPUT_PULLUP);

    // These 2 pins are used for Phase failure and SEq change. These are not used now. Hence commenting out
    pinMode(12, OUTPUT);
    digitalWrite(12, LOW);
    pinMode(38, OUTPUT);
    digitalWrite(38, LOW);
    // // Setup phase fault monitoring pins
    // pinMode(PHASE_FAILURE, INPUT_PULLDOWN);
    // //attachInterrupt(digitalPinToInterrupt(PHASE_FAILURE), handlePhaseFailureTrigger, CHANGE);

    // pinMode(PHASE_SEQUENCE_REVERSE, INPUT_PULLDOWN);
    // //attachInterrupt(digitalPinToInterrupt(PHASE_SEQUENCE_REVERSE), handlePhaseReversalTrigger, CHANGE);

    pinMode(LED_1, OUTPUT);
    pinMode(LED_2, OUTPUT);
    pinMode(LED_3, OUTPUT);
    pinMode(LED_4, OUTPUT);
    // #ifdef LED_NORMAL
    if (ledType == 0) {
        pinMode(LED_SMART, OUTPUT);
    } else {
        pinMode(LED_RED, OUTPUT);
        pinMode(LED_GREEN, OUTPUT);
        pinMode(LED_BLUE, OUTPUT);
    }
    // #endif

    // When there is a software restart for ex., due to brownout issue, turn off
    // the relay, as the ESP restarted
    digitalWrite(RELAY_OUT, LOW);
    digitalWrite(RELAY_OUT_POWER_TAP, LOW);
    digitalWrite(RELAY_OUT_POWER_TAP2, LOW);

    // #if defined(LED_RGB_FLOATRELAY)
    if (ledType == 2 || ledType == 3) {
        if (!isFloatEnabled) {
            Serial.println("isFloatEnabled is false");
            digitalWrite(RELAY_OUT_FLOAT, LOW);
            pinMode(FLOAT_SWITCH, INPUT_PULLUP);

        } else {
            Serial.println("isFloatEnabled is true");
            digitalWrite(RELAY_OUT_FLOAT, HIGH);
            pinMode(FLOAT_SWITCH, INPUT_PULLDOWN);

        }
    } else {
        pinMode(FLOAT_SWITCH, INPUT_PULLUP);
    }

    // Leakage detection pin (only if leak detection circuit is attached)
    if (leakDetectionEnabled) {
        pinMode(LEAK_DETECTION, INPUT_PULLDOWN);
        attachInterrupt(digitalPinToInterrupt(LEAK_DETECTION), handleLeakagePinSpike, RISING);
    }

    // Pins used for Ethernet - to a safer state -
    pinMode(SPI_MISO, INPUT_PULLDOWN);
    pinMode(W5500_INT, INPUT_PULLDOWN);

    // Outputs: Set low (no activity)
    pinMode(W5500_CS, OUTPUT);
    digitalWrite(W5500_CS, LOW);
    pinMode(W5500_RST, OUTPUT);
    digitalWrite(W5500_RST, LOW); // Or HIGH if your design prefers
    pinMode(SPI_MOSI, OUTPUT);
    digitalWrite(SPI_MOSI, LOW);
    pinMode(SPI_SCLK, OUTPUT);
    digitalWrite(SPI_SCLK, LOW);
    // end - Ethernet pins - to a safer state

    // Other pins
    pinMode(17, OUTPUT);
    digitalWrite(17, LOW);
    pinMode(11, OUTPUT);
    digitalWrite(11, LOW);
}

void IRAM_ATTR zeroCrossRoutine(void) {
    zcrossTime = millis();
    // Serial.printf("z\n");
}

void setup() {
    // esp_task_wdt_init(60, false); // 60 seconds watchdog timer

    Serial.begin(115200);
    delay(100);
    Serial.flush();
    Serial.println("setup started");
    // EEPROM.begin(EEPROM_SIZE);  // Initialize EEPROM

    // Set the watchdog timeout globally & add this task to watchdog
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL);
    rtcMutex = xSemaphoreCreateMutex();
    if (rtcMutex == NULL) {
        Serial.println("Mutex creation failed");
        while (true)
            ;
    }
    // littleFSSetup();

    // // Check if migration is needed (before initPreferences to avoid conflicts)
    // if (shouldMigrateLogs()) {
    //     Serial.println("=== FIRMWARE UPGRADE DETECTED - MIGRATING LOGS ===");
    //     Serial.print("Previous version: ");
    //     Serial.println(getPreviousFirmwareVersion());
    //     Serial.print("Current version: ");
    //     Serial.println(getFirmwareVersion());

    //     // Perform migration
    //     migrateOldLogFiles();

    //     Serial.println("=== LOG MIGRATION COMPLETED ===");
    // }

    initPreferences();
    bool vsmrLinkConfig = false;   // "3-Phase with VSMR" -> ESP-NOW downlink enabled
    String vsmrMacConfig = "";     // paired VSMR MAC filter ("" = accept any)
    read3PhaseSettings(isThreePhaseDevice, tappingRequired, tappingDuration, ethernetEnabled, ledType, shuntOrCT, masterSlaveMode, masterDeviceUrl, mPin, remoteMethods, leakDetectionEnabled, inRushExtraSecs, vsmrLinkConfig, vsmrMacConfig);

    if (ledType == 0) {
        active_pins = PINS_LED_BLUE;
    } else if (ledType == 1) {
        active_pins = PINS_LED_RGB;
    } else if (ledType == 2) {
        active_pins = PINS_LED_RGB_FLOATRELAY;
    } else if (ledType == 3) {
        active_pins = PINS_LED_RGB_FLOAT_WIFI_UPDATE;
    }

    // Initialize I2C with custom pins
    Wire.begin(SDA_PIN, SCL_PIN);

    initiateWifiSettings();

    setupPins();
    setAllIndicatorsOff();
    initButtonTiming();

    // Create the log queue before initiateTimeComponents so its boot-time debug logs
    // (EOSC / OSF / battery-health) are captured instead of dropped. The logging task
    // that drains + timestamps + persists them is started later (initUnifiedLogging),
    // once the RTC and LittleFS are ready.
    initUnifiedLogQueue();

    // Initialize NTP Client
    initiateTimeComponents();
    initializeSessions();
    littleFSSetup();
    initCertManagement();

    // Check if migration is needed (before initPreferences to avoid conflicts)

    Serial.println("before get gScheduleArray");
    // Load schedule
    if (isScheduleSet) {
        // getScheduleFromEEPROM(gScheduleArray);
        loadScheduleFromPreferences();
    }

    Serial.println("before set API call");
    // Setup API endpoints
    setupAPIs();
    // Initialize Power Sensor
    setupPowerSensor();
    readFaultDetails();

    if (isRestartEnabled) {
        readTempScheduleDetails(tempEvent, tempEventRunType);
    }

    Serial.println("before set websockets");
    initWebSocket();
    // initializeSessions(); -- moved to the top
    server.begin();
    // Create the semaphore
    // semaphore = xSemaphoreCreateBinary();

    // Create a task pinned to core 0 to check power usage
    xTaskCreatePinnedToCore(core0Task,   // Task function
                            "Core0Task", // Name of the task
                            8192,        // Stack size
                            NULL,        // Parameter to pass to the task
                            1,           // Task priority
                            NULL,        // Task handle
                            0            // Core 0
    );

    espStartTime = getDateTimeFromRTC();
    // The boot instant is only believable if the RTC carried the time across the outage.
    // A cell that held nothing reads back near 2000-01-01 and leaves OSF set. If the
    // boot-time sync inside initiateTimeComponents() succeeded, rtc.adjust() has already
    // cleared OSF and the reading is real - so this correctly comes out trusted.
    bool rtcOsfSet = false;
    if (xSemaphoreTake(rtcMutex, portMAX_DELAY) == pdTRUE) {
        rtcOsfSet = rtc.lostPower();
        xSemaphoreGive(rtcMutex);
    }
    espStartTimeTrusted = (espStartTime.unixtime() > RTC_TIME_SANITY_EPOCH) && !rtcOsfSet;
    espStartTimeLatched = true; // from here on, an RTC correction must carry espStartTime with it
    nextEvent = getNextSchedule(espStartTime);
    if ((lastManualOffTime > nextEvent.startTime && lastManualOffTime < nextEvent.endTime)) {
        manualOff = 1;
    } else if ((lastDryRunTime > nextEvent.startTime && lastDryRunTime < nextEvent.endTime)
               || (lastOverRunTime > nextEvent.startTime && lastOverRunTime < nextEvent.endTime)) {
        manualOff = 2;
    } else if ((lastHighVoltageTime > nextEvent.startTime && lastHighVoltageTime < nextEvent.endTime)
               || (lastLowVoltageTime > nextEvent.startTime && lastLowVoltageTime < nextEvent.endTime)) {
        manualOff = 4;
    }
    // Initialize unified logging system
    initUnifiedLogging();

    initEnergyData();

    // Serial.println("Cumulative Energy after initEnergyData: "+ (String)
    // cumulativeEnergy);

    setupMQTT();
    initOTA();
    // ESP-NOW downlink from a VSMR unit. No-op unless "3-Phase with VSMR" is
    // configured. Must run after Wi-Fi is up (initiateWifiSettings()).
    initVsmrLink(vsmrLinkConfig, vsmrMacConfig.c_str());
    confirmFirmwareValid();
    logAction(ModeOfAction::RESTART_ESP, 0, 0, 0, ScheduleType::SCH_NONE);

    // Serial.println("logAction completed");

    logRestartReason();
    // Serial.println(energy);
    resetButtonPress();
    TaskHandle_t loopTaskHandle = xTaskGetCurrentTaskHandle();
    vTaskPrioritySet(loopTaskHandle, 5);

    // Save current firmware version for next boot comparison
    savePreviousFirmwareVersion();

    Serial.println("setup ended");
}

void handleRTCAdjust() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    // Resync the RTC from NTP every 6 hours. This alone keeps the clock accurate and
    // supersedes the old "sync when the RTC reads 00:00" logic, which was fragile: a
    // wrong clock might never read exactly midnight. It reads no RTC of its own, so it
    // adds no per-loop I2C cost - setRTCFromNTP() touches the RTC only when it fires.
    const unsigned long RESYNC_INTERVAL_MS = 2UL * 60UL * 60UL * 1000UL; // 6 hours
    // Start one full interval "in the past" so the first eligible call (i.e. as soon as
    // WiFi is up) resyncs immediately, closing the gap where the boot-time sync ran
    // before WiFi came up. Unsigned wraparound makes this exact:
    //   millis() - (0 - INTERVAL) == millis() + INTERVAL >= INTERVAL  -> true at once.
    static unsigned long lastPeriodicResyncMs = 0UL - RESYNC_INTERVAL_MS;
    unsigned long nowMs = millis();
    if ((nowMs - lastPeriodicResyncMs) >= RESYNC_INTERVAL_MS) {
        // Success is logged (once/day) inside setRTCFromNTP(); no log needed here.
        setRTCFromNTP();
        // Reset the timer whether or not it succeeded; a failed attempt (no internet)
        // retries in another 6h, and the boot/login syncs still cover the gap.
        lastPeriodicResyncMs = nowMs;
    }
}
void factoryResetRoutine() {
    resetPreferences(); // Settings and cloud-sync are cleared
    resetEnergyData();
    clearPowerSettings();
    clearFaultDetails();

    // To do - Need to add other reset routines
    clearLogs();
    clearEnergyLogs();
    // clearDebugLogs();
}

void handleFactoryReset() {
    static unsigned long resetStartTime = 0;
    if (resetStartTime == 0) {
        // Serial.println("resetStartTime=0");

        resetStartTime = millis();
        factoryResetRoutine();
        logDebugAction(DebugAction::FACTORY_RESET_BUTTON_PRESS, (char *)"FACTORY RESET button pressed");
    } else if (millis() - resetStartTime > 2000) {
        // ESP.restart();
        // Serial.println("handleFactoryReset resetStartTime>2000");

        resetStartTime = 0;
        esp_sleep_enable_timer_wakeup(1 * 1000000);
        esp_deep_sleep_start();
    }
}
void loop() {
    // Selecting the Float flag depends on the user option

    if (isFloatEnabled == 2) {
        checkFlow();
    } // calling the flow ON function
    else if (isFloatEnabled == 1 || ledType == 0 || ledType == 1) {
        checkFloat();
    } // calling the default float OFF function
    // Clear leakage alert when circuit is fixed (pin goes LOW)
    if (leakageAlertState && digitalRead(LEAK_DETECTION) == LOW) {
        // Serial.println("leakageAlertState set to false 22222222");
        leakageAlertState = false;
    }

    esp_task_wdt_reset();
    // Ahead of the monitorPower() early return below: an abandoned firmware
    // upload has to be released on every cycle, not only on cycles where power
    // sensing happened to come back in time.
    serviceFirmwareUpload();
    DateTime curDateTime = getDateTimeFromRTC();
    if (relayState && (zcrossTime >= 3000) && ((millis() - zcrossTime) > 100) ) {
        // Serial.println("dump");
    
        dumpVitalsBeforeFlat(nextEvent.schStartTime, relayOnRTCTime, curDateTime, energyToOutput);
    }
    // wifi_sta_list_t wifi_sta_list;
    // tcpip_adapter_sta_list_t adapter_sta_list;

    // esp_wifi_ap_get_sta_list(&wifi_sta_list);
    // tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list);

    // Serial.printf("Connected devices: %d\n", adapter_sta_list.num);
    // // for (int i = 0; i < adapter_sta_list.num; i++) {
    // //     tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
    // //     Serial.printf("Device %d IP: %s\n", i+1, ip4addr_ntoa(&(station.ip)));
    // // }
    // delay(100);

    // When Slave mode is enabled, the switching is handled by the master device
    if (masterSlaveMode == 2) {
        // Check for button press events and send HTTP request to master device
        handleRemoteCommand();
    }
    else {
        handleSwitching(curDateTime);
    }

    handleLEDBlink(runType);

    // Todo to move this above switching
    if (!SENSOR_RESET_IN_PROGRESS) {
        bool powerSensingTimeout = readSensorWithTimeout(monitorPower, POWER_SENSING_TIMEOUT_MS, curDateTime);
        if (powerSensingTimeout) {
            Serial.println("monitorPower() timed out! Skipping this cycle...");
            return;
        } else {
            // Serial.println("monitorPower() checked...");
        }
    }
    esp_task_wdt_reset(); // Reset watchdog after power monitoring

    if (isThreePhaseDevice) {
        checkPhaseFaults(curDateTime);
    }
    checkFactoryReset();
    checkDisableWiFi();
    checkVsmrPairing();
    checkButtonOffLongPress();
#ifdef SPECIAL_DEBOUNCE_DELAY
    confirmButtonPresses();
#endif
    checkEnableBLE();

    if (relayState == 0 && buttonFactoryResetPressed) {
        handleFactoryReset();
        factoryResetLedIndicate = true;
        ignoreButton2FallOnce = true;
        ignoreButton3FallOnce = true;
        ignoreButtonOffFallOnce = true;
#ifdef SPECIAL_DEBOUNCE_DELAY
        buttonRiseTime2 = 0;
        buttonRiseTime3 = 0;
#endif
    }
    if (buttonDisableWiFiPressed) {
        disableWiFi();
        ignoreButton4FallOnce = true;
        ignoreButtonOffFallOnce = true;
        buttonDisableWiFiPressed = false;
#ifdef SPECIAL_DEBOUNCE_DELAY
        buttonRiseTime4 = 0;
#endif
        logDebugAction(DebugAction::DISABLE_WIFI, (char *)"WIFI details disabled with buttonpress combination");
    }
    if (buttonBLEEnablePressed) {
        Serial.println("BLE enabled via Button 1 + Button 2 (10 sec hold)");
        buttonBLEEnablePressed = false;
        ignoreButton1FallOnce = true;
        ignoreButton2FallOnce = true;
        // checkEnableBLE() clears these again once both buttons are released;
        // nothing else does while BLE is advertising.
        bleComboRearmPending = true;
#ifdef SPECIAL_DEBOUNCE_DELAY
        buttonRiseTime1 = 0;
        buttonRiseTime2 = 0;
#endif
        startBLEProvisioning();
    }
    
    // Check BLE timeout
    checkBLETimeout();
    
    handleRTCAdjust();
   

    // To avoid system restart feeding the watchdog
    esp_task_wdt_reset();
    // size_t freeHeap = xPortGetFreeHeapSize();
    // Serial.printf("Free heap : %d bytes\n", freeHeap);

    if (sendHADiscovery) {
        sendHADiscoveryMessages();
        sendHADiscovery = false;
    }
    // Picks up a /connectcloud or MQTT-settings change made while MQTT was
    // still disabled, i.e. before mqttPublisherTask exists to consume the flag.
    serviceMQTTReconfigure();
    handleMQTTMessaging();

    // Check if OTA download was triggered via RPC
    if (otaState == OTA_DOWNLOADING) {
        performOTAUpdate();
    }

    ws.cleanupClients();
    // static uint8_t printCounter=0;
    // if (printCounter >= 50) { // 2 * 500ms = 1000ms
    //           UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(NULL);
    //           Serial.print("Stack high water mark: Core 1 : ");
    //           Serial.println(highWaterMark);
    //           // Three different questions, three numbers:
    //           //   Free    - current free heap. Sampled at the same point each
    //           //             pass, so a downward trend at idle means a real leak.
    //           //   MinFree - low-water mark since boot. Monotonic by definition
    //           //             (it never recovers when memory is freed), so a slow
    //           //             decline is normal and is NOT evidence of a leak - it
    //           //             just records the deepest trough ever hit.
    //           //   MaxAlloc- largest contiguous block. If this falls while Free
    //           //             stays flat, the heap is fragmenting (String churn is
    //           //             the usual cause) and big allocations will start
    //           //             failing even with plenty of total free memory.
    //           Serial.print("Heap: free=");
    //           Serial.print(ESP.getFreeHeap());
    //           Serial.print(" minFree=");
    //           Serial.print(ESP.getMinFreeHeap());
    //           Serial.print(" maxAlloc=");
    //           Serial.print(ESP.getMaxAllocHeap());
    //           Serial.println(" bytes");
    //           printCounter = 0; // Reset counter
    //       } else {
    //           printCounter++;
    //       }

    // The old delay(100) here (and the one that used to sit in the power-sensor
    // read) is deliberately NOT restored - 100ms of it landed straight on button
    // response time.
    //
    // But loop() must still yield. UnifiedLogger runs at the same priority on
    // this core, so if loop() never blocks it stays Ready continuously and the
    // logger cannot win enough timeslices to drain its queue - log throughput
    // collapses and entries get dropped. vTaskDelay() blocks the task (it does
    // not spin), handing the core to the logger for this window.
    // Emit the single WS frame owed by any notifyClients() calls that were
    // coalesced during this pass (see websocketsettings.cpp).
    flushPendingNotify();

    vTaskDelay(pdMS_TO_TICKS(20));
}
void handleRemoteCommand() {
// Check for floating alert state and send OFF request to master device
        // Send when state transitions from false to true, or resend every 20 seconds if still active
        unsigned long currentTime = millis();
        if (floatingAlertState) {
            // Check if we need to send the message (first time or 20 seconds have passed)
            if (!floatingAlertOffSent || (currentTime - lastFloatingAlertOffSentTime >= 20000)) {
                Serial.println("inside remote command sending");
                // Only log when floatingAlertOffSent is false (first time)
                bool shouldLog = !floatingAlertOffSent;
                // Send OFF request to master device due to floating alert
                if (remoteMethods.indexOf("mqtt") >= 0) {
                    sendMQTTRemoteSwitchMessage("OFF", 0, shouldLog);
                    if (shouldLog) {
                        logRefreshRequired = true;  // Set flag to refresh logs
                    }
                }
                if (remoteMethods.indexOf("local") >= 0) {
                    Serial.println("inside remote command sending - Local");
                    sendButtonPressToMaster(0, "OFF", shouldLog);
                    if (shouldLog) {
                        logRefreshRequired = true;  // Set flag to refresh logs
                    }
                }
                floatingAlertOffSent = true;  // Mark as sent
                lastFloatingAlertOffSentTime = currentTime;  // Update timestamp
            }
        } else if (!floatingAlertState && floatingAlertOffSent) {
            // Reset flag when floating alert state clears
            floatingAlertOffSent = false;
            lastFloatingAlertOffSentTime = 0;  // Reset timestamp
        }
        // Check for flow incoming state changes and send appropriate message to master device
        if (flowIncomingState && !lastFlowIncomingStateSent) {
            // Flow state transitioned from false to true - send ON request to start device
            // Use button 11 for 23H:59M duration
            if (remoteMethods.indexOf("mqtt") >= 0) {
                sendMQTTRemoteSwitchMessage("ON", 11);  // Use button 11 for 23H:59M duration
                logRefreshRequired = true;  // Set flag to refresh logs
            }
            if (remoteMethods.indexOf("local") >= 0) {
                sendButtonPressToMaster(11, "ON");  // Use button 11 for 23H:59M duration
                logRefreshRequired = true;  // Set flag to refresh logs
            }
            lastFlowIncomingStateSent = true;  // Update tracking flag
        } else if (!flowIncomingState && lastFlowIncomingStateSent) {
            // Flow state transitioned from true to false - send OFF request to stop device
            if (remoteMethods.indexOf("mqtt") >= 0) {
                sendMQTTRemoteSwitchMessage("OFF", 0);
                logRefreshRequired = true;  // Set flag to refresh logs
            }
            if (remoteMethods.indexOf("local") >= 0) {
                sendButtonPressToMaster(0, "OFF");
                logRefreshRequired = true;  // Set flag to refresh logs
            }
            lastFlowIncomingStateSent = false;  // Update tracking flag
        }
        // Check for button press events and send HTTP request to master device
        if (button1Pressed || button2Pressed || button3Pressed || button4Pressed) {
            // Determine which button was pressed
            uint8_t buttonNo = button1Pressed ? 1 : button2Pressed ? 2 : button3Pressed ? 3 : button4Pressed ? 4 : 0;

            // Check conditions before sending ON request
            // Don't send start command if floatingAlertState is existing
            // Don't send start command if flow mode is enabled and flowIncomingState is not set
            bool shouldSendStart = true;
            if (floatingAlertState) {
                shouldSendStart = false;
                floatStateLedIndicate = true;
                logAction(ModeOfAction::START_MASTER_TANK_FULL_REJECT, 0, 0, 0, ScheduleType::SCH_NONE);
                logRefreshRequired = true;  // Set flag to refresh logs for failure log
            } 
            else if (isFloatEnabled == 2 && !flowIncomingState) {
                shouldSendStart = false;
                floatStateLedIndicate = true;
                logAction(ModeOfAction::START_MASTER_FLOW_REJECT, 0, 0, 0, ScheduleType::SCH_NONE);
                logRefreshRequired = true;  // Set flag to refresh logs for failure log
            }

            // Send ON request to master device only if conditions are met
            // Ensure buttonNo is valid (1-4) before sending ON command
            if (shouldSendStart && buttonNo >= 1 && buttonNo <= 4) {
                if (remoteMethods.indexOf("mqtt") >= 0) {
                    sendMQTTRemoteSwitchMessage("ON", buttonNo);
                    logRefreshRequired = true;  // Set flag to refresh logs
                }
                if (remoteMethods.indexOf("local") >= 0) {
                    sendButtonPressToMaster(buttonNo, "ON");
                    logRefreshRequired = true;  // Set flag to refresh logs
                }
            }
            else {
                logDebugAction(DebugAction::SYSTEM_FAILURE, (char *)"Invalid button press combination - Main Loop");
            }
            resetButtonPress();
        } else if (buttonOffPressed) {
            // Send OFF request to master device
            if (remoteMethods.indexOf("mqtt") >= 0) {
                sendMQTTRemoteSwitchMessage("OFF", 0);
                logRefreshRequired = true;  // Set flag to refresh logs
            }
            if (remoteMethods.indexOf("local") >= 0) {
                sendButtonPressToMaster(0, "OFF");
                logRefreshRequired = true;  // Set flag to refresh logs
            }

            resetButtonPress();
        } else {
            // Periodically check master state when slave sent a button press

            if (remoteMethods.indexOf("mqtt") >= 0) {
                if (masterSwitchOnStatusMqtt && (millis() - masterSwitchOnTimeMqtt) > 60000) {
                    masterSwitchOnStatusMqtt = false;
                }
            }
            if (remoteMethods.indexOf("local") >= 0) {
                checkMasterState();
            }
            // Any Relay ON requests need to be ignored
            
        }
}
void printFreeHeap() {
    size_t freeHeap = xPortGetFreeHeapSize();
    Serial.printf("Free heap: %d bytes\n", freeHeap);

    // Surface log messages that never reached the queue. Reported only when the
    // count grows, so a persistent drop condition doesn't spam - without this
    // the loss is completely silent and looks like "the log was never written".
    // static uint32_t lastReportedDrops = 0;
    // uint32_t drops = droppedLogCount;
    // if (drops != lastReportedDrops) {
    //     lastReportedDrops = drops;
    //     Serial.printf("[LOG] dropped messages: %u\n", (unsigned)drops);
    // }
}

// Logs only when this task's stack high-water mark drops below 1 KB,
// AND only when it worsens past the previous low. Each calling task owns
// a `static uint32_t low = UINT32_MAX;` and passes it by reference, so
// the first dip below 1 KB is reported and subsequent reports happen
// only when it gets worse — no log spam.
void checkStackHighWater(uint32_t &prevLow) {
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);  // bytes remaining, lowest ever
    if (hwm < 1024 && hwm < prevLow) {
        prevLow = hwm;
        char buf[80];
        snprintf(buf, sizeof(buf), "STACK low task=%s bytes=%u",
                 pcTaskGetName(NULL), (unsigned)hwm);
        Serial.printf("[STACK] %s\n", buf);
        // Persist so it survives reboot and can be pulled via the log API.
        // Safe to call from a low-stack context: the check runs at the top
        // of each task's loop (shallowest stack point) and queueDebugLog
        // just enqueues — actual flash write happens on unifiedLoggingTask.
        logDebugAction(DebugAction::SYSTEM_FAILURE, buf);
    }
}
void core0Task(void *parameter) {
    esp_task_wdt_add(NULL);
    TickType_t lastWakeTime = xTaskGetTickCount();
    const TickType_t interval = 500 / portTICK_PERIOD_MS;
    // uint8_t printCounter = 0;
    bool logsSent = false;

    static uint32_t core0StackLow = UINT32_MAX;
    for (;;) {
        esp_task_wdt_reset();
        vTaskDelayUntil(&lastWakeTime, interval);

        // Drain WiFi-event deferred work first; the WiFi stack task has only
        // ~3 KB of stack, so heavy work (mDNS restart, String allocs) is
        // queued there and executed here on this 8 KB-stack task.
        serviceWiFiEventDeferred();

        // Web portal asked to forget the WiFi credentials. Done here rather than
        // in the API handler so the radio/mDNS work runs on this task's stack,
        // and the 1s grace lets the HTTP response reach the browser before the
        // STA link goes away.
        if (removeWiFiRequestedAt != 0 && (millis() - removeWiFiRequestedAt) > 1000) {
            removeWiFiRequestedAt = 0;
            Serial.println("Removing WiFi configuration on web portal request");
            disableWiFi();
        }

        checkStackHighWater(core0StackLow);

        // #ifdef LED_RGB_FLOATRELAY
        if (ethernetConnected && (millis() - wifiCheckMillis > WIFI_CHECK_INTERVAL)) {
            wifiCheckMillis = millis();
            checkEthernetConnection();
        }
        // #endif
        if (!ethernetConnected && (millis() - wifiCheckMillis > WIFI_CHECK_INTERVAL)) {
            // Serial.println("Checking WiFi connection...");
            wifiCheckMillis = millis();
            checkWifiConnection();
        }

        // ESP-NOW VSMR link: broadcast the channel/MAC beacon (~1 Hz) only
        // while VSMR isn't being heard; silent once voltage frames flow.
        serviceVsmrLink();
        
        // Check for long press of stop button
        // checkButtonOffLongPress();
        if (buttonOffLongPressed) {
            if (bleProvisioningActive) {
                // If BLE pairing is active, stop it first
                Serial.println("Stop button long press - disabling BLE provisioning");
                stopBLEProvisioning();
            } else if (getWiFiMode() == 'W' && WiFi.status() == WL_CONNECTED && !parallelAPModeEnabled) {
                Serial.println("inside core 0 task - button off long pressed - enable parallel AP mode");
                enableParallelAPMode();
            }
            // Reset the flag and tracking to avoid repeated calls
            buttonOffLongPressed = false;
            buttonOffPressStartTime = 0;
        }

        // static uint8_t printCounter=0;
        // if (printCounter >= 50) { // 2 * 500ms = 1000ms
        //         UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(NULL);
        //         Serial.print("Stack high water mark: Core 0 : ");
        //         Serial.println(highWaterMark);
        //         printCounter = 0; // Reset counter
        //         printFreeHeap();
        // }

        // } else {
        //     printCounter++;
        // }

        delay(100);
    }
}

// String getGlobalOtherValuesfromMain() {
//     String html = "";
//     html += "<tr><td>previousMillis</td><td>" + String(previousMillis) + "</td></tr>";
//     html += "<tr><td>wifiCheckMillis</td><td>" + String(wifiCheckMillis) + "</td></tr>";
//     html += "<tr><td>manualOff</td><td>" + String(manualOff) + "</td></tr>";
//     html += "<tr><td>onTrigger</td><td>" + String(onTriggerFromInterface) + "</td></tr>";
//     html += "<tr><td>offTrigger</td><td>" + String(offTriggerFromInterface) + "</td></tr>";
//     html += "<tr><td>isFirstTime</td><td>" + String(isFirstTime) + "</td></tr>";
//     html += "<tr><td>wifiMode</td><td>" + String(getWiFiMode()) + "</td></tr>";
//     html += "<tr><td>ethernetConnected</td><td>" + String(ethernetConnected) + "</td></tr>";
//     html += "<tr><td>lowVoltage LED Indicate/td><td>" + String(lowVoltageLedIndicate) + "</td></tr>";
//     html += "<tr><td>highVoltage LEDIndicate/td><td>" + String(highVoltageLedIndicate) + "</td></tr>";
//     html += "<tr><td>lowVoltage Alert State/td><td>" + String(lowVoltageAlertState) + "</td></tr>";
//     html += "<tr><td>high Voltage Alert State/td><td>" + String(highVoltageAlertState) + "</td></tr>";
//     html += "<tr><td>phaseFailure Alert State</td><td>" + String(phaseFailureAlertState) + "</td></tr>";
//     html += "<tr><td>phaseSequenceReverse Alert State</td><td>" + String(phaseSequenceReverseAlertState) + "</td></tr>";

//     html += "<tr><td>cumulativeEnergy</td><td>" + String(cumulativeEnergy) + "</td></tr>";
//     html += "<tr><td>energyOnStart</td><td>" + String(energyOnStart) + "</td></tr>";
//     html += "<tr><td>energy</td><td>" + String(energy) + "</td></tr>";
//     html += "<tr><td>currentsessionEnergy</td><td>" + String(energy - energyOnStart) + "</td></tr>";
//     html += "<tr>ESP.getHeapSize()()<td></td><td>" + String(ESP.getHeapSize()) + "</td></tr>";
//     html += "<tr>ESP.getFreeHeap()()<td></td><td>" + String(ESP.getFreeHeap()) + "</td></tr>";
//     html += "<tr>ESP.getMinFreeHeap()()<td></td><td>" + String(ESP.getMinFreeHeap()) + "</td></tr>";
//     html += "<tr>ESP.getMaxAllocHeap()()<td></td><td>" + String(ESP.getMaxAllocHeap()) + "</td></tr>";

//     return html;
// }

// String getGlobalScheduleValuesAsJson() {
//   const size_t capacity =  200; // Adjust this
//   StaticJsonDocument<capacity> doc;

//   doc["timeButton"] = TIMERCONFIG;
//   doc["firmware"] = getFirmwareVersion();
//   doc["relayState"] = relayState;
//   doc["runType"] = runType;
//   doc["scheduledTime"] = scheduledTime;
//   doc["isTempSchedule"] = isTempSchedule;
//   doc["scheduleChanged"] = scheduleChanged;
//   doc["nextStart"] = formatDateTime(nextEvent.startTime);

//   String jsonOutput;
//   if (serializeJson(doc, jsonOutput) == 0) {
//     return "{\"error\":\"Capacity exceeded\"}"; // Fallback
//   }
//   return jsonOutput;
// }

// String getGlobalScheduleValuesfromMain() {
//     String html = "";
//     char buttonTimingStr[20];
//     readButtonTimingString(buttonTimingStr, sizeof(buttonTimingStr));
//     html += "<tr><td>TIME Button configuration</td><td>" + String(buttonTimingStr) + "</td></tr>";

//     if (strcmp((const char *)"S", shuntOrCT.c_str()) == 0) {
//         html += "<tr><td>Shunt/CT Indicator</td><td>SHUNT</td></tr>";
//     } else {
//         html += "<tr><td>Shunt/CT Indicator</td><td>CT</td></tr>";
//     }

//     if (ledType == 0) {
//         html += "<tr><td>LED version/ PIN config</td><td>NORMAL BLUE</td></tr>";
//     } else if (ledType == 1) {
//         html += "<tr><td>LED version/ PIN config</td><td>RGB LED</td></tr>";
//     } else if (ledType == 2) {
//         html += "<tr><td>LED version/ PIN config</td><td>Float switch enabled</td></tr>";
//     } else if (ledType == 3) { 
//         html += "<tr><td>LED version/ PIN config</td><td>Float switch enabled with WiFi changes</td></tr>";
//     }

//     html += "<tr><td>Firmware Version</td><td>" + getFirmwareVersion() + "</tr>";
//     html += "<tr><td>--</td><td>--</td></tr>";

//     html += "<tr><td>relayState</td><td>" + String(relayState) + "</td></tr>";
//     html += "<tr><td>runType</td><td>" + String(runType) + "</td></tr>";
//     html += "<tr><td>scheduledTime Indicator</td><td>" + String(scheduledTime) + "</tr>";
//     html += "<tr><td>isTempSchedule</td><td>" + String(isTempSchedule) + "</td></tr>";
//     html += "<tr><td>sched Chngd</td><td>" + String(scheduleChanged) + "</tr>";
//     html += "<tr><td>nxt.start</td><td>" + formatDateTime(nextEvent.startTime) + "</tr>";
//     html += "<tr><td>nxt.end</td><td>" + formatDateTime(nextEvent.endTime) + "</tr>";
//     html += "<tr><td>nxt.sch start</td><td>" + formatDateTime(nextEvent.schStartTime) + "</tr>";
//     html += "<tr><td>nxt.sch end</td><td>" + formatDateTime(nextEvent.schEndTime) + "</tr>";
//     html += "<tr><td>restart after powercut</td><td>" + String(isRestartEnabled) + "</tr>";
//     html += "<tr><td>dryrun restart</td><td>" + String(restartOptions >> 1 & 0x01) + "</tr>";
//     html += "<tr><td>overload restart</td><td>" + String(restartOptions >> 2 & 0x01) + "</tr>";
//     html += "<tr><td>low voltage restart</td><td>" + String(restartOptions >> 3 & 0x01) + "</tr>";
//     html += "<tr><td>high voltage restart</td><td>" + String(restartOptions >> 4 & 0x01) + "</tr>";
//     // html += "<tr><td>phase failure restart</td><td>" + String(restartOptions >> 5 & 0x01) + "</td></tr>";
//     // html += "<tr><td>phase sequence reverse restart</td><td>" + String(restartOptions >> 6 & 0x01) + "</td></tr>";
//     html += "<tr><td>manualOff</td><td>" + String(manualOff) + "</td></tr>";

//     html += "<tr><td>current time inside</td><td>" + formatDateTime(getDateTimeFromRTC()) + "</td></tr>";
//     html += "<tr><td>calculatedScheduledOffTime</td><td>" + formatDateTime(calculatedScheduledOffTime) + "</td></tr>";

//     html += "<tr><td>lastManualOffTime</td><td>" + formatDateTime(lastManualOffTime) + "</td></tr>";
//     html += "<tr><td>lastDryRunTime</td><td>" + formatDateTime(lastDryRunTime) + "</td></tr>";
//     html += "<tr><td>lastOverRunTime</td><td>" + formatDateTime(lastOverRunTime) + "</td></tr>";
//     html += "<tr><td>lastHighVoltageTime</td><td>" + formatDateTime(lastHighVoltageTime) + "</td></tr>";
//     html += "<tr><td>lastLowVoltageTime</td><td>" + formatDateTime(lastLowVoltageTime) + "</td></tr>";
//     html += "<tr><td>lastPhaseFailureTime</td><td>" + formatDateTime(lastPhaseFailureTime) + "</td></tr>";
//     html +=
//         "<tr><td>lastPhaseSequenceReverseTime</td><td>" + formatDateTime(lastPhaseSequenceReverseTime) + "</td></tr>";
//     html +=
//         "<tr><td>isFloatEnabled </td><td>" + String(isFloatEnabled) + "</td></tr>";
//     for (uint8_t i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
//         unsigned long actTime = rtcSessionData.sessions[i].lastActivityTime;
//         html += "<tr><td>Session</td><td>Session :" + String(i)
//                 + (String) ": active=" + (rtcSessionData.sessions[i].active ? "true" : "false")
//                 + (String) ", token=" + String(rtcSessionData.sessions[i].token) + (String) ", lastActivityTime="
//                 + String(rtcSessionData.sessions[i].lastActivityTime) + (String) " </td></tr>";
//     }
//     return html;
// }

void handleSwitching(DateTime curDateTime) {
    unsigned long currentMillis = millis();
    // static uint8_t printCounter=0;
    if (currentMillis - previousMillis >= LOOP_INTERVEL) {
        // DateTime curDateTime = getDateTimeFromRTC();

        DateTime nextStart, nextEnd;
        // ScheduleType schType;

        if (scheduleChanged) {
            // Serial.println("schedule changed");
            nextEvent = getNextSchedule(curDateTime);
            scheduleChanged = false;
            // String scheduleString = String(nextEvent.startTime.dayOfTheWeek()) + "-"
            //                         + String(nextEvent.startTime.hour()) + ":" + String(nextEvent.startTime.minute())
            //                         + " - " + String(nextEvent.endTime.hour()) + ":"
            //                         + String(nextEvent.endTime.minute());
            // Serial.println(scheduleString);
            sendMQTTAttrib = true;
        }
        nextStart = nextEvent.startTime;
        nextEnd = nextEvent.endTime;
        // schType = nextEvent.scheduleType;
        PowerResumeMode restartOption = nextEvent.restartOption;

        if ((nextStart != DateTime(0, 0, 0, 0, 0, 0)) && (curDateTime >= nextEvent.schStartTime)
            && (curDateTime < nextEvent.schEndTime)) {
            scheduledTimeOuter = 1;
        } else {
            // Serial.println("inside sch time = 0");
            scheduledTimeOuter = 0;
        }
        if ((nextStart != DateTime(0, 0, 0, 0, 0, 0)) && (curDateTime >= nextStart) && (curDateTime < nextEnd)) {
            scheduledTime = 1;
        } else {
            // Serial.println("inside sch time = 0");
            scheduledTime = 0;
        }
       
        // The phase alert states are no longer gated here - the start paths run and
        // handleRelayOn() rejects them like float/HV/LV, so the attempt gets logged
        // whichever path asked for it. phaseAlertLogged then takes over and keeps the
        // block shut, the way lowVoltageAlertLogged does.
        if (relayState != 1 && !lowVoltageAlertLogged && !highVoltageAlertLogged && !phaseAlertLogged
            && !leakageDetected) {
            if (button1Pressed || button2Pressed || button3Pressed || button4Pressed) {
                // Form the schedule object based on the button numbers
                uint8_t buttonNo = button1Pressed   ? 1
                                   : button2Pressed ? 2
                                   : button3Pressed ? 3
                                   : button4Pressed ? 4
                                                    : 0;

                handleRelayOnManualPress(buttonNo, curDateTime);
                resetButtonPress();
            } else if (onTriggerFromInterface == 1) {
                handleRelayOnfromWeb(curDateTime, onTriggerDurationFromInterface);
                onTriggerFromInterface = 0;
                onTriggerDurationFromInterface = 0;
            } else if (onTriggerFromMQTT == 1) {
                // Serial.println("case .... onTriggerFromMQTT==1");
                handleRelayOnfromMQTT(curDateTime, onTriggerDurationFromMQTT);
                onTriggerFromMQTT = 0;
                onTriggerDurationFromMQTT = 0;

            } else if (onTriggerFromWebhook == 1) {
                Serial.println("case .... onTriggerFromWebhook==1");
                Serial.println("millis() before calling handleRelayOnfromWebhook");
                // Serial.println(millis());
                handleRelayOnfromWebhook(curDateTime, onTriggerDurationFromWebhook);
                onTriggerFromWebhook = 0;
                onTriggerDurationFromWebhook = 0; // No duration for webhook trigger

            } else if (flowIncomingState && Flow_stopped_manual == false && isFloatEnabled == 2) {
                if ((!(lastDryRunTime > nextEvent.schStartTime && lastDryRunTime < nextEvent.schEndTime)
                        || ((restartOptions >> 1 & 0x01) && curDateTime > lastDryRunTime + dryRunRetryMinutes * 60))
                    && (!(lastOverRunTime > nextEvent.schStartTime && lastOverRunTime < nextEvent.schEndTime)
                        || ((restartOptions >> 2 & 0x01) && curDateTime > lastOverRunTime + overRunRetryMinutes * 60))
                    && (!(lastLowVoltageTime > nextEvent.startTime && lastLowVoltageTime < nextEvent.endTime)
                        || ((restartOptions >> 3 & 0x01)
                            && curDateTime > lastLowVoltageTime + lowVoltageRetryMinutes * 60))
                    && (!(lastHighVoltageTime > nextEvent.startTime && lastHighVoltageTime < nextEvent.endTime)
                        || ((restartOptions >> 4 & 0x01)
                            && curDateTime > lastHighVoltageTime + highVoltageRetryMinutes * 60))
                    && (!(lastPhaseFailureTime > nextEvent.startTime && lastPhaseFailureTime < nextEvent.endTime)
                         || ((restartOptions >> 3 & 0x01)
                             && curDateTime > lastPhaseFailureTime + dryRunRetryMinutes * 60))
                     && (!(lastPhaseSequenceReverseTime > nextEvent.startTime
                           && lastPhaseSequenceReverseTime < nextEvent.endTime)
                         || ((restartOptions >> 3 & 0x01)
                             && curDateTime > lastPhaseSequenceReverseTime + dryRunRetryMinutes * 60))
                    && (manualOff != 5 || ( millis() > relayOffTime + (breakTimeMins * 60  * 1000)))) {
                        
                    handleRelayOnFlow(curDateTime);
                }
               
               // handleRelayOnFlow(curDateTime);
            }
            // To check whether it is scheduled time and manual off is not done after
            // scheduled start time

            else if (scheduledTime && isFloatEnabled !=2
                     && (nextEnd > curDateTime + TimeSpan(0, 0, 0, 55))
                     // A Duration Based schedule's window stays "live" even after the
                     // owed ON time has been delivered, so the window alone must not
                     // authorise a start - there has to be duration still owed.
                     && (nextEvent.scheduleType != ScheduleType::DURATION || nextEvent.remainingDuration > 0)
                     && !(lastManualOffTime >= nextEvent.schStartTime && lastManualOffTime < nextEvent.schEndTime)
                     && (manualOff != 3) && (manualOff != 6)
                     && (!(lastDryRunTime > nextEvent.schStartTime && lastDryRunTime < nextEvent.schEndTime)
                         || ((restartOptions >> 1 & 0x01) && curDateTime > lastDryRunTime + dryRunRetryMinutes * 60))
                     && (!(lastOverRunTime > nextEvent.schStartTime && lastOverRunTime < nextEvent.schEndTime)
                         || ((restartOptions >> 2 & 0x01) && curDateTime > lastOverRunTime + overRunRetryMinutes * 60))
                     && (!(lastLowVoltageTime > nextEvent.startTime && lastLowVoltageTime < nextEvent.endTime)
                         || ((restartOptions >> 3 & 0x01)
                             && curDateTime > lastLowVoltageTime + lowVoltageRetryMinutes * 60))
                     && (!(lastHighVoltageTime > nextEvent.startTime && lastHighVoltageTime < nextEvent.endTime)
                         || ((restartOptions >> 4 & 0x01)
                             && curDateTime > lastHighVoltageTime + highVoltageRetryMinutes * 60))
                     && (!(lastPhaseFailureTime > nextEvent.startTime && lastPhaseFailureTime < nextEvent.endTime)
                         || ((restartOptions >> 3 & 0x01)
                             && curDateTime > lastPhaseFailureTime + lowVoltageRetryMinutes * 60))
                     && (!(lastPhaseSequenceReverseTime > nextEvent.startTime
                           && lastPhaseSequenceReverseTime < nextEvent.endTime)
                         || ((restartOptions >> 3 & 0x01)
                             && curDateTime > lastPhaseSequenceReverseTime + lowVoltageRetryMinutes * 60))
                     && (manualOff != 5 || (millis() > relayOffTime + (breakTimeMins * 60 * 1000)))
                     && (curDateTime >= (espStartTime + TimeSpan(0, 0, startDelayMins, 0)))
                     // "we booted before this window began, so this is a normal scheduled
                     // start, not a resume" - only admissible when the boot instant is
                     // believable. With a cell that held nothing espStartTime sits at
                     // 2000-01-01, which is before EVERY window start, and would authorise
                     // the start on its own - switching the relay back on even though
                     // restart-after-powercut is off. Untrusted boot time => isRestartEnabled
                     // is the only way in.
                     && ((espStartTimeTrusted && espStartTime < nextEvent.schStartTime) || isRestartEnabled)

                     //&& (!(espStartTime > nextStart && espStartTime < nextEnd) ||
                     //(isRestartEnabled && curDateTime > espStartTime+TimeSpan(0, 0,
                     // startDelayMins, 0)))
            ) {
                // Explanation -
                // restartOptions contains all restart selections -
                // LSB - restart after power, LSB+1 - restart after dryrun, LSB+2 -
                // after overload etc By shfting the bit rightside and masking with 1
                // gives whetehr restart selected or not !isTempSchedule is added to
                // avoid a log "started at schedule time", if it is manual start and the
                // powerdown happens within a minute curDateTime <
                // (nextStart+TimeSpan(0, 0, 1, 0)) - if the start happens within a
                // minute of schedule, it is considered scheduled start, otherwise
                // resumption curDateTime >= (espStartTime+TimeSpan(0, 0,
                // startDelayMins, 0) - to make sure, startdelay is taken care
                // (curDateTime >= (espStartTime+TimeSpan(0, 0, startDelayMins, 0))) -
                // for checking delay time, in both schedule/ tempschedule/ cases.

                // Serial.println("isTempSchedule");
                // Serial.println(isTempSchedule);

                // If there is any ESP resstart after schedule/temp sch start, Restart
                // should be enabled

                // Temp schedule is not to be logged as Schedule. Also if schedule
                // starts after 1 minute of sch time, it is considered as Resume
                // operation
                if ((!isTempSchedule && (curDateTime < (nextStart + TimeSpan(0, 0, 1, 0))))) {
                    if (nextEvent.scheduleType == ScheduleType::CYCLIC) {
                        handleRelayOnCyclic(nextStart, nextEnd, curDateTime);
                    } else if (nextEvent.scheduleType == ScheduleType::DURATION) {
                        // nextEnd is the window end here, not the off time -
                        // handleRelayOnSchedule() takes the run length from
                        // nextEvent.remainingDuration for this type.
                        handleRelayOnSchedule(nextStart, nextEnd, curDateTime);
                    } else {
                        handleRelayOnSchedule(nextStart, nextEnd, curDateTime);
                    }
                } else {
                    handleRelayOnResume(nextStart, nextEnd, curDateTime);
                }
            }
            // Serial.println("inside schedule condition");

        } else if (relayState == 1) {
            if (leakDetectionEnabled && leakageDetected) {
                Serial.println("inside Main Handle switching - leakageDetected");
                handleRelayOFF('A', curDateTime);
                manualOff = 6;
                Serial.println("leakageAlertState set to true");
                leakageAlertState = true;
                leakageDetected = false;  // Clear after handling
            } else if (dryRunAlertState) {
                handleRelayOFF('A', curDateTime);
                manualOff = 2;
                lastDryRunTime = curDateTime;
                updateFaultDetails();
            } else if (overRunAlertState) {
                handleRelayOFF('A', curDateTime);
                manualOff = 2;
                lastOverRunTime = curDateTime;
                updateFaultDetails();
            } else if (highVoltageAlertState) {
                handleRelayOFF('A', curDateTime);
                manualOff = 4;
                lastHighVoltageTime = curDateTime;
                updateFaultDetails();
            } else if (lowVoltageAlertState) {
                handleRelayOFF('A', curDateTime);
                manualOff = 4;
                lastLowVoltageTime = curDateTime;
                updateFaultDetails();
            } else if (flowIncomingState == false && isFloatEnabled == 2) {
                handleRelayOFF('L', curDateTime);
                // updateFaultDetails();
            } else if (floatingAlertState) {
                handleRelayOFF('F', curDateTime);
                manualOff = 3;
                // updateFaultDetails();
            }

            else if (phaseFailureAlertState) {
                Serial.println("inside relay on - but phaseFailureAlertState");
                handleRelayOFF('P', curDateTime);
                manualOff = 4;
                lastPhaseFailureTime = curDateTime;
                updateFaultDetails();
            }

            else if (phaseSequenceReverseAlertState) {
                handleRelayOFF('R', curDateTime);
                manualOff = 4;
                lastPhaseSequenceReverseTime = curDateTime;
                updateFaultDetails();
            }

            // else if (calculatedOffTime != DateTime(0, 0, 0, 0, 0, 0) && curDateTime
            // > calculatedOffTime ){
            else if (millis() > relayOnTime + (relayOnDuration.totalseconds() * 1000)) {
                if (runType == 'S' || runType == 'C' || runType == 'L' || (runType == 'R' && !isTempSchedule)) {
                    // A Duration Based run that ends on its owed ON time stops before
                    // the window closes - a different event from "stopped at scheduled
                    // time", so it gets its own log. Which of the two this run was
                    // headed for was settled at switch-on (see handleRelayOnSchedule),
                    // where the owed time and the window end were both still known.
                    if (runEndsOnDurationSpent) {
                        handleRelayOFF('D', curDateTime);
                        Serial.println("Duration completed off" + formatDateTime(curDateTime));
                    } else {
                        handleRelayOFF('S', curDateTime);
                        Serial.println("Scheduled off" + formatDateTime(curDateTime));
                    }
                    scheduleChanged = true;
                } else {
                    Serial.println("timer off" + formatDateTime(curDateTime));

                    handleRelayOFF('T', curDateTime);
                }
            } else if (buttonOffPressed) {
                if (flowIncomingState && isFloatEnabled == 2) {
                    Flow_stopped_manual = true; // this line is indication flag for telling the manual off happening
                }
                if (!isTempSchedule && (runType == 'S' || runType == 'R' || runType == 'C' || runType == 'L')) {
                    handleRelayOFF('M', curDateTime);
                    manualOff = 1;
                    lastManualOffTime = curDateTime;
                    updateFaultDetails();
                } else {
                    handleRelayOFF('M', curDateTime);
                }
                resetButtonPress();
            } else if (offTriggerFromInterface == 1) {
                if (flowIncomingState && isFloatEnabled == 2) {
                    Flow_stopped_manual = true; // this line is indication flag for telling the manual off happening
                }
                if (!isTempSchedule && (runType == 'S' || runType == 'C' || runType == 'L' || runType == 'R')) {
                    handleRelayOFF('W', curDateTime);
                    manualOff = 1;
                    lastManualOffTime = curDateTime;
                    updateFaultDetails();
                } else {
                    handleRelayOFF('W', curDateTime);
                }
                offTriggerFromInterface = 0;
            } else if (offTriggerFromMQTT == 1) {
                 if (flowIncomingState && isFloatEnabled == 2) {
                    Flow_stopped_manual = true; // this line is indication flag for telling the manual off happening
                }
                if (!isTempSchedule && (runType == 'S' || runType == 'C' || runType == 'L' || runType == 'R')) {
                    handleRelayOFF('Q', curDateTime);
                    manualOff = 1;
                    lastManualOffTime = curDateTime;
                    updateFaultDetails();
                } else {
                    handleRelayOFF('Q', curDateTime);
                }
                offTriggerFromMQTT = 0;
            } else if (offTriggerFromWebhook == 1) {
                 if (flowIncomingState && isFloatEnabled == 2) {
                    Flow_stopped_manual = true; // this line is indication flag for telling the manual off happening
                }
                if (!isTempSchedule && (runType == 'S' || runType == 'C' || runType == 'L' || runType == 'R')) {
                    handleRelayOFF('H', curDateTime);
                    manualOff = 1;
                    lastManualOffTime = curDateTime;
                    updateFaultDetails();
                } else {
                    handleRelayOFF('H', curDateTime);
                }
                offTriggerFromWebhook = 0;
            } else if (maxRunTimeMins > 0 && millis() > relayOnTime + (maxRunTimeMins * 60  * 1000)) {
                    Serial.println("Relay maxtime break" + formatDateTime(curDateTime));
                    handleRelayOFF('B', curDateTime); // Relay maxtime break
                    manualOff = 5;
            } 
            // else if (!scheduledTime && calculatedOffTime==DateTime(0, 0, 0, 0, 0,
            // 0)) {
            //   handleRelayOFF('S',curDateTime ) ;
            //   Serial.println("Scheduled off" + formatDateTime(curDateTime));
            //   scheduleChanged=true;
            // }
            else {
                if (((millis() - lastEnergySaveTime) > ENERGY_SAVE_INTERVAL) && !zcrossTime) {
                    unsigned long currentRTCTime = curDateTime.unixtime();
                    lastEnergySaveTime = millis();
                    // Serial.println("energy-intermediate");
                    // Serial.println(energy);
                    // Persist the guarded session energy (energyToOutput) so a
                    // CF_CNT decrease can never write a negative value to NVS.
                    updateTempEnergyData(currentRTCTime, energyToOutput, cumulativeEnergy + energyToOutput);
                }
                if (tappingRequired == 2 && tappingStartTime > 0
                    && ((millis() - tappingStartTime) > tappingDuration * 1000)) {
                    noInterrupts();
                    digitalWrite(RELAY_OUT_POWER_TAP, LOW);
                    digitalWrite(RELAY_OUT_POWER_TAP2, HIGH);
                    // Serial.println("Tapping finished");
                    tappingStartTime = 0;
                    // delay(20);
                    interrupts();
                }
            }
        }

        if (relayState == 0 && buttonOffPressed) {
            uint8_t pinState = digitalRead(RELAY_OUT);
            if (pinState) {
                logAction(ModeOfAction::ABNORMAL_CASE_DIFF_IN_STATUS, 0, 0, 0, ScheduleType::SCH_NONE);
                handleRelayOFF('M', curDateTime);
                manualOff = 1;
                lastManualOffTime = curDateTime;
                resetButtonPress();
            }
        }
        // Serial.println("manualOff: " + String(manualOff));
        // if (!scheduledTime && (curDateTime >= nextStart) && (curDateTime < nextEnd)) {
        if (!scheduledTimeOuter && relayState == 0) {
            if (manualOff != 0) {
                manualOff = 0;
                scheduleChanged = true;
                // Serial.println("manualOff=0");
            }
        } else if (!scheduledTime && relayState == 0) {

            lowVoltageAlertLogged = false;
            highVoltageAlertLogged = false;
            phaseAlertLogged = false;
            if (manualOff == 4 || manualOff == 3 || manualOff == 5 || manualOff == 6) {
                if (manualOff == 6) {
                    leakageAlertState = false;
                    Serial.println("leakageAlertState set to false 111111");
                }
                manualOff = 0;
            }
        }
        if ((curDateTime >= nextEnd) && relayState == 0) {
            //  Serial.println("inside curDateTime >= nextEnd , setting schedule changed =true");
            //  Serial.println(formatDateTime(curDateTime));
            //  Serial.println(formatDateTime(nextEnd));
            scheduleChanged = true;
        }
        // Serial.println("end of  loop");
        // Serial.println("duration for a loop: " + String(currentMillis -
        // previousMillis));
        previousMillis = currentMillis;
        // resetButtonPress();
    }
}
