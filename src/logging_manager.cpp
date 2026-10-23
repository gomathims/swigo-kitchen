#include "logging_manager.h"
#include "globals.h"

#include <vector>
#include <algorithm>

// Temporary instrumentation: splits each log write into "resolve timestamp"
// (RTC read + rtcMutex wait) vs "flash" (open/rotate/write/close), so the real
// bottleneck is measured rather than guessed. Define LOG_TIMING to enable.
// Remove once the batching question is settled.
#ifdef LOG_TIMING
// Cleared up front so a path that never reads the RTC (operation logs, which
// carry a producer-supplied ts) reports 0 rather than a stale split.
#define LOG_TIMING_BEGIN()   rtcLastMutexWaitUs = 0; rtcLastReadUs = 0; \
                             uint32_t _tStart = micros(); uint32_t _tAfterTs = _tStart
#define LOG_TIMING_MARK_TS() _tAfterTs = micros()
// ts is wall-clock across the timestamp step; mutex/rtc are the two blocking
// parts inside it. ts >> (mutex + rtc) means the task was starved or stalled
// rather than blocked - look outside the logging path (SPI-flash erase, OTA).
#define LOG_TIMING_END(tag)  Serial.printf("[LOGT] %s ts=%luus (mutex=%luus rtc=%luus) flash=%luus total=%luus\n", \
                                 (tag), \
                                 (unsigned long)(_tAfterTs - _tStart), \
                                 (unsigned long)rtcLastMutexWaitUs, \
                                 (unsigned long)rtcLastReadUs, \
                                 (unsigned long)(micros() - _tAfterTs), \
                                 (unsigned long)(micros() - _tStart))
#else
#define LOG_TIMING_BEGIN()   do {} while (0)
#define LOG_TIMING_MARK_TS() do {} while (0)
#define LOG_TIMING_END(tag)  do {} while (0)
#endif

// Global variables for current and previous file timestamps
time_t currentDebugFile = 0;
time_t previousDebugFile = 0;
time_t currentOperationFile = 0;
time_t previousOperationFile = 0;
time_t currentEnergyFile = 0;
time_t previousEnergyFile = 0;

void initializeLogFileTracking(LogType type) {

    // The pointers currentfile and previousFile, helps to keep this function serving to all 3 types
    String prefix;
    time_t *currentFile = nullptr;
    time_t *previousFile = nullptr;
    switch(type) {
        case LOG_TYPE_DEBUG:
            prefix = LOG_FILE_PREFIX_DB;
            currentFile = &currentDebugFile;
            previousFile = &previousDebugFile;
            break;
        case LOG_TYPE_OPERATION:
            prefix = LOG_FILE_PREFIX_OP;
            currentFile = &currentOperationFile;
            previousFile = &previousOperationFile;
            break;
        case LOG_TYPE_ENERGY:
            prefix = LOG_FILE_PREFIX_ED;
            currentFile = &currentEnergyFile;
            previousFile = &previousEnergyFile;
            break;
    }
    *currentFile = 0;
    *previousFile = 0;
    File root = LittleFS.open("/logs");
    File file = root.openNextFile();
    time_t latest = 0, prev = 0;
    while (file) {
        String filename = file.name();
        if (filename.startsWith(prefix + "_")) {
            String tsStr = filename.substring(filename.indexOf('_') + 1, filename.indexOf('.'));
            time_t ts = tsStr.toInt();
            if (ts > latest) {
                prev = latest;
                latest = ts;
            } else if (ts > prev) {
                prev = ts;
            }
        }
        file = root.openNextFile();
    }
    *currentFile = latest;
    *previousFile = prev;
}

// Defined further below; needed by logActionInternal.
bool isStartEvent(ModeOfAction mode);

// Mirrors the isStartEvent list in data/scripts/script.js. Keep in sync.
static bool isStartEventMode(ModeOfAction m) {
    switch (m) {
        case START_WEB:
        case START_SCHEDULED:
        case START_POWER_RESUME:
        case START_MANUAL_1:
        case START_MANUAL_2:
        case START_MANUAL_3:
        case START_MANUAL_4:
        case START_MQTT:
        case START_WEBHOOK:
        case START_CYCLIC:
            return true;
        default:
            return false;
    }
}

String formatOperationLogMessage(const LogEntry &entry) {
    auto it = modeMessages.find(entry.mode);
    String message = (it != modeMessages.end()) ? it->second : String("Unknown mode");

    // Fault-stop modes: append live (V, A) snapshot.
    if (entry.mode == STOP_DRYRUN || entry.mode == STOP_OVERRUN
        || entry.mode == STOP_HIGHVOLTAGE || entry.mode == STOP_LOWVOLTAGE) {
        message = message + " (" + String(entry.voltage) + "V, " + String(entry.current) + "A)";
    }

    // Start events with planned duration: "XH YM" / "XH" / "YM" suffix.
    if (entry.duration > 0 && isStartEventMode(entry.mode)) {
        uint16_t hours = entry.duration / 60;
        uint16_t minutes = entry.duration % 60;
        if (hours > 0 && minutes > 0) {
            message = message + " - " + String(hours) + "H " + String(minutes) + "M";
        } else if (hours > 0) {
            message = message + " - " + String(hours) + "H";
        } else {
            message = message + " - " + String(minutes) + "M";
        }
    }

    return message;
}

// Queue for unified logging
QueueHandle_t unifiedLogQueue = NULL;

// Incremented whenever a log message is discarded instead of queued.
volatile uint32_t droppedLogCount = 0;

// Unified logging task
void unifiedLoggingTask(void *param) {
    UnifiedLogMessage logMsg;
    static uint32_t loggerStackLow = UINT32_MAX;

    for (;;) {
        checkStackHighWater(loggerStackLow);

        if (xQueueReceive(unifiedLogQueue, &logMsg, portMAX_DELAY) == pdTRUE) {
            switch (logMsg.type) {
                case LOG_TYPE_DEBUG:
                    logDebugActionInternal(logMsg.data.debug.action,
                                           logMsg.data.debug.message ? logMsg.data.debug.message : "",
                                           logMsg.ts);
                    // The queue entry owns the message copy - release it here,
                    // the only point where a queued debug log is consumed.
                    free(logMsg.data.debug.message);
                    break;
                case LOG_TYPE_OPERATION:
                    logActionInternal(logMsg.data.operation.mode,
                                    logMsg.data.operation.sType,
                                    logMsg.data.operation.duration,
                                   logMsg.data.operation.current,
                                   logMsg.data.operation.voltage,
                                   logMsg.ts);
                    break;
                case LOG_TYPE_ENERGY:
                    logEnergyDataInternal(DateTime(logMsg.data.energy.startTS), 
                                       DateTime(logMsg.data.energy.endTS), 
                                       logMsg.data.energy.energy);
                    break;
            }
        }
    }
}

// Queue functions for unified logging
// NOTE: these allocate, so they must never be called from an ISR. Interrupt
// context should latch a flag and let loop() do the logging (see the
// pendingSpikeButton handoff in buttonpress.cpp).
void queueDebugLog(DebugAction action, const char *message, time_t ts) {
    if (!unifiedLogQueue) return;

    // Leave the last LOG_QUEUE_RESERVE slots for operation/energy logs. A burst
    // of debug messages is exactly when an operation log matters most, so debug
    // logs yield rather than fill the queue to the brim.
    if (uxQueueSpacesAvailable(unifiedLogQueue) <= LOG_QUEUE_RESERVE) {
        droppedLogCount++;
        return;
    }

    UnifiedLogMessage logMsg;
    logMsg.type = LOG_TYPE_DEBUG;
    logMsg.ts = ts;
    logMsg.data.debug.action = action;
    logMsg.data.debug.message = NULL;

    // Copy the message onto the heap, truncated to DEBUG_MESSAGE_SIZE - 1 chars
    // (the limit logDebugActionInternal writes into a DebugEntry).
    if (message != nullptr) {
        size_t copyLength = strnlen(message, DEBUG_MESSAGE_SIZE - 1);
        char *heapMessage = (char *)malloc(copyLength + 1);
        if (heapMessage == NULL) {
            droppedLogCount++;
            return;
        }
        memcpy(heapMessage, message, copyLength);
        heapMessage[copyLength] = '\0';
        logMsg.data.debug.message = heapMessage;
    }

    if (xQueueSend(unifiedLogQueue, &logMsg, 0) != pdTRUE) {
        // Nothing took ownership of the copy, so release it here or it leaks.
        free(logMsg.data.debug.message);
        droppedLogCount++;
    }
}

void queueOperationLog(ModeOfAction mode, ScheduleType scheduleType, uint16_t duration, float current, float voltage,
                       time_t ts) {
    if (!unifiedLogQueue) return;
    UnifiedLogMessage logMsg;
    logMsg.type = LOG_TYPE_OPERATION;
    logMsg.ts = ts;
    logMsg.data.operation.mode = mode;
    logMsg.data.operation.sType = scheduleType;
    logMsg.data.operation.duration = duration;
    logMsg.data.operation.current = current;
    logMsg.data.operation.voltage = voltage;
    if (xQueueSend(unifiedLogQueue, &logMsg, 0) != pdTRUE) {
        droppedLogCount++;
    }
}

void queueEnergyLog(DateTime startDT, DateTime endDT, float energy) {
    if (!unifiedLogQueue) return;
    UnifiedLogMessage logMsg;
    logMsg.type = LOG_TYPE_ENERGY;
    logMsg.ts = startDT.unixtime(); // energy entries carry their own start/end times
    logMsg.data.energy.startTS = startDT.unixtime();
    logMsg.data.energy.endTS = endDT.unixtime();
    logMsg.data.energy.energy = energy;
    if (xQueueSend(unifiedLogQueue, &logMsg, 0) != pdTRUE) {
        droppedLogCount++;
    }
}

// Create the log queue ONLY. Call this early in setup() (before initiateTimeComponents)
// so boot-time logDebugAction() calls are captured instead of dropped by queueDebugLog's
// "if (!unifiedLogQueue) return;" guard. Messages wait in the queue until the logging
// task is started later - the task timestamps (needs RTC) and persists (needs LittleFS),
// so it must not run until those are ready.
void initUnifiedLogQueue() {
    if (unifiedLogQueue == NULL) {
        unifiedLogQueue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(UnifiedLogMessage));
    }
}

void initUnifiedLogging() {
    initUnifiedLogQueue(); // idempotent - queue may already exist from the early call
    // Priority 1 = the same as Arduino's loopTask, and on the same core (core 1).
    // Equal priority makes the scheduler time-slice between them instead of
    // letting this task hold the CPU through a ~110ms flash write while loop()
    // waits - loop() is where button presses and relay switching are handled.
    // Core 1 deliberately: core 0 runs the WiFi driver and lwIP tasks
    // (CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE_0 / CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0),
    // and blocking flash work there destabilises WiFi.
    xTaskCreatePinnedToCore(unifiedLoggingTask, "UnifiedLogger", 8192, NULL, 1, NULL, 1);
}

// Send all logs to MQTT for a specific type
//
// DISABLED - unreachable. Its only caller was the no-arg sendAllLogsToMQTT() in
// logdata.cpp, whose own only caller (main.cpp) is itself commented out.
// Historical resync now goes through queueReplayRequest()/publishReplayEntry(),
// which - unlike this - reproduces same-second ordering.
#if 0
void sendAllLogsToMQTT(LogType type) {
    int entriesSent = 0;
    int maxEntries = MAX_LOG_ENTRIES_TO_SYNC_MQTT;
    
    // Helper lambda to read from a specific file
    auto readLogFile = [&](time_t fileTs) {
        if (fileTs == 0) return;
        
        String filename = getLogFileName(type, fileTs);
        File file = LittleFS.open(filename, "r");
        if (!file) return;
        
        // Calculate entry count from file size (no header)
        size_t fileSize = file.size();
        size_t entryCount = fileSize / getEntrySize(type);
        
        // Read entries (in reverse order for newest first)
        for (int i = entryCount - 1; i >= 0 && entriesSent < maxEntries; i--) {
            file.seek(i * getEntrySize(type));
            
            switch(type) {
                case LOG_TYPE_DEBUG: {
                    DebugEntry entry;
                    file.read((uint8_t*)&entry, sizeof(entry));

                    JsonDocument doc;
                    doc["ts"] = static_cast<int64_t>(entry.timestamp - deviceTZOffset * 60) * 1000;
                    doc["values"]["eventMessage"] = entry.message;
                    doc["values"]["action"] = entry.mode;
                    doc["values"]["resync"] = true;

                    char jsonBuffer[256];
                    serializeJson(doc, jsonBuffer);
                    queueMQTTMessage(propTopic, jsonBuffer, false);
                    break;
                }
                case LOG_TYPE_OPERATION: {
                    LogEntry entry;
                    file.read((uint8_t*)&entry, sizeof(entry));

                    JsonDocument doc;
                    doc["ts"] = static_cast<int64_t>(entry.timestamp - deviceTZOffset * 60) * 1000;
                    doc["values"]["eventMessage"] = formatOperationLogMessage(entry);
                    doc["values"]["current"] = entry.current;
                    doc["values"]["voltage"] = entry.voltage;
                    // Replayed with the original event ts, so a reconnect while ON
                    // restores the latest duration the widget's countdown is built
                    // from (whole-minute precision only in the flash format).
                    doc["values"]["durationSecs"] = (uint32_t)entry.duration * 60;
                    doc["values"]["relayStatus"] = relayState ? true : false;
                    doc["values"]["resync"] = true;

                    if (masterSlaveMode == 1) {
                        doc["notifyRemotes"] = true;
                    }

                    char jsonBuffer[256];
                    serializeJson(doc, jsonBuffer);
                    queueMQTTMessage(propTopic, jsonBuffer, false);
                    break;
                }
                case LOG_TYPE_ENERGY: {
                    EnergyEntry entry;
                    file.read((uint8_t*)&entry, sizeof(entry));
                    
                    JsonDocument doc;
                    doc["ts"] = static_cast<int64_t>(entry.startTS - deviceTZOffset * 60) * 1000;
                    doc["values"]["startTS"] = entry.startTS;
                    doc["values"]["endTS"] = entry.endTS;
                    doc["values"]["energy"] = entry.energy;
                    
                    char jsonBuffer[256];
                    serializeJson(doc, jsonBuffer);
                    queueMQTTMessage(propTopic, jsonBuffer, false);
                    break;
                }
            }
            entriesSent++;
        }
        file.close();
    };
    
    // Read from current file first, then previous file if needed
    time_t currentFile = 0;
    time_t previousFile = 0;
    
    switch(type) {
        case LOG_TYPE_DEBUG: 
            currentFile = currentDebugFile;
            previousFile = previousDebugFile;
            break;
        case LOG_TYPE_OPERATION: 
            currentFile = currentOperationFile;
            previousFile = previousOperationFile;
            break;
        case LOG_TYPE_ENERGY: 
            currentFile = currentEnergyFile;
            previousFile = previousEnergyFile;
            break;
    }
    
    readLogFile(currentFile);
    if (entriesSent < maxEntries) {
        readLogFile(previousFile);
    }
}
#endif // sendAllLogsToMQTT disabled

void logDebugActionInternal(DebugAction action, const char* message, time_t eventTs) {
    LOG_TIMING_BEGIN();
    time_t currentTime = eventTs != 0 ? eventTs : getDateTimeFromRTC().unixtime();
    LOG_TIMING_MARK_TS();

    // Check if we need a new file
    if (currentDebugFile == 0) {
        createNewLogFile(LOG_TYPE_DEBUG);
        cleanupOldLogFiles(LOG_TYPE_DEBUG);
    }
    
    String filename = getLogFileName(LOG_TYPE_DEBUG, currentDebugFile);
    File file = LittleFS.open(filename, "a"); // Open in append mode - can check size directly
    
    // Check if current file has space for one more entry
    if (!file || file.size() + sizeof(DebugEntry) >= LOG_FILE_SIZE) {
        if (file) file.close();
        createNewLogFile(LOG_TYPE_DEBUG);
        cleanupOldLogFiles(LOG_TYPE_DEBUG);
        filename = getLogFileName(LOG_TYPE_DEBUG, currentDebugFile);
        file = LittleFS.open(filename, "a"); // Open new file in append mode
    }
    
    if (!file) {
        // String errorMsg = "Failed to open debug log file for writing: " + filename;
        // Serial.println(errorMsg);
        // Note: We can't use logDebugAction here as it would cause infinite recursion
        return;
    }
    
    // Write log entry directly to end of file
    DebugEntry entry;
    entry.timestamp = currentTime;
    entry.mode = action;
    // strncpy(entry.message, message, sizeof(entry.message) - 1);
    if (message != nullptr) {
        strncpy(entry.message, message, sizeof(entry.message) - 1);
        entry.message[sizeof(entry.message) - 1] = '\0';
        // size_t copyLength = strnlen(message, sizeof(entry.message) - 1);
        // if (copyLength > sizeof(entry.message) - 1) {
        //     copyLength = sizeof(entry.message) - 1;
        // }
        // memcpy(entry.message, message, copyLength);
        // entry.message[copyLength] = '\0';
    } else {
        entry.message[0] = '\0'; // Empty string if message is null
    }
    entry.message[sizeof(entry.message) - 1] = '\0';
    
    file.write((uint8_t*)&entry, sizeof(entry));
    file.close();
    LOG_TIMING_END("debug");
}

void logActionInternal(ModeOfAction mode, ScheduleType scheduleType, uint16_t duration, float current, float voltage,
                       time_t eventTs) {
    LOG_TIMING_BEGIN();
    time_t currentTime = eventTs != 0 ? eventTs : getDateTimeFromRTC().unixtime();
    LOG_TIMING_MARK_TS();

    // Static variables to track timestamp sequence for uniqueness
    static time_t lastTimestamp = 0;
    static uint16_t sequenceCounter = 0;
    
    // Reset sequence counter when timestamp changes
    if (currentTime != lastTimestamp) {
        sequenceCounter = 0;
        lastTimestamp = currentTime;
    } else {
        // Increment sequence counter for events within the same second
        sequenceCounter++;
        // Cap at 999 to stay within millisecond range (0-999ms)
        if (sequenceCounter > 999) {
            sequenceCounter = 999;
        }
    }
    
    // Check if we need a new file
    if (currentOperationFile == 0) {
        createNewLogFile(LOG_TYPE_OPERATION);
        cleanupOldLogFiles(LOG_TYPE_OPERATION);
    }
    
    String filename = getLogFileName(LOG_TYPE_OPERATION, currentOperationFile);
    File file = LittleFS.open(filename, "a"); // Open in append mode - can check size directly
    
    // Check if current file has space for one more entry
    if (!file || file.size() + sizeof(LogEntry) >= LOG_FILE_SIZE) {
        if (file) file.close();
        createNewLogFile(LOG_TYPE_OPERATION);
        cleanupOldLogFiles(LOG_TYPE_OPERATION);
        filename = getLogFileName(LOG_TYPE_OPERATION, currentOperationFile);
        file = LittleFS.open(filename, "a"); // Open new file in append mode
    }
    
    if (!file) {
        String errorMsg = "Failed to open operation log file for writing: " + filename;
        // Serial.println(errorMsg);
        logDebugAction(DebugAction::SYSTEM_FAILURE, (char*)errorMsg.c_str());
        return;
    }
    
    // Write log entry directly to end of file
    LogEntry entry;
    entry.timestamp = currentTime;
    entry.mode = mode;
    entry.sType = scheduleType;
    entry.duration = duration;
    entry.current = current;
    entry.voltage = voltage;
    
    file.write((uint8_t*)&entry, sizeof(entry));
    file.close();
    LOG_TIMING_END("oper"); // measured before the MQTT publish below, which is queued not blocking

    if (isCloudConnected ) {
        JsonDocument doc;
        // Use sequence counter to ensure unique, ordered timestamps within the same second
        doc["ts"] = static_cast<int64_t>(entry.timestamp - deviceTZOffset * 60) * 1000 + static_cast<int64_t>(sequenceCounter);
        doc["values"]["eventMessage"] = formatOperationLogMessage(entry);
        doc["values"]["current"] = entry.current;
        doc["values"]["voltage"] = entry.voltage;
        // durationSecs telemetry (consumed by device_control_v12+; replaces the
        // old whole-minutes `duration` key). Start events publish the exact
        // relayOnDuration; entry.duration is the flash format's whole minutes,
        // which floors a schedule resuming mid-minute and would end the widget
        // countdown up to 59s early.
        if (isStartEvent(entry.mode) && relayState) {
            doc["values"]["durationSecs"] = relayOnDuration.totalseconds();
            // runType lets the widget decide whether the running window is a real
            // schedule (S/C/R -> "Current Schedule") or a manual/button/web start
            // that interrupted it (anything else -> hide it, since the schedule
            // window would misrepresent the actual short run).
            doc["values"]["runType"] = String(runType);
        } else {
            // To note: duration could be in fraction of minutes(Particularly when power Resume happens). but it is saved as whole minute in log
            doc["values"]["durationSecs"] = (uint32_t)entry.duration * 60;
        }
        doc["values"]["relayStatus"] = relayState ? true : false;
        addVsmrTelemetry(doc["values"]);
        char jsonBuffer[320];
        serializeJson(doc, jsonBuffer);
        queueMQTTMessage(propTopic, jsonBuffer, true);
    }
    sendHomeAssistantStatusMessage(entry);
}

void logEnergyDataInternal(DateTime startDT, DateTime endDT, float energy) {
   // time_t currentTime = getDateTimeFromRTC().unixtime();
    
    // Check if we need a new file
    if (currentEnergyFile == 0) {
        createNewLogFile(LOG_TYPE_ENERGY);
        cleanupOldLogFiles(LOG_TYPE_ENERGY);
    }
    
    String filename = getLogFileName(LOG_TYPE_ENERGY, currentEnergyFile);
    File file = LittleFS.open(filename, "a"); // Open in append mode - can check size directly
    
    // Check if current file has space for one more entry
    if (!file || file.size() + sizeof(EnergyEntry) >= LOG_FILE_SIZE) {
        if (file) file.close();
        createNewLogFile(LOG_TYPE_ENERGY);
        cleanupOldLogFiles(LOG_TYPE_ENERGY);
        filename = getLogFileName(LOG_TYPE_ENERGY, currentEnergyFile);
        file = LittleFS.open(filename, "a"); // Open new file in append mode
    }
    
    if (!file) {
        String errorMsg = "Failed to open energy log file for writing: " + filename;
        // Serial.println(errorMsg);
        logDebugAction(DebugAction::SYSTEM_FAILURE, (char*)errorMsg.c_str());
        return;
    }
    
    // Write log entry directly to end of file
    EnergyEntry entry;
    entry.startTS = startDT.unixtime();
    entry.endTS = endDT.unixtime();
    entry.energy = energy;
    
    file.write((uint8_t*)&entry, sizeof(entry));
    file.close();
}

String getLogFileName(LogType type, time_t timestamp) {
    const char* prefix;
    switch(type) {
        case LOG_TYPE_DEBUG: prefix = LOG_FILE_PREFIX_DB; break;
        case LOG_TYPE_OPERATION: prefix = LOG_FILE_PREFIX_OP; break;
        case LOG_TYPE_ENERGY: prefix = LOG_FILE_PREFIX_ED; break;
        default: prefix = "XX"; break;
    }
    return String("/logs/") + prefix + "_" + String(timestamp) + ".bin";
}

void createNewLogFile(LogType type) {
    time_t timestamp = getDateTimeFromRTC().unixtime();
    String filename = getLogFileName(type, timestamp);
    
    // Create empty file - no header needed with append mode
    File file = LittleFS.open(filename, "w");
    if (file) {
        file.close();
        
        // Update file tracking: current becomes previous, new becomes current
        switch(type) {
            case LOG_TYPE_DEBUG: 
                previousDebugFile = currentDebugFile;
                currentDebugFile = timestamp; 
                break;
            case LOG_TYPE_OPERATION: 
                previousOperationFile = currentOperationFile;
                currentOperationFile = timestamp; 
                break;
            case LOG_TYPE_ENERGY: 
                previousEnergyFile = currentEnergyFile;
                currentEnergyFile = timestamp; 
                break;
        }
        
        // Serial.printf("Created new %s log file: %s (previous: %s)\n", 
        //              (type == LOG_TYPE_DEBUG) ? "debug" : 
        //              (type == LOG_TYPE_OPERATION) ? "operation" : "energy",
        //              filename.c_str(),
        //              (type == LOG_TYPE_DEBUG) ? String(previousDebugFile).c_str() :
        //              (type == LOG_TYPE_OPERATION) ? String(previousOperationFile).c_str() :
        //              String(previousEnergyFile).c_str());
    } else {
        // Log failure to debug system
        String errorMsg = "Failed to create " + String((type == LOG_TYPE_DEBUG) ? "debug" : 
                                                      (type == LOG_TYPE_OPERATION) ? "operation" : "energy") + 
                         " log file: " + filename;
        logDebugAction(DebugAction::SYSTEM_FAILURE, (char*)errorMsg.c_str());
        
        Serial.printf("ERROR: Failed to create %s log file: %s\n", 
                     (type == LOG_TYPE_DEBUG) ? "debug" : 
                     (type == LOG_TYPE_OPERATION) ? "operation" : "energy",
                     filename.c_str());
    }
}

void cleanupOldLogFiles(LogType type) {
    String prefix;
    u_int8_t maxFiles;
    switch(type) {
        case LOG_TYPE_DEBUG: 
            prefix = LOG_FILE_PREFIX_DB; 
            maxFiles = MAX_LOG_FILES_DB;
            break;
        case LOG_TYPE_OPERATION: prefix = LOG_FILE_PREFIX_OP; 
            maxFiles = MAX_LOG_FILES_OP;
            break;
        case LOG_TYPE_ENERGY: prefix = LOG_FILE_PREFIX_ED;
            maxFiles = MAX_LOG_FILES_ED;
            break;
    }
    
    // Get all files of this type
    File root = LittleFS.open("/logs");
    if (!root) {
        Serial.println("Failed to open /logs directory");
        return;
    }
    
    File file = root.openNextFile();
    std::vector<String> files;
    
    while (file) {
        String filename = file.name();
        if (filename.startsWith(prefix + "_")) {
            files.push_back(filename);
        }
        file = root.openNextFile();
    }
    
    // Sort by timestamp (newest first)
    std::sort(files.begin(), files.end(), [](const String& a, const String& b) {
        // Extract timestamp from filename and compare
        String timestampA = a.substring(a.indexOf('_') + 1, a.indexOf('.'));
        String timestampB = b.substring(b.indexOf('_') + 1, b.indexOf('.'));
        return timestampA.toInt() > timestampB.toInt();
    });
    
    // Keep only MAX_LOG_FILES
    for (size_t i = maxFiles; i < files.size(); i++) {
        String filePath = "/logs/" + files[i];
        Serial.print("Removing old log file: ");
        Serial.println(filePath);
        
        if (!LittleFS.remove(filePath)) {
            Serial.print("Failed to remove file: ");
            Serial.println(filePath);
            
            // Log deletion failure to debug system
            String errorMsg = "Failed to remove old log file: " + filePath;
            logDebugAction(DebugAction::SYSTEM_FAILURE, (char*)errorMsg.c_str());
        }
    }
}

std::vector<String> getLogFilesSorted(const String& prefix) {
    std::vector<String> files;
    File root = LittleFS.open("/logs");
    File file = root.openNextFile();
    
    while (file) {
        String filename = file.name();
        if (filename.startsWith(prefix + "_")) {
            files.push_back(filename);
        }
        file = root.openNextFile();
    }
    
    // Sort by timestamp (newest first)
    std::sort(files.begin(), files.end(), [](const String& a, const String& b) {
        // Extract timestamp from filename
        String timestampA = a.substring(a.indexOf('_') + 1, a.indexOf('.'));
        String timestampB = b.substring(b.indexOf('_') + 1, b.indexOf('.'));
        return timestampA.toInt() > timestampB.toInt();
    });
    
    return files;
}

size_t getEntrySize(LogType type) {
    switch(type) {
        case LOG_TYPE_DEBUG: return sizeof(DebugEntry);
        case LOG_TYPE_OPERATION: return sizeof(LogEntry);
        case LOG_TYPE_ENERGY: return sizeof(EnergyEntry);
        default: return 0;
    }
}

// Debug function to list all files in /logs directory
void listAllLogFiles() {
    Serial.println("=== Listing all files in /logs directory ===");
    File root = LittleFS.open("/logs");
    if (!root) {
        Serial.println("Failed to open /logs directory");
        return;
    }
    
    File file = root.openNextFile();
    int fileCount = 0;
    
    while (file) {
        String filename = file.name();
        size_t fileSize = file.size();
        Serial.printf("File %d: %s (size: %d bytes)\n", fileCount++, filename.c_str(), fileSize);
        file = root.openNextFile();
    }
    
    Serial.printf("Total files found: %d\n", fileCount);
    Serial.println("=== End of file listing ===");
}

// Debug function to test file deletion
void testFileDeletion(const String& filename) {
    String filePath = "/logs/" + filename;
    Serial.printf("Testing deletion of: %s\n", filePath.c_str());
    
    if (LittleFS.exists(filePath)) {
        Serial.println("File exists, attempting deletion...");
        if (LittleFS.remove(filePath)) {
            Serial.println("File deleted successfully!");
        } else {
            Serial.println("Failed to delete file!");
        }
    } else {
        Serial.println("File does not exist!");
    }
}

// Helper function to validate timestamp
bool isValidTimestamp(time_t timestamp, time_t currentTs) {
    // Check for obviously invalid timestamps
    if (timestamp == 0 || timestamp == -1) {
        return false;
    }
    
    // Check if timestamp is in a reasonable range (between 2020 and 2030)
    // Unix timestamp for 2020-01-01 00:00:00 UTC = 1577836800
    // Unix timestamp for 2030-12-31 23:59:59 UTC = 1924991999
    if (timestamp < 1577836800 || timestamp > currentTs) {
        return false;
    }
    
    return true;
}

// Migration routine to convert old circular queue format to new multi-file format
String migrateOldLogFiles() {
    String migrationDetails = "=== Log Migration Summary ===\n";
    Serial.println("=== Starting log file migration ===");
    
    // Check for old operation log files
    if (shouldMigrateOperationLogs()) {
        Serial.println("Found old operation log files, migrating...");
        migrationDetails += migrateOperationLogs();
    } else {
        Serial.println("No old operation log files found");
        migrationDetails += "Operation logs: No migration needed\n";
    }
    
    // Check for old debug log files
    if (shouldMigrateDebugLogs()) {
        Serial.println("Found old debug log files, migrating...");
        migrationDetails += migrateDebugLogs();
    } else {
        Serial.println("No old debug log files found");
        migrationDetails += "Debug logs: No migration needed\n";
    }
    
    // Check for old energy log files
    if (shouldMigrateEnergyLogs()) {
        Serial.println("Found old energy log files, migrating...");
        migrationDetails += migrateEnergyLogs();
    } else {
        Serial.println("No old energy log files found");
        migrationDetails += "Energy logs: No migration needed\n";
    }
    
    migrationDetails += "=== Log file migration completed ===\n";
    Serial.println("=== Log file migration completed ===");
    
    return migrationDetails;
}

String migrateOperationLogs() {
    String migrationDetails = "";
    // Open old files
    File dataFile = LittleFS.open("/logs/gen2-log.bin", "r");
    File pointersFile = LittleFS.open("/logs/pointers.bin", "r");

    if (!dataFile || !pointersFile) {
        String errorMsg = "Failed to open old operation log files\n";
        Serial.println(errorMsg);
        if (dataFile)
            dataFile.close();
        if (pointersFile)
            pointersFile.close();
        return errorMsg;
    }

    // Read pointers
    Pointers pointers;
    pointersFile.read((uint8_t *)&pointers, sizeof(pointers));
    pointersFile.close();

    // Serial.printf("Old operation log - start: %d, end: %d, count: %d\n", pointers.start, pointers.end, pointers.count);

    if (pointers.count == 0) {
        String noEntriesMsg = "No operation log entries to migrate\n";
        // Serial.println(noEntriesMsg);
        dataFile.close();
        return noEntriesMsg;
    }

    // Create new log file for migration
    time_t migrationTimestamp = getDateTimeFromRTC().unixtime();
    String newFilename = getLogFileName(LOG_TYPE_OPERATION, migrationTimestamp);
    Serial.println("newFilename");
    Serial.println(newFilename);

    File newFile = LittleFS.open(newFilename, "w");

    if (!newFile) {
        String errorMsg = "Failed to create new operation log file for migration\n";
        // Serial.println(errorMsg);
        dataFile.close();
        return errorMsg;
    }

    // Read and migrate entries (no header needed)
    const size_t recordSize = 16; // Old format with padding
    const size_t maxEntries = dataFile.size() / recordSize;
    // Serial.println("maxEntries");
    // Serial.println(maxEntries);

    // Collect all entries in chronological order
    std::vector<LogEntry> entries;
    entries.reserve(pointers.count);

    int32_t current = pointers.end - recordSize;
    if (current < 0) {
        current = (maxEntries - 1) * recordSize;
    }
    // First pass: collect all entries from circular queue
    for (int i = 0; i < pointers.count && i < maxEntries; i++) {
        Serial.print("Reading entry i=");
        Serial.println(i);

        // Read old format entry
        dataFile.seek(current);
        
        time_t timestamp;
        dataFile.read((uint8_t *)&timestamp, sizeof(timestamp));
        
        uint8_t mode;
        dataFile.read((uint8_t *)&mode, sizeof(mode));
        
        // Old format doesn't have scheduleType and duration fields
        // Set them to default values for old entries
        uint8_t scheduleType = SCH_NONE;  // Default to no schedule
        uint16_t duration = 0;            // Default to no duration
        
        // Skip 1 byte padding
        dataFile.seek(current + 8);
        
        float current_val, voltage_val;
        dataFile.read((uint8_t *)&current_val, sizeof(current_val));
        dataFile.read((uint8_t *)&voltage_val, sizeof(voltage_val));

                // Validate timestamp before creating entry
        if (isValidTimestamp(timestamp, migrationTimestamp)) {
            // Create new format entry
            LogEntry newEntry;
            newEntry.timestamp = timestamp;
            newEntry.mode = (ModeOfAction)mode;
            newEntry.sType = (ScheduleType)scheduleType;  // SCH_NONE for old entries
            newEntry.duration = duration;                 // 0 for old entries (no duration tracking)
            newEntry.current = current_val;
            newEntry.voltage = voltage_val;
            
            entries.push_back(newEntry);
        } else {
            Serial.printf("Skipping operation log entry with invalid timestamp: %ld\n", timestamp);
        }

        // Move to previous entry in circular queue
        current -= recordSize;
        if (current < 0) {
            Serial.println("inside if (current < 0) ");
            current = (maxEntries - 1) * recordSize;
        }
    }

    // Second pass: write entries in chronological order (oldest first)
    int migratedCount = 0;
    for (int i = entries.size() - 1; i >= 0; i--) {
        newFile.write((uint8_t *)&entries[i], sizeof(LogEntry));
        migratedCount++;
    }

    newFile.close();
    dataFile.close();

    String resultMsg = String("Operation logs: Migrated - ") + migratedCount + 
                      " (skipped - " + (pointers.count - migratedCount) + " )\n";
    Serial.printf("Migrated %d operation log entries to %s (skipped %d entries with invalid timestamps)\n", 
                  migratedCount, newFilename.c_str(), pointers.count - migratedCount);

    // Update current file tracking
    currentOperationFile = migrationTimestamp;

    // Rename old files to indicate migration
    if (LittleFS.rename("/logs/gen2-log.bin", "/logs/gen2-log-migrated.bin")) {
        Serial.println("Renamed old gen2-log.bin to gen2-log-migrated.bin");
    }
    if (LittleFS.rename("/logs/pointers.bin", "/logs/pointers-migrated.bin")) {
        Serial.println("Renamed old pointers.bin to pointers-migrated.bin");
    }
    
    return resultMsg;
}

String migrateDebugLogs() {
    String migrationDetails = "";
    // Open old files
    File dataFile = LittleFS.open("/logs/gen2-debug.bin", "r");
    File pointersFile = LittleFS.open("/logs/debug-pointers.bin", "r");
    
    if (!dataFile || !pointersFile) {
        String errorMsg = "Failed to open old debug log files\n";
        Serial.println(errorMsg);
        if (dataFile) dataFile.close();
        if (pointersFile) pointersFile.close();
        return errorMsg;
    }
    
    // Read pointers
    DebugLogPointers pointers;
    pointersFile.read((uint8_t*)&pointers, sizeof(pointers));
    pointersFile.close();
    
    Serial.printf("Old debug log - start: %d, calend: %d, count: %d\n", 
                  pointers.start, pointers.end, pointers.count);
    
    if (pointers.count == 0) {
        String noEntriesMsg = "No debug log entries to migrate\n";
        Serial.println(noEntriesMsg);
        dataFile.close();
        return noEntriesMsg;
    }
    
    // Create new log file for migration
    time_t migrationTimestamp = getDateTimeFromRTC().unixtime();
    String newFilename = getLogFileName(LOG_TYPE_DEBUG, migrationTimestamp);
    File newFile = LittleFS.open(newFilename, "w");
    
    if (!newFile) {
        String errorMsg = "Failed to create new debug log file for migration\n";
        Serial.println(errorMsg);
        dataFile.close();
        return errorMsg;
    }
    
    // Read entire file into memory to handle alignment issues
    size_t fileSize = dataFile.size();
    uint8_t* rawFileData = new uint8_t[fileSize];
    dataFile.read(rawFileData, fileSize);
    dataFile.close();
    
    // Record size accounting for memory alignment
    const size_t actualRecordSize = 205; // timestamp(4) + mode(1) + message(200)
    const size_t alignedRecordSize = 208; // Actual size on disk due to alignment
    const size_t maxEntries = fileSize / alignedRecordSize;
    
    Serial.printf("File size: %d, Actual record size: %d, Aligned record size: %d, Max entries: %d\n", 
                  fileSize, actualRecordSize, alignedRecordSize, maxEntries);
    Serial.println(pointers.end);
    
    // Collect all entries in chronological order
    std::vector<DebugEntry> entries;
    entries.reserve(pointers.count);

    int32_t current = pointers.end - alignedRecordSize;
    if (current < alignedRecordSize) {
        current = (maxEntries - 1) * alignedRecordSize;
    }
    Serial.println("current pointer");
    Serial.println(current);
    // First pass: collect all entries from circular queue
    for (int i = 0; i < pointers.count && i < maxEntries; i++) {
        Serial.printf("Processing entry %d at offset %d\n", i, current);
        
        // Extract data from raw buffer accounting for alignment
        uint8_t* recordStart = &rawFileData[current];
        
        // Extract timestamp (first 4 bytes)
        time_t timestamp;
        memcpy(&timestamp, recordStart, 4);
        
        // Extract mode (5th byte)
        uint8_t mode = recordStart[4];
        
        // Extract message (bytes 5-204)
        char message[200];
        memcpy(message, &recordStart[5], 200);
        
        // Validate timestamp before creating entry
        if (isValidTimestamp(timestamp, migrationTimestamp)) {
            Serial.println("Valid timestamp");
            Serial.println(formatDateTime(timestamp));
            Serial.println("mode");
            Serial.println(mode);
            Serial.println("message");
            Serial.println(message);
            
            // Create new format entry
            DebugEntry newEntry;
            newEntry.timestamp = timestamp;
            newEntry.mode = (DebugAction)mode;
            // strncpy(newEntry.message, message, sizeof(newEntry.message) - 1);
            // newEntry.message[sizeof(newEntry.message) - 1] = '\0';
            // After: Safe bounded copying
            size_t copyLength = strnlen(message, sizeof(newEntry.message) - 1);
            if (copyLength > sizeof(newEntry.message) - 1) {
                copyLength = sizeof(newEntry.message) - 1;
            }
            memcpy(newEntry.message, message, copyLength);
            newEntry.message[copyLength] = '\0';
            
            entries.push_back(newEntry);
        } else {
            Serial.println("Invalid timestamp");
            Serial.println(formatDateTime(timestamp));
            Serial.printf("Skipping debug log entry with invalid timestamp: %ld\n", timestamp);
        }
        
        // Move to previous entry in circular queue (using aligned size)
        current -= alignedRecordSize;
        if (current < 0) {
            current = (maxEntries - 1) * alignedRecordSize;
        }
    }
    
    // Second pass: write entries in chronological order (oldest first)
    int migratedCount = 0;
    for (int i = entries.size() - 1; i >= 0; i--) {
        newFile.write((uint8_t*)&entries[i], sizeof(DebugEntry));
        migratedCount++;
    }
    
    newFile.close();
    
    // Clean up allocated memory
    delete[] rawFileData;
    
    String resultMsg = String("Debug logs: Migrated ") + migratedCount +  
                      " (skipped " + (pointers.count - migratedCount) + " )\n";
    Serial.printf("Migrated %d debug log entries to %s (skipped %d entries with invalid timestamps)\n", 
                  migratedCount, newFilename.c_str(), pointers.count - migratedCount);
    
    // Update current file tracking
    currentDebugFile = migrationTimestamp;
    
    // Rename old files to indicate migration
    if (LittleFS.rename("/logs/gen2-debug.bin", "/logs/gen2-debug-migrated.bin")) {
        Serial.println("Renamed old gen2-debug.bin to gen2-debug-migrated.bin");
    }
    if (LittleFS.rename("/logs/debug-pointers.bin", "/logs/debug-pointers-migrated.bin")) {
        Serial.println("Renamed old debug-pointers.bin to debug-pointers-migrated.bin");
    }
    
    return resultMsg;
}

String migrateEnergyLogs() {
    String migrationDetails = "";
    // Open old files
    File dataFile = LittleFS.open("/logs/gen2-energy.bin", "r");
    File pointersFile = LittleFS.open("/logs/energy-pointers.bin", "r");
    
    if (!dataFile || !pointersFile) {
        String errorMsg = "Failed to open old energy log files\n";
        Serial.println(errorMsg);
        if (dataFile) dataFile.close();
        if (pointersFile) pointersFile.close();
        return errorMsg;
    }
    
    // Read pointers
    EnergyPointers pointers;
    pointersFile.read((uint8_t*)&pointers, sizeof(pointers));
    pointersFile.close();
    
    Serial.printf("Old energy log - start: %d, end: %d, count: %d\n", 
                  pointers.start, pointers.end, pointers.count);
    
    if (pointers.count == 0) {
        String noEntriesMsg = "No energy log entries to migrate\n";
        Serial.println(noEntriesMsg);
        dataFile.close();
        return noEntriesMsg;
    }
    
    // Create new log file for migration
    time_t migrationTimestamp = getDateTimeFromRTC().unixtime();
    String newFilename = getLogFileName(LOG_TYPE_ENERGY, migrationTimestamp);
    File newFile = LittleFS.open(newFilename, "w");
    
    if (!newFile) {
        String errorMsg = "Failed to create new energy log file for migration\n";
        Serial.println(errorMsg);
        dataFile.close();
        return errorMsg;
    }
    
    // Read and migrate entries (no header needed)
    const size_t recordSize = 12; // Old energy format: startTS(4) + endTS(4) + energy(4)
    const size_t maxEntries = dataFile.size() / recordSize;
    
    // Collect all entries in chronological order
    std::vector<EnergyEntry> entries;
    entries.reserve(pointers.count);
    
    int32_t current = pointers.end - recordSize;
    if (current < recordSize) {
        current = (maxEntries - 1) * recordSize;
    }
    
    // First pass: collect all entries from circular queue
    for (int i = 0; i < pointers.count && i < maxEntries; i++) {
        // Read old format entry
        dataFile.seek(current);
        
        time_t startTS, endTS;
        float energy;
        
        dataFile.read((uint8_t*)&startTS, sizeof(startTS));
        dataFile.read((uint8_t*)&endTS, sizeof(endTS));
        dataFile.read((uint8_t*)&energy, sizeof(energy));
        
        // Validate timestamps before creating entry
        if (isValidTimestamp(startTS, migrationTimestamp) && isValidTimestamp(endTS, migrationTimestamp) && startTS <= endTS) {
            // Create new format entry
            EnergyEntry newEntry;
            newEntry.startTS = startTS;
            newEntry.endTS = endTS;
            newEntry.energy = energy;
            
            entries.push_back(newEntry);
        } else {
            Serial.printf("Skipping energy log entry with invalid timestamps: startTS=%ld, endTS=%ld\n", startTS, endTS);
        }
        
        // Move to previous entry in circular queue
        current -= recordSize;
        if (current < 0) {
            current = (maxEntries - 1) * recordSize;
        }
    }
    
    // Second pass: write entries in chronological order (oldest first)
    int migratedCount = 0;
    for (int i = entries.size() - 1; i >= 0; i--) {
        newFile.write((uint8_t*)&entries[i], sizeof(EnergyEntry));
        migratedCount++;
    }
    
    newFile.close();
    dataFile.close();
    
    String resultMsg = String("Energy logs: Migrated ") + migratedCount + 
                      " (skipped " + (pointers.count - migratedCount) + " )\n";
    Serial.printf("Migrated %d energy log entries to %s (skipped %d entries with invalid timestamps)\n", 
                  migratedCount, newFilename.c_str(), pointers.count - migratedCount);
    
    // Update current file tracking
    currentEnergyFile = migrationTimestamp;
    
    // Rename old files to indicate migration
    if (LittleFS.rename("/logs/gen2-energy.bin", "/logs/gen2-energy-migrated.bin")) {
        Serial.println("Renamed old gen2-energy.bin to gen2-energy-migrated.bin");
    }
    if (LittleFS.rename("/logs/energy-pointers.bin", "/logs/energy-pointers-migrated.bin")) {
        Serial.println("Renamed old energy-pointers.bin to energy-pointers-migrated.bin");
    }
    
    return resultMsg;
}

void clearLogsByType(LogType type) {
    String prefix;
    time_t *currentFile = nullptr;
    time_t *previousFile = nullptr;
    
    switch(type) {
        case LOG_TYPE_DEBUG:
            prefix = LOG_FILE_PREFIX_DB;
            currentFile = &currentDebugFile;
            previousFile = &previousDebugFile;
            break;
        case LOG_TYPE_OPERATION:
            prefix = LOG_FILE_PREFIX_OP;
            currentFile = &currentOperationFile;
            previousFile = &previousOperationFile;
            break;
        case LOG_TYPE_ENERGY:
            prefix = LOG_FILE_PREFIX_ED;
            currentFile = &currentEnergyFile;
            previousFile = &previousEnergyFile;
            break;
    }
    
    // Remove all log files of this type
    File root = LittleFS.open("/logs");
    if (!root) {
        Serial.println("Failed to open /logs directory for clearing");
        return;
    }
    
    File file = root.openNextFile();
    int filesRemoved = 0;
    
    while (file) {
        String filename = file.name();
        file.close(); // Close the file before deleting
        
        if (filename.startsWith(prefix + "_")) {
            String filePath = "/logs/" + filename;
            Serial.print("Removing ");
            Serial.print((type == LOG_TYPE_DEBUG) ? "debug" : 
                        (type == LOG_TYPE_OPERATION) ? "operation" : "energy");
            Serial.print(" log file: ");
            Serial.println(filePath);
            
            if (LittleFS.remove(filePath)) {
                filesRemoved++;
            } else {
                Serial.print("Failed to remove file: ");
                Serial.println(filePath);
            }
        }
        file = root.openNextFile();
    }
    
    Serial.print("Removed ");
    Serial.print(filesRemoved);
    Serial.print(" ");
    Serial.print((type == LOG_TYPE_DEBUG) ? "debug" : 
                (type == LOG_TYPE_OPERATION) ? "operation" : "energy");
    Serial.println(" log files");
    
    // Reset file tracking
    *currentFile = 0;
    *previousFile = 0;
    
    // Create a new initial file
    createNewLogFile(type);
}

// Migration check functions
bool shouldMigrateOperationLogs() {
    return LittleFS.exists("/logs/gen2-log.bin") && LittleFS.exists("/logs/pointers.bin");
}

bool shouldMigrateDebugLogs() {
    return LittleFS.exists("/logs/gen2-debug.bin") && LittleFS.exists("/logs/debug-pointers.bin");
}

bool shouldMigrateEnergyLogs() {
    return LittleFS.exists("/logs/gen2-energy.bin") && LittleFS.exists("/logs/energy-pointers.bin");
}

bool isStartEvent(ModeOfAction mode) {
    return (mode == START_WEB || mode == START_SCHEDULED ||
            mode == START_POWER_RESUME || mode == START_MANUAL_1 ||
            mode == START_MANUAL_2 || mode == START_MANUAL_3 ||
            mode == START_MANUAL_4 || mode == START_MQTT ||
            mode == START_CYCLIC || mode == START_FLOW_SENSOR ||
            mode == FORCE_START_FLOW || mode == START_WEBHOOK);
}

bool isStopEvent(ModeOfAction mode) {
    return (mode == STOP_TIMER || mode == STOP_MANUAL || 
            mode == STOP_SCHEDULED || mode == STOP_WEB || 
            mode == STOP_POWER_DOWN || mode == STOP_DRYRUN || 
            mode == STOP_OVERRUN || mode == STOP_LOWVOLTAGE || 
            mode == STOP_HIGHVOLTAGE || mode == STOP_FLOAT || 
            mode == STOP_MQTT || mode == STOP_FLOW_SENSOR || 
            mode == STOP_WEBHOOK || mode == STOP_MAXTIME_BREAK ||
            mode == STOP_DURATION_COMPLETED);
}

void sendHomeAssistantStatusMessage(const LogEntry& entry) {
    // Only send Home Assistant messages if MQTT is enabled and configured for Home Assistant
    if (!isGeneralMQTTEnabled || mqttIntegrationType != "HA") {
        return;
    }

    String switchName = getSwitchNameForHa();
    String relayStatusTopic = getHaSwitchTopic("switch", "relayStatus");
    Serial.println("Sending Relay status to: " + relayStatusTopic);
    Serial.println(entry.mode);
    // Send main relay status based on start/stop events
    if (isStartEvent(entry.mode)) {
        Serial.println("Sending Main Relay ON to: " + relayStatusTopic);
        queueMQTTMessage(relayStatusTopic.c_str(), "ON", true);
    } else if (isStopEvent(entry.mode)) {
        Serial.println("Sending Main Relay OFF to: " + relayStatusTopic);
        queueMQTTMessage(relayStatusTopic.c_str(), "OFF", true);
    }

}

// ---------- Historical log replay ----------
// A ReplayRef is a LittleFS index (file + offset). LogType is the same for
// every entry in a single request, so it lives in a file-scope static —
// keeping each slot at 8 bytes exactly.
// Unix seconds as uint32_t is safe through the year 2106.
struct ReplayRef {
    uint32_t fileTs; // which log file (filename key)
    uint32_t offset; // byte offset within that file
};

static const uint16_t REPLAY_MAX = 50;
static ReplayRef replayBuf[REPLAY_MAX]; // ~800 B
static uint16_t replayCount = 0;
static LogType replayType = LOG_TYPE_OPERATION;

// Same-second ordering for replayed entries: see replayIntraSecondIndex().

bool isReplayPending() {
    return replayCount > 0;
}

static const char *logPrefixForType(LogType type) {
    switch (type) {
        case LOG_TYPE_DEBUG:     return LOG_FILE_PREFIX_DB;
        case LOG_TYPE_OPERATION: return LOG_FILE_PREFIX_OP;
        case LOG_TYPE_ENERGY:    return LOG_FILE_PREFIX_ED;
    }
    return nullptr;
}

static time_t fileTsFromName(const String &filename) {
    int u = filename.indexOf('_');
    int d = filename.indexOf('.');
    if (u < 0 || d < 0 || d <= u + 1) return 0;
    return (time_t)filename.substring(u + 1, d).toInt();
}

// Scans one file newest-first, appending matching refs into replayBuf up to REPLAY_MAX.
static void scanFileForReplay(LogType type, time_t fileTs, time_t fromTs, time_t toTs,
                              uint16_t maxCount) {
    if (fileTs == 0 || replayCount >= maxCount) return;
    String filename = getLogFileName(type, fileTs);
    File file = LittleFS.open(filename, "r");
    if (!file) return;
    size_t entrySize = getEntrySize(type);
    size_t fileSize = file.size();
    if (entrySize == 0 || fileSize < entrySize) { file.close(); return; }
    size_t entryCount = fileSize / entrySize;
    for (int i = (int)entryCount - 1; i >= 0 && replayCount < maxCount; i--) {
        uint32_t offset = (uint32_t)i * entrySize;
        if (!file.seek(offset)) continue;
        // timestamp is the first field of LogEntry / DebugEntry / EnergyEntry.
        time_t entryTs = 0;
        if (file.read((uint8_t *)&entryTs, sizeof(time_t)) != sizeof(time_t)) continue;
        bool inRange = (fromTs == 0 || entryTs >= fromTs) && (toTs == 0 || entryTs <= toTs);
        if (inRange) {
            replayBuf[replayCount].fileTs = (uint32_t)fileTs;
            replayBuf[replayCount].offset = offset;
            replayCount++;
        }
    }
    file.close();
}

uint16_t queueReplayRequest(LogType type, time_t fromTs, time_t toTs, uint16_t maxCount) {
    if (isReplayPending()) {
        Serial.println("[REPLAY] reject - previous request still draining");
        return 0;
    }
    if (maxCount == 0 || maxCount > REPLAY_MAX) {
        maxCount = REPLAY_MAX;
    }
    const char *prefix = logPrefixForType(type);
    if (prefix == nullptr) return 0;

    // Scan every file of this type (getLogFilesSorted returns newest-first) directly
    // into replayBuf. Drain order is newest -> oldest; TB rows are keyed by ts so
    // the UI doesn't care.
    replayCount = 0;
    replayType = type;
    std::vector<String> files = getLogFilesSorted(String(prefix));
    for (size_t idx = 0; idx < files.size() && replayCount < maxCount; idx++) {
        time_t fileTs = fileTsFromName(files[idx]);
        scanFileForReplay(type, fileTs, fromTs, toTs, maxCount);
    }
    if (replayCount == 0) {
        Serial.println("[REPLAY] no matching entries");
        return 0;
    }
    Serial.printf("[REPLAY] queued %u entries (type=%d, from=%ld, to=%ld, scannedFiles=%u)\n",
                  (unsigned)replayCount, (int)type, (long)fromTs, (long)toTs,
                  (unsigned)files.size());
    return replayCount;
}

// The flash format stores whole seconds, so several entries can share a
// timestamp; published with an identical ts the cloud would order them
// arbitrarily - or overwrite one another, since rows are keyed by ts.
//
// This returns the entry's position *within its second as stored in the file*,
// by walking backwards from its offset while the timestamp matches. That is the
// same number the live path's sequenceCounter (logActionInternal) produced, so a
// replayed entry lands on exactly the ts it had when published live.
//
// Deriving it from the file rather than from position within the replay batch is
// what makes a truncated run correct: replaying only the newest 5 of 10
// same-second entries still yields 5..9, not 0..4 - which would have collided
// with the entries that really were 0..4.
//
// Being a pure function of (file, offset) it is also retry-safe by construction.
// Caller must have the entry already read; this moves the file cursor.
static uint16_t replayIntraSecondIndex(File &file, uint32_t offset, size_t entrySize, time_t ts) {
    uint16_t index = 0;
    uint32_t probe = offset;
    while (probe >= entrySize && index < 999) {
        probe -= entrySize;
        if (!file.seek(probe)) break;
        // timestamp is the first field of LogEntry / DebugEntry / EnergyEntry
        time_t prevTs = 0;
        if (file.read((uint8_t *)&prevTs, sizeof(time_t)) != sizeof(time_t)) break;
        if (prevTs != ts) break;
        index++;
    }
    return index;
}

// Returns true on success, false if publish could not be queued.
static bool publishReplayEntry(const ReplayRef &ref) {
    String filename = getLogFileName(replayType, (time_t)ref.fileTs);
    File file = LittleFS.open(filename, "r");
    if (!file) return false;
    if (!file.seek(ref.offset)) { file.close(); return false; }
    char jsonBuffer[256];
    bool ok = false;
    time_t entryTs = 0;
    uint16_t subMs = 0;
    switch (replayType) {
        case LOG_TYPE_DEBUG: {
            DebugEntry entry;
            if (file.read((uint8_t *)&entry, sizeof(entry)) == sizeof(entry)) {
                JsonDocument doc;
                entryTs = entry.timestamp;
                subMs = replayIntraSecondIndex(file, ref.offset, sizeof(entry), entryTs);
                doc["ts"] = static_cast<int64_t>(entryTs - deviceTZOffset * 60) * 1000 + subMs;
                doc["values"]["debugMessage"] = entry.message;
                doc["values"]["action"] = entry.mode;
                doc["values"]["resync"] = true;
                serializeJson(doc, jsonBuffer);
                ok = true;
            }
            break;
        }
        case LOG_TYPE_OPERATION: {
            LogEntry entry;
            if (file.read((uint8_t *)&entry, sizeof(entry)) == sizeof(entry)) {
                JsonDocument doc;
                entryTs = entry.timestamp;
                subMs = replayIntraSecondIndex(file, ref.offset, sizeof(entry), entryTs);
                doc["ts"] = static_cast<int64_t>(entryTs - deviceTZOffset * 60) * 1000 + subMs;
                doc["values"]["eventMessage"] = formatOperationLogMessage(entry);
                doc["values"]["resync"] = true;
                // Replayed with the original event ts, so the reconnect replay
                // restores the latest duration the widget's countdown is built
                // from (device turned ON while MQTT was down). Whole-minute
                // precision only - the flash format stores minutes.
                doc["values"]["durationSecs"] = (uint32_t)entry.duration * 60;
                // doc["values"]["current"] = entry.current;
                // doc["values"]["voltage"] = entry.voltage;
                serializeJson(doc, jsonBuffer);
                ok = true;
            }
            break;
        }
        case LOG_TYPE_ENERGY: {
            EnergyEntry entry;
            if (file.read((uint8_t *)&entry, sizeof(entry)) == sizeof(entry)) {
                JsonDocument doc;
                entryTs = entry.startTS;
                subMs = replayIntraSecondIndex(file, ref.offset, sizeof(entry), entryTs);
                doc["ts"] = static_cast<int64_t>(entryTs - deviceTZOffset * 60) * 1000 + subMs;
                doc["values"]["startTS"] = entry.startTS;
                doc["values"]["endTS"] = entry.endTS;
                doc["values"]["energy"] = entry.energy;
                serializeJson(doc, jsonBuffer);
                ok = true;
            }
            break;
        }
    }
    file.close();
    if (!ok) return false;
    return queueMQTTMessage(propTopic, jsonBuffer, false);
}

void servicePendingReplay() {
    if (replayCount == 0) return;
    if (mqttQueue == NULL) return;
    // Only push when the live queue is idle — live traffic always wins.
    if (uxQueueMessagesWaiting(mqttQueue) != 0) return;
    if (!mqttClient.connected()) return;

    // Drain OLDEST first. scanFileForReplay fills replayBuf newest-first, so the
    // last slot is the oldest entry - taking from the end and shrinking the count
    // walks forward in time. Ascending order is what lets the same-second
    // sequence numbering in publishReplayEntry() reproduce the original order.
    const ReplayRef &ref = replayBuf[replayCount - 1];
    if (publishReplayEntry(ref)) {
        replayCount--;
        if (replayCount == 0) {
            Serial.println("[REPLAY] completed");
        }
    }
    // If publishReplayEntry() returned false (queue push rejected, file vanished, etc.)
    // we leave the ref in place; the next tick will retry.
}