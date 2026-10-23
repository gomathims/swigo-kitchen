#pragma once
#include "logdebug.h"
#include "logdata.h"
#include "energydata.h"
#include <vector>
#include <mqtt.h>

#define LOG_FILE_SIZE 8*1024  // 8KB

#define MAX_LOG_FILES_DB 12   // Keep last 12 debug files (doubled — extra history for connectivity diagnostics)
#define MAX_LOG_FILES_OP 4   // Keep last 4 operation files
#define MAX_LOG_FILES_ED 4    // Keep last 4 energy files

#define LOG_FILE_PREFIX_OP "OP"
#define LOG_FILE_PREFIX_DB "DB"
#define LOG_FILE_PREFIX_ED "ED"

// Queue slots are cheap now that debug messages live on the heap (16 bytes per
// slot instead of 208), so the depth can be generous while using less RAM than
// the old 20-slot queue did.
#define LOG_QUEUE_SIZE 64
// Debug logs refuse to consume the last LOG_QUEUE_RESERVE slots, so a burst of
// debug messages can never crowd out an operation or energy log.
#define LOG_QUEUE_RESERVE 8
typedef struct {
    time_t timestamp;
    uint16_t entryCount;
    uint16_t fileSize;
} LogFileHeader;

typedef enum {
    LOG_TYPE_DEBUG,
    LOG_TYPE_OPERATION,
    LOG_TYPE_ENERGY
} LogType;

// Unified logging message structure
typedef struct {
    LogType type;
    // Event time, captured by the producer where it is already known (most
    // callers already hold the DateTime the event happened at). 0 means "not
    // supplied" - the logging task then reads the RTC when it consumes the
    // entry. Stamping at the source keeps the recorded time accurate no matter
    // how long the entry waits in the queue.
    time_t ts;
    union {
        struct {
            DebugAction action;
            // Heap-allocated by queueDebugLog() and owned by the queue entry;
            // unifiedLoggingTask() frees it once consumed. Keeping it out of the
            // struct is what shrinks a queue slot from 208 bytes to 16. May be
            // NULL if the caller passed no message.
            char *message;
        } debug;
        struct {
            ModeOfAction mode;
            ScheduleType sType;
            uint16_t duration;
            float current;
            float voltage;
        } operation;
        struct {
            time_t startTS;
            time_t endTS;
            float energy;
        } energy;
    } data;
} UnifiedLogMessage;

// Count of log messages that never made it into the queue (queue full, reserve
// threshold hit, or allocation failure). Lets a silent drop be noticed.
extern volatile uint32_t droppedLogCount;

// Global variables for current and previous file timestamps
extern time_t currentDebugFile;
extern time_t previousDebugFile;
extern time_t currentOperationFile;
extern time_t previousOperationFile;
extern time_t currentEnergyFile;
extern time_t previousEnergyFile;

// Shared function declarations
String getLogFileName(LogType type, time_t timestamp);
void createNewLogFile(LogType type);
void cleanupOldLogFiles(LogType type);
std::vector<String> getLogFilesSorted(const String& prefix);
size_t getEntrySize(LogType type);

// Internal logging functions (used by the unified logging task).
// eventTs is the producer-supplied event time; 0 falls back to an RTC read.
void logDebugActionInternal(DebugAction action, const char* message, time_t eventTs = 0);
void logActionInternal(ModeOfAction mode, ScheduleType scheduleType, uint16_t duration, float current, float voltage,
                       time_t eventTs = 0);
void logEnergyDataInternal(DateTime startDT, DateTime endDT, float energy);

// Public API functions for different log types
// String prepareLogsJson(LogType type, int maxEntries);
// void sendAllLogsToMQTT(LogType type);  // disabled - see logging_manager.cpp

// Builds the human-readable operation-log message published to the cloud /
// shown in the dashboard. Mirrors the formatter in data/scripts/script.js so
// live, replay, and web views stay consistent:
//   - fault-stop modes (DRYRUN / OVERRUN / HV / LV) get "(V,A)" appended
//   - start events with a planned duration get " - XH YM" appended
String formatOperationLogMessage(const LogEntry &entry);

// Historical log replay (used on reconnect and by the resendRecentLogs RPC).
// Single-slot: rejects a new request while one is still draining.
// Returns number of entries queued (0 on reject / no matches / bad args).
uint16_t queueReplayRequest(LogType type, time_t fromTs, time_t toTs, uint16_t maxCount);
bool isReplayPending();
// Called by mqttPublisherTask once per tick — pushes at most one entry into
// mqttQueue and only when mqttQueue is idle, so live traffic keeps priority.
void servicePendingReplay();

// Queue functions for unified logging
void queueDebugLog(DebugAction action, const char *message, time_t ts = 0);
void queueOperationLog(ModeOfAction mode,  ScheduleType scheduleType, uint16_t duration, float current, float voltage,
                       time_t ts = 0);
void queueEnergyLog(DateTime startDT, DateTime endDT, float energy);

// Initialization
void initUnifiedLogQueue();
void initUnifiedLogging();
void initializeLogFileTracking(LogType type);

// Debug functions
void listAllLogFiles();
void testFileDeletion(const String& filename);

// Migration functions
String migrateOldLogFiles();
String migrateOperationLogs();
String migrateDebugLogs();
String migrateEnergyLogs();
void clearLogsByType(LogType type);

// Migration check functions
bool shouldMigrateOperationLogs();
bool shouldMigrateDebugLogs();
bool shouldMigrateEnergyLogs();

// Helper functions
bool isValidTimestamp(time_t timestamp, time_t currentTs);

void sendHomeAssistantStatusMessage(const LogEntry& );