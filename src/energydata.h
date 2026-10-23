#ifndef ENERGYDATA_H
#define ENERGYDATA_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <RTClib.h>

#include <unordered_map>

#include "preference_management.h"
#include "preference_power.h"

// Constants
#define ENERGY_LOG_SIZE_LIMIT 2 * 1024  // 200 Kilo Bytes

// Function declarations
void formatTime(char* buffer, size_t bufferSize, time_t time);
void energyFSSetup();
void clearEnergyLogs();
void logEnergyData(DateTime startDT, DateTime endDT, float energy = 0.0);
void initEnergyData();
// Fills the provided document with energy entries. Caller serializes it
// directly into the HTTP response stream — no intermediate String alloc.
void prepareEnergyDataJson(JsonDocument &doc);
// Sums how much of each logged energy entry overlaps [windowStart, asOf] -
// i.e. every ON segment that belongs to this schedule occurrence so far,
// across any number of power-cut interruptions. A session is clipped to the
// window rather than counted or dropped whole, so a run already going when
// the window opened (a button press before the schedule, say) contributes
// only the part that ran inside it. Searches both the current and previous energy
// log files, since a long-running window's segments can straddle a file
// rotation. Returns the total in whole seconds - both stamps are second
// resolution and nothing is rounded to minutes anywhere along the way, so a
// 9m30s segment is owed back as 570s, not 540.
unsigned long getElapsedEnergyDurationInWindow(DateTime windowStart, DateTime asOf);

// Structs
struct EnergyEntry {
  time_t startTS;
  time_t endTS;
  float energy;
};

// Pointers for circular buffer
struct EnergyPointers {
  size_t start;
  size_t end;
  size_t count;
};

// Global variables
extern const char* energyDataFileName;
extern const char* energypointersFileName;
extern EnergyPointers energyPointers;

#endif  // ENERGYDATA_H
