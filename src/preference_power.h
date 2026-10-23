#ifndef PREFERENCE_POWER_H
#define PREFERENCE_POWER_H

#include "globals.h"
#include "power_monitoring.h"
#include "preference_management.h"

// Function declarations
bool readFaultDetails();
void updateFaultDetails();
void clearFaultDetails();

void readPowerSettings();
void updatePowerSettings(PowerSettings pSet);
void clearPowerSettings();

void dumpVitalsBeforeFlat(DateTime schStartTime, unsigned long relayOnTimeRTC, DateTime curDateTime, float energy);

// Reads back the session that dumpVitalsBeforeFlat() wrote on the way down.
// Returns true only when a usable session is stored (both timestamps present),
// and hands the values to the caller rather than parking them in globals -
// initEnergyData() consumes them immediately and nothing else needs them.
bool retrieveEnergyDump(unsigned long &relayStartTS, unsigned long &currentTS, float &energyConsumed);

// Drops the stored dump once it has been consumed.
void clearEnergyDump();

#endif // PREFERENCE_POWER_H