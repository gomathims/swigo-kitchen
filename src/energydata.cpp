#include "energydata.h"
#include "logging_manager.h"



// Function implementations
void formatTime(char *buffer, size_t bufferSize, time_t time) {
    strftime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S", localtime(&time));
}


void clearEnergyLogs() {
    // File logFile = LittleFS.open(energyDataFileName, "w");
    // if (!logFile) {
    //     Serial.println("Failed to open log file for clearing");
    //     return;
    // }
    // logFile.close();

    // // Reset pointers
    // energyPointers.start = 0;
    // energyPointers.end = 0;
    // energyPointers.count = 0;
    // saveEnergyPointers();
    clearLogsByType(LOG_TYPE_ENERGY); 
}

void logEnergyData(DateTime startDT, DateTime endDT, float energy) {
    // Use the unified logging system
    queueEnergyLog(startDT, endDT, energy);
}

void initEnergyData() {
    unsigned long startTS = 0, endTS = 0;
    float energy = 0;

    bool dumpUsable = retrieveEnergyDump(startTS, endTS, energy);

    if (zcrossTime && dumpUsable) {
        if (startTS != 0 && endTS != 0) {
            logDebugAction(DebugAction::SYSTEM_INFO, "Successful EnergyDump retrieval");
        }
        Serial.printf("Coming inside of poweroff sch\n");
    
    } else {
        readEnergyData(startTS, endTS, energy);
        Serial.printf("Coming inside of normal sch\n");
    }
    // Both ends must be present - an entry with a zero timestamp is a session
    // that was never really recorded, and it would still be counted as a run by
    // getElapsedEnergyDurationInWindow().
    if (startTS != 0 && endTS != 0) {
        logEnergyData(DateTime(startTS), DateTime(endTS), energy);
    }
    clearEnergyDump();
    resetTempEnergyData();
}

void prepareEnergyDataJson(JsonDocument &doc) {
    // Only use currentEnergyFile and previousEnergyFile
    doc["logArray"].to<JsonArray>();
    JsonArray logArray = doc["logArray"].as<JsonArray>();
    int entriesRead = 0;
    int maxEntries = MAX_LOG_ENTRIES_TO_UI;
    // Helper lambda to read from a file
    auto readLogFile = [&](time_t fileTs) {
        if (fileTs == 0) return;
        String filename = getLogFileName(LOG_TYPE_ENERGY, fileTs);
        File file = LittleFS.open(filename, "r");
        if (!file) return;
        
        // Calculate entry count from file size (no header)
        size_t fileSize = file.size();
        size_t entryCount = fileSize / sizeof(EnergyEntry);
        
        Serial.printf("Energy file: %s, %d entries, file size: %d bytes\n", 
                     filename.c_str(), entryCount, fileSize);
        
        for (int i = entryCount - 1; i >= 0 && entriesRead < maxEntries; i--) {
            file.seek(i * sizeof(EnergyEntry));
            EnergyEntry entry;
            file.read((uint8_t *)&entry, sizeof(EnergyEntry));
            JsonObject logEntry = logArray.add<JsonObject>();
            char timeBuffer[20];
            formatTime(timeBuffer, sizeof(timeBuffer), entry.startTS);
            logEntry["startTS"] = timeBuffer;
            formatTime(timeBuffer, sizeof(timeBuffer), entry.endTS);
            logEntry["endTS"] = timeBuffer;
            logEntry["energy"] = entry.energy;
            entriesRead++;
        }
        file.close();
    };
    readLogFile(currentEnergyFile);
    if (entriesRead < maxEntries) {
        readLogFile(previousEnergyFile);
    }
}

unsigned long getElapsedEnergyDurationInWindow(DateTime windowStart, DateTime asOf) {
    time_t winStart = windowStart.unixtime();
    Serial.println("inside getElapsedEnergyDurationInWindow");
    Serial.println(formatDateTime(windowStart));
    Serial.println(formatDateTime(asOf));

    time_t winEnd = asOf.unixtime();
    unsigned long totalSeconds = 0;

    // Entries are only ever appended, so a file holds them in chronological
    // order - and sessions never overlap (the relay cannot run twice at once),
    // so endTS rises along with startTS. Walking backwards from the newest
    // therefore lands on the window's segments straight away and can stop at the
    // first entry that ENDED at or before the window opened: everything further
    // back ended earlier still. The stop is on endTS, not startTS, because a
    // session that began before the window can still have run inside it.
    // Returns true when it stopped that way: the window is then fully covered
    // and the older file need not be opened at all.
    auto scanFileBackwards = [&](time_t fileTs) -> bool {
        if (fileTs == 0) return false;
        String filename = getLogFileName(LOG_TYPE_ENERGY, fileTs);
        File file = LittleFS.open(filename, "r");
        if (!file) return false;

        const size_t CHUNK = 8; // entries per read - one seek+read instead of eight
        EnergyEntry entries[CHUNK];
        size_t remaining = file.size() / sizeof(EnergyEntry);
        bool reachedBeforeWindow = false;

        while (remaining > 0 && !reachedBeforeWindow) {
            size_t count = (remaining < CHUNK) ? remaining : CHUNK;
            size_t firstIndex = remaining - count; // the chunk's oldest entry
            size_t bytesWanted = count * sizeof(EnergyEntry);
            file.seek(firstIndex * sizeof(EnergyEntry));
            if (file.read((uint8_t *)entries, bytesWanted) != bytesWanted) {
                break; // short/failed read - don't trust what's behind it
            }
            for (size_t i = count; i-- > 0;) {
                const EnergyEntry &entry = entries[i];
                if (entry.endTS <= winStart) {
                    // Finished before the window opened, and so did everything
                    // before it.
                    reachedBeforeWindow = true;
                    break;
                }
                // Count the part of the session that falls inside the window,
                // not the whole session: a run already going when the schedule
                // opened - a button press, say - still delivers ON time from the
                // window start onwards, and only that part counts towards the
                // owed duration. The far end is clipped at "now" the same way,
                // so a session lying entirely beyond the window adds nothing
                // without stopping the walk.
                time_t segStart = (entry.startTS > winStart) ? entry.startTS : winStart;
                time_t segEnd = (entry.endTS < winEnd) ? entry.endTS : winEnd;
                if (segEnd > segStart) {
                    totalSeconds += (unsigned long)(segEnd - segStart);
                }
            }
            remaining = firstIndex;
        }
        file.close();
        return reachedBeforeWindow;
    };

    // currentEnergyFile/previousEnergyFile are the only two kept on disk
    // (see cleanupOldLogFiles) - a window's segments can only ever land in
    // one of these two, even across a rotation mid-window. The segments sit at
    // the tail of the current file in all but that rotation case, so the older
    // file is opened only when the scan ran off the start of the current one
    // while still inside the window.
    if (!scanFileBackwards(currentEnergyFile)) {
        scanFileBackwards(previousEnergyFile);
    }
    Serial.println(totalSeconds);
    return totalSeconds;
}
