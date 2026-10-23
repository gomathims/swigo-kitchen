#include "preference_management.h"
#include "power_monitoring.h"
#include "build_info.h"

Preferences preferences;

void clearTempScheduleDetails() {
    preferences.begin("temp-sch", false);
    tempEvent.startTime = DateTime();
    tempEvent.endTime = DateTime();
    tempEvent.scheduleType = ScheduleType::TEMPORARY;
    preferences.putBool("isTempActive", false);
    preferences.clear();
    preferences.end();
}

void initPreferences() {
    preferences.begin("settings", true);
    isFirstTime = preferences.getBool("isFirstTime", true);
    isScheduleSet = preferences.getBool("isScheduleSet", false);
    isNameSet = preferences.getBool("isNameSet", false);
    isAPPassSet = preferences.getBool("isAPPassSet", false);
    isWiFiSet = preferences.getBool("isWiFiSet", false);
    isPLimitSet = preferences.getBool("isPLimitSet", false);
    // RTC backup-battery health
    rtcBatteryLow = preferences.getBool("rtcBattLow", false);
    Serial.println("battery status got from NVS :");
    Serial.println(rtcBatteryLow);

    rtcLostPowerCount = preferences.getInt("rtcLostPwrCnt", 0);
    rtcOsfBaselined = preferences.getBool("rtcBaselined", false);
    isFloatEnabled = preferences.getUShort("floatEnabled", 0);
    Serial.println("isFloatEnabled: " + String(isFloatEnabled));
    
    // Load flow detection delays (stored in seconds, default to 10 seconds)
    flowDetectionDelaySec = preferences.getUShort("flowDetectDelay", 10);
    flowStopDelaySec = preferences.getUShort("flowStopDelay", 10);

    {
        String stored = preferences.getString("deviceName", DEFAULT_DEVICE_NAME);
        strncpy(switchName, stored.c_str(), 20);
        switchName[20] = '\0';
    }
    deviceTZOffset = preferences.getInt("tzOffset", TIMEZONE_OFFSET);
    strcpy(devicePin, preferences.getString("devicePin", DEFAULT_DEVICE_PIN).c_str());
    if (isAPPassSet) {
        String stored = preferences.getString("ap-pass", "");
        strncpy(apPassBuffer, stored.c_str(), 10);
        apPassBuffer[10] = '\0';
    }

    if (isWiFiSet) {
        {
            String stored = preferences.getString("ssid", "");
            strncpy(ssidBuffer, stored.c_str(), 20);
            ssidBuffer[20] = '\0';
        }
        {
            String stored = preferences.getString("wifi-pass", "");
            strncpy(wifiPassBuffer, stored.c_str(), 20);
            wifiPassBuffer[20] = '\0';
        }
        staticIP.fromString(preferences.getString("staticIP", "0.0.0.0").c_str());
        gateway.fromString(preferences.getString("gateway", "0.0.0.0").c_str());
        subnet.fromString(preferences.getString("subnet", "0.0.0.0").c_str());
        primaryDNS.fromString(preferences.getString("pDNS", "8.8.8.8").c_str());
        secondaryDNS.fromString(preferences.getString("sDNS", "1.1.1.1").c_str());
    }
    preferences.end();
}

void resetPreferences() {
    preferences.begin("settings", false);
    preferences.clear();
    preferences.end();

    preferences.begin("cl-sync", false);
    preferences.clear();
    preferences.end();

    clearTempScheduleDetails();

    preferences.begin("schedule", false);
    preferences.clear();
    preferences.end();
}

void savePreference(char *key, bool value) {
    preferences.begin("settings", false);
    preferences.putBool(key, value);
    preferences.end();
}

void saveRtcHealth() {
    preferences.begin("settings", false);
    preferences.putBool("rtcBattLow", rtcBatteryLow);
    preferences.putInt("rtcLostPwrCnt", rtcLostPowerCount);
    preferences.putBool("rtcBaselined", rtcOsfBaselined);
    preferences.end();
}

bool readPreference(char *key) {
    preferences.begin("settings", true);
    bool value = preferences.getBool(key, false);
    preferences.end();
    return value;
}

void saveDeviceDetails() {
    preferences.begin("settings", false);
    String curValue = preferences.getString("deviceName", "");
    if (strcmp(curValue.c_str(), switchName) != 0) {
        preferences.putString("deviceName", switchName);
    }
    curValue = preferences.getString("devicePin", "");
    if (strcmp(curValue.c_str(), devicePin) != 0) {
        preferences.putString("devicePin", devicePin);
    }

    int curTzOffset = preferences.getInt("tzOffset", -1);
    if (curTzOffset != deviceTZOffset) {
        preferences.putInt("tzOffset", deviceTZOffset);
    }

    bool curIsFirstTime = preferences.getBool("isFirstTime", true);
    if (curIsFirstTime != isFirstTime) {
        preferences.putBool("isFirstTime", isFirstTime);
    }

    bool curIsNameSet = preferences.getBool("isNameSet", false);
    if (curIsNameSet != isNameSet) {
        preferences.putBool("isNameSet", isNameSet);
    }
    preferences.end();
}

void saveFloatDetails(uint8_t paraIsFloatEnabled, uint16_t flowDetectDelay, uint16_t flowStopDelay) {
    preferences.begin("settings", false);
    uint8_t curIsFloatEnabled = preferences.getUShort("floatEnabled", 0);
    Serial.println("inside saveFloatDetails preferences");

    if (curIsFloatEnabled != paraIsFloatEnabled) {
        preferences.putUShort("floatEnabled", paraIsFloatEnabled);
    }
    
    // Save flow detection delays (in seconds)
    preferences.putUShort("flowDetectDelay", flowDetectDelay);
    preferences.putUShort("flowStopDelay", flowStopDelay);
    
    preferences.end();
}

void saveWifiSSIDPAss() {
    preferences.begin("settings", false);
    String curValue = preferences.getString("ssid", "");
    if (strcmp(curValue.c_str(), ssidBuffer) != 0) {
        preferences.putString("ssid", ssidBuffer);
    }

    curValue = preferences.getString("wifi-pass", "");
    if (strcmp(curValue.c_str(), wifiPassBuffer) != 0) {
        preferences.putString("wifi-pass", wifiPassBuffer);
    }
    preferences.end();
}

void saveWifiDetails() {
    preferences.begin("settings", false);
    String curValue = preferences.getString("ssid", "");
    if (strcmp(curValue.c_str(), ssidBuffer) != 0) {
        preferences.putString("ssid", ssidBuffer);
    }

    curValue = preferences.getString("wifi-pass", "");
    if (strcmp(curValue.c_str(), wifiPassBuffer) != 0) {
        preferences.putString("wifi-pass", wifiPassBuffer);
    }

    curValue = preferences.getString("staticIP", "");

    if (strcmp(curValue.c_str(), staticIP.toString().c_str()) != 0) {
        preferences.putString("staticIP", staticIP.toString());
    }

    curValue = preferences.getString("gateway", "");
    if (strcmp(curValue.c_str(), gateway.toString().c_str()) != 0) {
        preferences.putString("gateway", gateway.toString());
    }

    curValue = preferences.getString("subnet", "");
    if (strcmp(curValue.c_str(), subnet.toString().c_str()) != 0) {
        preferences.putString("subnet", subnet.toString());
    }

    curValue = preferences.getString("pDNS", "");
    if (strcmp(curValue.c_str(), primaryDNS.toString().c_str()) != 0) {
        preferences.putString("pDNS", primaryDNS.toString());
    }

    curValue = preferences.getString("sDNS", "");
    if (strcmp(curValue.c_str(), secondaryDNS.toString().c_str()) != 0) {
        preferences.putString("sDNS", secondaryDNS.toString());
    }

    bool curIsWiFiSet = preferences.getBool("isWiFiSet", false);
    if (curIsWiFiSet != isWiFiSet) {
        preferences.putBool("isWiFiSet", isWiFiSet);
    }

    preferences.end();
}


void saveApPassDetails() {
    preferences.begin("settings", false);
    
    String curValue = preferences.getString("ap-pass", DEFAULT_AP_PASSWORD);
    if (strcmp(curValue.c_str(), apPassBuffer) != 0) {
        preferences.putString("ap-pass", apPassBuffer);
    }

    bool curIsAPPassSet = preferences.getBool("isAPPassSet", false);
    if (curIsAPPassSet != isAPPassSet) {
        preferences.putBool("isAPPassSet", isAPPassSet);
    }
    preferences.end();
}

void readEnergyData(unsigned long &relayStartTS, unsigned long &currentTS, float &energyConsumed) {
    preferences.begin("energy-data", true);
    relayStartTS = preferences.getLong("relayStartTS", 0);
    currentTS = preferences.getLong("currentTS", 0);
    energyConsumed = preferences.getFloat("energyConsumed", 0);
    cumulativeEnergy = preferences.getFloat("cumEnergy", 0);
    // Heal devices that persisted a negative total before the
    // energyToOutput-based guards were added.
    if (energyConsumed < 0) {
        energyConsumed = 0;
    }
    if (cumulativeEnergy < 0) {
        cumulativeEnergy = 0;
    }
    preferences.end();
}

void initiateTempEnergyData(unsigned long relayStartTS) {
    preferences.begin("energy-data", false);
    preferences.putLong("relayStartTS", relayStartTS);
    preferences.putLong("currentTS", relayStartTS);
    preferences.putFloat("energyConsumed", 0);
    preferences.end();
}

void updateTempEnergyData(unsigned long currentTS, float energyConsumed, float cumulativeEnergy) {
    preferences.begin("energy-data", false);
    preferences.putLong("currentTS", currentTS);
    preferences.putFloat("energyConsumed", energyConsumed);
    preferences.putFloat("cumEnergy", cumulativeEnergy);
    preferences.end();
}

void resetTempEnergyData() {
    preferences.begin("energy-data", false);
    preferences.putLong("relayStartTS", 0);
    preferences.putLong("currentTS", 0);
    preferences.putFloat("energyConsumed", 0);
    preferences.end();
}

void resetEnergyData() {
    preferences.begin("energy-data", false);
    preferences.clear();
    preferences.end();
}

void readCloudSyncDetails() {
    preferences.begin("cl-sync", true);
    isCloudConnected = preferences.getBool("syncEnabled", false);
    String token = preferences.getString("token", "");
    String deviceId = preferences.getString("deviceId", "");
    strcpy(token_buffer, token.c_str());
    strcpy(deviceId_buffer, deviceId.c_str());
    preferences.end();
}

void updateCloudSyncDetails(bool syncEnabled, String token, String deviceId) {
    preferences.begin("cl-sync", false);
    preferences.putBool("syncEnabled", syncEnabled);
    preferences.putString("token", token);
    preferences.putString("deviceId", deviceId);
    isCloudConnected = syncEnabled;
    strcpy(token_buffer, token.c_str());
    strcpy(deviceId_buffer, deviceId.c_str());
    preferences.end();
}

void saveMQTTSettings(const char *broker, int port, const char *username, const char *password,
                      const char *publishTopic, const char *subscribeTopic, const char *type) {
    preferences.begin("cl-sync", false);
    preferences.putBool("sync2Enabled", true);
    preferences.putString("broker", broker);
    preferences.putInt("port", port);
    preferences.putString("username", username);
    preferences.putString("password", password);
    preferences.putString("publishTopic", publishTopic);
    preferences.putString("subscribeTopic", subscribeTopic);
    preferences.putString("type", type);
    preferences.end();
}

bool readGeneralMQTTSettings(String &broker, int &port, String &username, String &password, String &publishTopic,
                             String &subscribeTopic, String &type) {
    preferences.begin("cl-sync", true);
    if (!preferences.getBool("sync2Enabled", false)) {
        preferences.end();
        return false;
    }
    broker = preferences.getString("broker", "");
    port = preferences.getInt("port", 1883);
    username = preferences.getString("username", "");
    password = preferences.getString("password", "");
    publishTopic = preferences.getString("publishTopic", "");
    subscribeTopic = preferences.getString("subscribeTopic", "");
    type = preferences.getString("type", "");
    preferences.end();
    return true;
}

bool readTempScheduleDetails(SwitchingEvent &tempEvent, char &varRunType) {
    preferences.begin("temp-sch", true);
    if (preferences.getBool("isTempActive", false)) {
        tempEvent.startTime = DateTime(preferences.getLong64("startTime", 0));
        tempEvent.endTime = DateTime(preferences.getLong64("endTime", 0));
        tempEvent.scheduleType = ScheduleType::SCH_NONE;
        varRunType = preferences.getChar("runType", '0');
        preferences.end();
        return false;
    } else {
        tempEvent.startTime = DateTime();
        tempEvent.endTime = DateTime();
        tempEvent.scheduleType = ScheduleType::SCH_NONE;
        varRunType = '0';
        preferences.end();
        return true;
    }
}

void updateTempScheduleDetails(SwitchingEvent tempEvent, char varRunType) {
    preferences.begin("temp-sch", false);
    preferences.putBool("isTempActive", true);
    preferences.putLong64("startTime", (tempEvent.startTime).unixtime());
    preferences.putLong64("endTime", (tempEvent.endTime).unixtime());
    preferences.putChar("runType", varRunType);
    preferences.end();
}


/***********************  Admin config in Preferences ******************** */

void readButtonTimingDetails(TimerConfig &timingConfig, unsigned long &button1Dur, unsigned long &button2Dur,
                             unsigned long &button3Dur, unsigned long &button4Dur) {
    preferences.begin("Admin-config", true);
    timingConfig = (TimerConfig)(uint8_t)preferences.getUShort("timingConfig", TimerConfig::TIMERCONFIG_5M);
    if (timingConfig == TimerConfig::TIMERCONFIG_CUSTOM) {
        button1Dur = preferences.getULong("button1Duration", 0);
        button2Dur = preferences.getULong("button2Duration", 0);
        button3Dur = preferences.getULong("button3Duration", 0);
        button4Dur = preferences.getULong("button4Duration", 0);
    }
    preferences.end();
}

void readButtonTimingString(char *formattedStr, size_t maxLength) {
    preferences.begin("Admin-config", true);
    String storedStr = preferences.getString("buttonTimingStr", "");
    strncpy(formattedStr, storedStr.c_str(), maxLength);
    preferences.end();
}

void updateButtonTimingDetails(TimerConfig timingConfig, unsigned long button1Duration, unsigned long button2Duration,
                               unsigned long button3Duration, unsigned long button4Duration, const char *formattedStr) {
    preferences.begin("Admin-config", false);
    preferences.putUShort("timingConfig", (uint16_t)timingConfig);
    preferences.putULong("button1Duration", button1Duration);
    preferences.putULong("button2Duration", button2Duration);
    preferences.putULong("button3Duration", button3Duration);
    preferences.putULong("button4Duration", button4Duration);
    preferences.putString("buttonTimingStr", formattedStr);
    preferences.end();
}

// void save3PhaseSettings(bool threePhase, uint8_t tappingRequired, uint8_t tappingDuration, bool ethernetEnabled, uint8_t ledType, String shuntOrCT) {
void save3PhaseSettings(bool threePhase, uint8_t tappingRequired, uint8_t tappingDuration, bool ethernetEnabled, uint8_t ledType, String shuntOrCT, bool leakDetectionEnabled, uint8_t inRushSecs, bool vsmrLinkEnabled, String vsmrMac) {
    preferences.begin("Admin-config", false);
    preferences.putBool("threePhase", threePhase);
    preferences.putUShort("tappingRequired", tappingRequired);
    preferences.putUShort("tappingDuration", tappingDuration);
    preferences.putBool("ethernetEnabled", ethernetEnabled);
    preferences.putUShort("ledType", ledType);
    preferences.putString("shuntOrCT", shuntOrCT);
    preferences.putBool("leakDetEnabled", leakDetectionEnabled);
    preferences.putUChar("inRush", inRushSecs);
    preferences.putBool("vsmrLink", vsmrLinkEnabled);   // ESP-NOW VSMR downlink
    preferences.putString("vsmrMac", vsmrMac);          // paired VSMR MAC filter
    preferences.end();
}
void saveMasterRemoteSettings(uint8_t masterSlaveMode, String masterDeviceUrl, String mPin, String remoteMethods) {
    preferences.begin("Admin-config", false);
    preferences.putUShort("masterSlaveMode", masterSlaveMode);
    preferences.putString("masterDeviceUrl", masterDeviceUrl);
    preferences.putString("mPin", mPin);
    preferences.putString("remoteMethods", remoteMethods);
    preferences.end();
}

void read3PhaseSettings(bool &threePhase, uint8_t &tappingRequired, uint8_t &tappingDuration, bool &ethernetEnabled, uint8_t &ledType, String &shuntOrCT, uint8_t &masterSlaveMode, String &masterDeviceUrl, String &mPin, String &remoteMethods, bool &leakDetectionEnabled, uint8_t &inRushSecs, bool &vsmrLinkEnabled, String &vsmrMac) {
    preferences.begin("Admin-config", true);
    threePhase = preferences.getBool("threePhase", false);
    tappingRequired = preferences.getUShort("tappingRequired", 0);
    tappingDuration = preferences.getUShort("tappingDuration", 0);
    ethernetEnabled = preferences.getBool("ethernetEnabled", false);
    ledType = preferences.getUShort("ledType", 2);
    shuntOrCT = preferences.getString("shuntOrCT", "S");
    masterSlaveMode = preferences.getUShort("masterSlaveMode", 0);
    masterDeviceUrl = preferences.getString("masterDeviceUrl", "");
    mPin = preferences.getString("mPin", "");
    remoteMethods = preferences.getString("remoteMethods", "");
    leakDetectionEnabled = preferences.getBool("leakDetEnabled", false);
    inRushSecs = preferences.getUChar("inRush", 0);
    vsmrLinkEnabled = preferences.getBool("vsmrLink", false);   // ESP-NOW VSMR downlink
    vsmrMac = preferences.getString("vsmrMac", "");             // paired VSMR MAC filter
    Serial.println("ethernetEnabled in read3PhaseSettings: " + String(ethernetEnabled));
    preferences.end();
}

/*********************** Schedule in Preferences ******************** */

void saveScheduleToPreferences(uint8_t numObjects) {
    preferences.begin("schedule", false);
    preferences.putUChar("count", numObjects);
    for (uint8_t i = 0; i < numObjects; i++) {
        TimeSchedule timeSchedule = gScheduleArray[i];
        char keyStartHour[15];
        char keyStartMin[15];
        char keyEndHour[15];
        char keyEndMin[15];
        char keyDetails[15];
        char keyDays[15];
        // New cyclic fields
        char keyOnDurH[15];
        char keyOnDurM[15];
        char keyOffDurH[15];
        char keyOffDurM[15];
        sprintf(keyStartHour, "sh%d", i);
        sprintf(keyStartMin, "sm%d", i);
        sprintf(keyEndHour, "eh%d", i);
        sprintf(keyEndMin, "em%d", i);
        sprintf(keyDetails, "dt%d", i);
        sprintf(keyDays, "dy%d", i);
        sprintf(keyOnDurH, "onH%d", i);
        sprintf(keyOnDurM, "onM%d", i);
        sprintf(keyOffDurH, "offH%d", i);
        sprintf(keyOffDurM, "offM%d", i);
        preferences.putUChar(keyStartHour, timeSchedule.startHour);
        preferences.putUChar(keyStartMin, timeSchedule.startMinute);
        preferences.putUChar(keyEndHour, timeSchedule.endHour);
        preferences.putUChar(keyEndMin, timeSchedule.endMinute);
        preferences.putUChar(keyDetails, timeSchedule.details);
        preferences.putUChar(keyDays, timeSchedule.days);
        // Save new cyclic fields
        preferences.putUChar(keyOnDurH, timeSchedule.onDurHrs);
        preferences.putUChar(keyOnDurM, timeSchedule.onDurMins);
        preferences.putUChar(keyOffDurH, timeSchedule.offDurHrs);
        preferences.putUChar(keyOffDurM, timeSchedule.offDurMins);
    }
    preferences.end();
}

void loadScheduleFromPreferences() {
    preferences.begin("schedule", true);
    uint8_t numObjects = preferences.getUChar("count", 0);
    for (uint8_t i = 0; i < numObjects; i++) {
        TimeSchedule timeSchedule;
        char keyStartHour[15];
        char keyStartMin[15];
        char keyEndHour[15];
        char keyEndMin[15];
        char keyDetails[15];
        char keyDays[15];
        // New cyclic fields
        char keyOnDurH[15];
        char keyOnDurM[15];
        char keyOffDurH[15];
        char keyOffDurM[15];
        sprintf(keyStartHour, "sh%d", i);
        sprintf(keyStartMin, "sm%d", i);
        sprintf(keyEndHour, "eh%d", i);
        sprintf(keyEndMin, "em%d", i);
        sprintf(keyDetails, "dt%d", i);
        sprintf(keyDays, "dy%d", i);
        sprintf(keyOnDurH, "onH%d", i);
        sprintf(keyOnDurM, "onM%d", i);
        sprintf(keyOffDurH, "offH%d", i);
        sprintf(keyOffDurM, "offM%d", i);
        timeSchedule.startHour = preferences.getUChar(keyStartHour, 0);
        timeSchedule.startMinute = preferences.getUChar(keyStartMin, 0);
        timeSchedule.endHour = preferences.getUChar(keyEndHour, 0);
        timeSchedule.endMinute = preferences.getUChar(keyEndMin, 0);
        timeSchedule.details = preferences.getUChar(keyDetails, 0);
        timeSchedule.days = preferences.getUChar(keyDays, 0);
        // Load new cyclic fields
        timeSchedule.onDurHrs = preferences.getUChar(keyOnDurH, 0);
        timeSchedule.onDurMins = preferences.getUChar(keyOnDurM, 0);
        timeSchedule.offDurHrs = preferences.getUChar(keyOffDurH, 0);
        timeSchedule.offDurMins = preferences.getUChar(keyOffDurM, 0);
        gScheduleArray[i] = timeSchedule;
    }
    gScheduleCount = numObjects;
    preferences.end();
}

void versionSetup() {
    // Open preferences in read/write mode
    // preferences.begin("firmware-info", false);
    // Store firmware version details
    // preferences.putUInt("major", FIRMWARE_MAJOR);
    // preferences.putUInt("minor", FIRMWARE_MINOR);
    // preferences.putUInt("patch", FIRMWARE_PATCH);
    // #ifdef BUILD_NUMBER
    // preferences.putUInt("build", BUILD_NUMBER);
    // #else
    // preferences.putUInt("build", 0);
    // #endif
    // preferences.end();
}

String getFirmwareVersion() {
    String version = String(FIRMWARE_MAJOR) + "." + String(FIRMWARE_MINOR) + "." + String(FIRMWARE_PATCH);

    if (strcmp((const char *)"B", (char *)PROD_BETA) == 0) {
        #ifdef BUILD_NUMBER
            version += " Build - " + String(BUILD_NUMBER);
        #endif
        version += " Beta";
    }
    return version;
}

String getFirmwareVersionToLog() {
    String version = String(FIRMWARE_MAJOR) + "." + String(FIRMWARE_MINOR) + "." + String(FIRMWARE_PATCH);
    #ifdef BUILD_NUMBER
        version += "." + String(BUILD_NUMBER);
    #endif
   
    return version;
}

void savePreviousFirmwareVersion() {
    preferences.begin("firmware-info", false);
    preferences.putString("prev_version", getFirmwareVersionToLog());
    preferences.end();
}

String getPreviousFirmwareVersion() {
    preferences.begin("firmware-info", true);
    // Auto upgrade options started after 1.7.2 prod version only. Prior to that upgrade is a manual effort or a fresh install.
    String previousVersion = preferences.getString("prev_version", "1.7.2");
    preferences.end();
    return previousVersion;
}

bool isFirmwareVersionChanged() {
    String currentVersion = getFirmwareVersionToLog();
    String previousVersion = getPreviousFirmwareVersion();
    return (currentVersion != previousVersion);
}

bool shouldMigrateLogs() {
    // String previousVersion = getPreviousFirmwareVersion();
    // Serial.println("Inside shouldMigrateLogs");
    // Serial.println("previousVersion");
    // Serial.println(previousVersion);

    // Check for the presence of old log files to determine if migration is needed
    bool hasOldOperationLogs = LittleFS.exists("/logs/gen2-log.bin") && LittleFS.exists("/logs/pointers.bin");
    bool hasOldDebugLogs = LittleFS.exists("/logs/gen2-debug.bin") && LittleFS.exists("/logs/debug-pointers.bin");
    bool hasOldEnergyLogs = LittleFS.exists("/logs/gen2-energy.bin") && LittleFS.exists("/logs/energy-pointers.bin");
    
    // Migration needed if any old log files exist
    bool migrationNeeded = hasOldOperationLogs || hasOldDebugLogs || hasOldEnergyLogs;
    
    if (migrationNeeded) {
        Serial.println("=== OLD LOG FILES DETECTED - MIGRATION NEEDED ===");
        if (hasOldOperationLogs) Serial.println("Found old operation log files");
        if (hasOldDebugLogs) Serial.println("Found old debug log files");
        if (hasOldEnergyLogs) Serial.println("Found old energy log files");
    } else {
        Serial.println("No old log files found - migration not needed");
    }
    
    return migrationNeeded;
}