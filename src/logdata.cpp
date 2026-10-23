#include "logdata.h"
#include "logging_manager.h"
#include "globals.h"
#include "logdebug.h"
#include "preference_management.h"

// Global variables
std::unordered_map<ModeOfAction, String> modeMessages = {
    {START_WEB, "Started via Web"},
    {START_SCHEDULED, "Started at scheduled time"},
    // No "alert"/"fault"/"fail" in the wording: both UIs colour a log red purely
    // on those substrings, and this is a normal start, not a fault.
    {START_POWER_RESUME, "Resumed after power/runtime break or trip"},
    {START_MANUAL_1, "Started with Button 1"},
    {START_MANUAL_2, "Started with Button 2"},
    {START_MANUAL_3, "Started with Button 3"},
    {START_MANUAL_4, "Started with Button 4"},
    {STOP_TIMER, "Stopped - Timer countdown 0"},
    {STOP_MANUAL, "Stopped manually"},
    {STOP_SCHEDULED, "Stopped at scheduled time"},
    {STOP_WEB, "Stopped via Web"},
    {STOP_POWER_DOWN, "Stopped due to power down"},
    {STOP_DRYRUN, "Stopped due to dry run alert"},
    {STOP_OVERRUN, "Stopped due to overload alert"},
    {STOP_LOWVOLTAGE, "Stopped due to low voltage alert"},
    {STOP_HIGHVOLTAGE, "Stopped due to high voltage alert"},
    {STOP_FLOAT, "Stopped due to float alert"},
    {STOP_PHASE_FAILURE, "Stopped due to phase failure alert"},
    {STOP_PHASE_SEQUENCE_REVERSE, "Stopped due to phase sequence reverse alert"},
    {RESTART_ESP, "Restarted SwiGO SuperSmart"},
    {START_MQTT, "Started from Remote App"},
    {STOP_MQTT, "Stopped from Remote App"},
    {START_MQTT_FLOAT_REJECT, "Started from Remote App, but failed as tank full"},
    {START_MQTT_HV_REJECT, "Started from Remote App, but failed due to prevailing high voltage"},
    {START_MQTT_LV_REJECT, "Started from Remote App, but failed due to prevailing low voltage"},
    {START_MQTT_DURATION_REJECT, "Start from Remote App did not succeed"},
    {START_CYCLIC, "Started a Cyclic Schedule"},

    {START_WEB_FLOAT_REJECT, "Started via Web, but failed as tank full"},
    {START_SCHEDULED_FLOAT_REJECT, "Started at scheduled time, but failed as tank full"},
    {START_POWER_RESUME_FLOAT_REJECT, "Started after power resumption, but failed as tank full"},
    {START_MANUAL_1_FLOAT_REJECT, "Started with Button 1, but failed as tank full"},
    {START_MANUAL_2_FLOAT_REJECT, "Started with Button 2, but failed as tank full"},
    {START_MANUAL_3_FLOAT_REJECT, "Started with Button 3, but failed as tank full"},
    {START_MANUAL_4_FLOAT_REJECT, "Started with Button 4, but failed as tank full"},
    {START_WEB_HV_REJECT, "Started via Web, but failed due to prevailing high voltage"},
    {START_SCHEDULED_HV_REJECT, "Started at scheduled time, but failed due to prevailing high voltage"},
    {START_POWER_RESUME_HV_REJECT, "Started after power resumption, but failed due to prevailing high "
                                   "voltage"},
    {START_MANUAL_1_HV_REJECT, "Started with Button 1, but failed due to prevailing high voltage"},
    {START_MANUAL_2_HV_REJECT, "Started with Button 2, but failed due to prevailing high voltage"},
    {START_MANUAL_3_HV_REJECT, "Started with Button 3, but failed due to prevailing high voltage"},
    {START_MANUAL_4_HV_REJECT, "Started with Button 4, but failed due to prevailing high voltage"},
    {START_WEB_LV_REJECT, "Started via Web, but failed due to prevailing low voltage"},
    {START_SCHEDULED_LV_REJECT, "Started at scheduled time, but failed due to prevailing low voltage"},
    {START_POWER_RESUME_LV_REJECT, "Started after power resumption, but failed due to prevailing low "
                                   "voltage"},
    {START_MANUAL_1_LV_REJECT, "Started with Button 1, but failed due to prevailing low voltage"},
    {START_MANUAL_2_LV_REJECT, "Started with Button 2, but failed due to prevailing low voltage"},
    {START_MANUAL_3_LV_REJECT, "Started with Button 3, but failed due to prevailing low voltage"},
    {START_MANUAL_4_LV_REJECT, "Started with Button 4, but failed due to prevailing low voltage"},
    {PHASE_SEQUENCE_REVERSE_ALERT, "Phase Sequence Reverse Alert"},
    {PHASE_FAILURE_ALERT, "Phase Failure Alert"},
    {PHASE_SEQUENCE_REVERSE_ALERT_CLEARED, "Phase Sequence Reverse Alert Cleared"},
    {PHASE_FAILURE_ALERT_CLEARED, "Phase Failure Alert Cleared"},
    {START_FLOW_SENSOR, "Started, Flow detected"},
    {STOP_FLOW_SENSOR, "Stopped, Flow stopped"},
    {FLOW_NOT_DETECTED, "Flow is Not Detected - Start Failed"},
    {START_WEBHOOK, "Started from Remote Switch"},
    {STOP_WEBHOOK, "Stopped from Remote Switch"},
    {START_MASTER_BUTTON_PRESS_SUCCESS, "Start command sent to Master successfully"},
    {STOP_MASTER_BUTTON_PRESS_SUCCESS, "Stop command sent to Master successfully"},
    {START_MASTER_BUTTON_PRESS_SENDING_FAILED, "Start command sending failed"},
    {STOP_MASTER_BUTTON_PRESS_SENDING_FAILED, "Stop command sending failed"},
    {START_MASTER_BUTTON_PRESS_ACTION_FAILED, "'Start command sent: Start failed as some fault condition occured in Master device"},
    {STOP_MASTER_BUTTON_PRESS_ACTION_FAILED, "Stop command sent: Master switch already OFF"},
    {START_MASTER_BUTTON_PRESS_ALREADY_ON, "Start command sent: Master switch already ON"},
    {STOP_MAXTIME_BREAK, "Stopped, Max run time reached"},
    {START_MASTER_MQTT_SUCCESS, "Start command sent to Master via Cloud"},
    {STOP_MASTER_MQTT_SUCCESS, "Stop command sent to Master via Cloud"},
    {FLOW_DETECTED, "Flow detected"},
    {FLOW_STOPPED, "Flow stopped"},
    {START_MASTER_TANK_FULL_REJECT, "Tank is full, Start command not sent to Master"},
    {START_MASTER_FLOW_REJECT, "Flow not detected, Start command not sent to Master"},
    {FORCE_START_FLOW, "Force start in Flow-mode"},
    {LEAKAGE_DETECTED, "Current leak detected"},
    {PHASE_FAILURE_START_REJECT, "Phase failure - Start failed"},
    {PHASE_SEQUENCE_REVERSE_START_REJECT, "Phase sequence reverse - Start failed"},
    {STOP_DURATION_COMPLETED, "Stopped, Scheduled duration completed"},
};

const char *dirPath = "/logs";
const char *wwwDirPath = "/www";
const char *logFileName = "/logs/gen2-log.bin";
const char *pointersFileName = "/logs/pointers.bin";
Pointers pointers = {0, 0, 0};

// New variables for multi-file logging
// time_t currentOperationFile = 0;
// time_t previousOperationFile = 0;
// #define LOG_FILE_SIZE 1024  // 1KB
//#define LOG_TYPE_OPERATION 1

// Function implementations
void littleFSSetup() {
    if (!LittleFS.begin(true, "/littlefs", 10, "log_fs")) {
        Serial.println("Failed to mount log_fs partition");
        return;
    }

    if (!LittleFS.exists(dirPath)) {
        if (!LittleFS.mkdir(dirPath)) {
            Serial.println("Failed to create directory!");
            return;
        }
    }

    // Clean up old /www/ files from previous firmware that used LittleFS for web content
    if (LittleFS.exists("/www")) {
        Serial.println("[FS] Removing old /www/ web files from LittleFS...");
        File wwwDir = LittleFS.open("/www");
        if (wwwDir && wwwDir.isDirectory()) {
            File entry = wwwDir.openNextFile();
            while (entry) {
                String fname = String("/www/") + entry.name();
                entry.close();
                LittleFS.remove(fname);
                Serial.printf("[FS] Removed %s\n", fname.c_str());
                entry = wwwDir.openNextFile();
            }
            wwwDir.close();
        }
        LittleFS.rmdir("/www");
        Serial.printf("[FS] /www/ cleanup done. Free: %u bytes\n", LittleFS.totalBytes() - LittleFS.usedBytes());
    }

    // Check for old log files and migrate if needed
    Serial.println("=== Checking for old log files ===");
    String migrationDetails = "";
    bool migrationNeeded = shouldMigrateLogs();
    
    if (migrationNeeded) {
        Serial.println("=== FIRMWARE UPGRADE DETECTED - MIGRATING LOGS ===");
        Serial.print("Previous version: ");
        Serial.println(getPreviousFirmwareVersion());
        Serial.print("Current version: ");
        Serial.println(getFirmwareVersion());
        
        // Perform migration and capture details
        migrationDetails = migrateOldLogFiles();
        
        Serial.println("=== LOG MIGRATION COMPLETED ===");
    } else {
        Serial.println("No migration needed - using existing log structure");
    }
    
    // Initialize multi-file logging system for all log types
    initializeLogFileTracking(LOG_TYPE_OPERATION);
    initializeLogFileTracking(LOG_TYPE_DEBUG);
    initializeLogFileTracking(LOG_TYPE_ENERGY);
    
    // Add debug entry with migration details if migration occurred
    if (migrationNeeded && migrationDetails.length() > 0) {
        String debugMessage = "Log migration completed. Details: " + migrationDetails;
        logDebugAction(DebugAction::SYSTEM_INFO, (char*)debugMessage.c_str());
    }
    
}

void clearLogs() {
    // Use the unified logging manager to clear operation logs
    clearLogsByType(LOG_TYPE_OPERATION);
}

void logAction(ModeOfAction mode, float current, float voltage, uint16_t duration, ScheduleType scheduleType,
               time_t ts) {
    // File logFile = LittleFS.open(logFileName, "r+");
    // if (!logFile) {
    //     Serial.println("Failed to open log file for appending");
    //     return;
    // }
    // size_t maxEntries = LOG_SIZE_LIMIT / sizeof(LogEntry);
    // size_t nextEnd = pointers.end + sizeof(LogEntry);

    // if (nextEnd + sizeof(LogEntry) >= LOG_SIZE_LIMIT) {
    //     // Wrap around to the beginning
    //     nextEnd = 0;
    // }
    // // Serial.println(nextEnd);
    // LogEntry entry = {};
    // try {
    //     // Write the new entry at the end pointer
    //     logFile.seek(pointers.end);
    //     DateTime now = getDateTimeFromRTC();
    //     time_t timestamp = now.unixtime();
    //     entry = {timestamp, mode, current, voltage};
    //     // logFile.write((uint8_t*)&entry, sizeof(LogEntry));
    //     if (logFile.write((uint8_t *)&entry, sizeof(LogEntry)) != sizeof(LogEntry)) {
    //         Serial.println("Failed to write log entry");
    //         logFile.close();
    //         logDebugAction(DebugAction::SYSTEM_FAILURE, (char *)"Error writing Operation log");
    //         return;
    //     }
    //     logFile.close();
    // } catch (...) {
    //     Serial.println("Failed to write operation log entry");
    //     logDebugAction(DebugAction::SYSTEM_FAILURE, (char *)"Error writing Operation log");
    //     return;
    // }

    // // Update pointers
    // pointers.end = nextEnd;
    // if (pointers.count < maxEntries) {
    //     pointers.count++;
    // } else {
    //     pointers.start = ((pointers.start + sizeof(LogEntry)) > LOG_SIZE_LIMIT) ? 0 : pointers.start + sizeof(LogEntry);
    // }

    // size_t bytes = 0;
    // int retryCount = 3; // Number of retries
    // while (retryCount > 0) {
    //     bytes = savePointers();
    //     if (bytes > 0) {
    //         break;
    //     } else {
    //         Serial.println("Failed to save pointers. Retrying...");
    //         retryCount--;
    //     }
    // }
    // if (bytes == 0) {
    //     Serial.println("Failed to save pointers after retries.");
    //     logDebugAction(DebugAction::SYSTEM_FAILURE, (char *)"Error saving pointers of Operation log");
    // }

    // Use the unified logging system
    queueOperationLog(mode, scheduleType, duration, current, voltage);
    logRefreshRequired = true;
    // if (isCloudConnected || isGeneralMQTTEnabled) {
    //     // queueLogEntryToMQTT(entry);
    //     JsonDocument doc;
    //     doc["ts"] = static_cast<int64_t>(entry.timestamp - deviceTZOffset * 60) * 1000;
    //     auto messageIter = modeMessages.find(entry.mode);
    //     if (messageIter != modeMessages.end()) {
    //         doc["values"]["eventMessage"] = messageIter->second;
    //     } else {
    //         doc["values"]["eventMessage"] = "Unknown mode";
    //     }
    //     doc["values"]["current"] = entry.current;
    //     doc["values"]["voltage"] = entry.voltage;
    //     doc["values"]["relayStatus"] = relayState ? true : false;
    //     ;

    //     char jsonBuffer[256];
    //     serializeJson(doc, jsonBuffer);
    //     // Serial.println(jsonBuffer);

    //     queueMQTTMessage(propTopic, jsonBuffer, false);
    // }
    // Serial.println("logAction end ");
}
void prepareLogsJson(JsonDocument &doc) {
    // Only use currentOperationFile and previousOperationFile
    doc["logArray"].to<JsonArray>();
    JsonArray logArray = doc["logArray"].as<JsonArray>();

    int entriesRead = 0;
    int maxEntries = MAX_LOG_ENTRIES_TO_UI;
    // Helper lambda to read from a file
    auto readLogFile = [&](time_t fileTs) {
        if (fileTs == 0) return;
        String filename = getLogFileName(LOG_TYPE_OPERATION, fileTs);
        File file = LittleFS.open(filename, "r");
        if (!file) return;
        
        // Calculate entry count from file size (no header)
        size_t fileSize = file.size();
        size_t entryCount = fileSize / sizeof(LogEntry);
        
        // Serial.printf("Operation file: %s, %d entries, file size: %d bytes\n", 
        //              filename.c_str(), entryCount, fileSize);
        
        for (int i = entryCount - 1; i >= 0 && entriesRead < maxEntries; i--) {
            file.seek(i * sizeof(LogEntry));
            LogEntry entry;
            file.read((uint8_t *)&entry, sizeof(LogEntry));
            JsonObject logEntry = logArray.add<JsonObject>();
            char timeBuffer[20];
            formatTime(timeBuffer, sizeof(timeBuffer), entry.timestamp);
            logEntry["ts"] = timeBuffer;
            logEntry["md"] = entry.mode;
            logEntry["duration"] = entry.duration;
            if (entry.mode == STOP_DRYRUN || entry.mode == STOP_OVERRUN || entry.mode == STOP_HIGHVOLTAGE
                || entry.mode == STOP_LOWVOLTAGE) {
                logEntry["current"] = entry.current;
                logEntry["voltage"] = entry.voltage;
            }
            entriesRead++;
        }
        file.close();
    };
    readLogFile(currentOperationFile);
    if (entriesRead < maxEntries) {
        // Serial.println("Reading Previous log file...");
        readLogFile(previousOperationFile);
    }
}

// DISABLED - its only caller in main.cpp is commented out, and its body was
// already reduced to forwarding into the (now also disabled) LogType version.
#if 0
void sendAllLogsToMQTT() {
    // File logFile = LittleFS.open(logFileName, "r");
    // if (!logFile) {
    //     Serial.println("Failed to open log file for reading");
    //     return; // Return empty JSON document
    // }

    // if (pointers.count == 0) {
    //     logFile.close();
    //     return; // Return empty JSON document
    // }
    // size_t maxEntries = LOG_SIZE_LIMIT / sizeof(LogEntry);
    // int32_t currentPtr = (pointers.end - sizeof(LogEntry));
    // if (currentPtr < 0) {
    //     // currentPtr += LOG_SIZE_LIMIT;
    //     currentPtr = (maxEntries - 1) * sizeof(LogEntry);
    // }
    // uint8_t countToUI = pointers.count <= MAX_LOG_ENTRIES_TO_SYNC_MQTT ? pointers.count : MAX_LOG_ENTRIES_TO_SYNC_MQTT;

    // for (size_t i = 0; i < countToUI; i++) {
    //     logFile.seek(currentPtr);
    //     LogEntry entry;
    //     logFile.read((uint8_t *)&entry, sizeof(LogEntry));
    //     // queueLogEntryToMQTT(entry);
    //     JsonDocument doc;
    //     doc["ts"] = static_cast<int64_t>(entry.timestamp - deviceTZOffset * 60) * 1000;
    //     String message;
    //     auto messageIter = modeMessages.find(entry.mode);
    //     if (messageIter != modeMessages.end()) {
    //         message = messageIter->second;
    //     } else {
    //         message = "Unknown mode";
    //     }

    //     if (entry.mode == STOP_DRYRUN || entry.mode == STOP_OVERRUN || entry.mode == STOP_HIGHVOLTAGE
    //         || entry.mode == STOP_LOWVOLTAGE) {
    //         message = message + " " + String(entry.current) + "A " + String(entry.voltage) + "V";
    //     }
    //     doc["values"]["eventMessage"] = message;
    //     doc["values"]["current"] = entry.current;
    //     doc["values"]["voltage"] = entry.voltage;

    //     char jsonBuffer[256];
    //     serializeJson(doc, jsonBuffer);
    //     queueMQTTMessage(propTopic, jsonBuffer, false);

    //     currentPtr -= sizeof(LogEntry);

    //     if (currentPtr < 0) {
    //         // currentPtr += LOG_SIZE_LIMIT;
    //         currentPtr = (maxEntries - 1) * sizeof(LogEntry);
    //     }
    // }

    // logFile.close();
    // return;
    sendAllLogsToMQTT(LOG_TYPE_OPERATION);
}
#endif // sendAllLogsToMQTT disabled