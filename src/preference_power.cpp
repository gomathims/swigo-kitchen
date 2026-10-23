#include "preference_power.h"
#include "preference_management.h"
#include "timesettings.h"

// Global Preferences object

bool readFaultDetails() {
    preferences.begin("fault-data", true);
    Serial.print("Inside readFaultDetails  ");
    lastManualOffTime = DateTime(preferences.getLong64("manualOff", 0));
    lastLowVoltageTime = DateTime(preferences.getLong64("lowVoltage", 0));
    lastHighVoltageTime = DateTime(preferences.getLong64("highVoltage", 0));
    lastDryRunTime = DateTime(preferences.getLong64("dryrun", 0));
    lastOverRunTime = DateTime(preferences.getLong64("overload", 0));
    lastPhaseFailureTime = DateTime(preferences.getLong64("phaseFailure", 0));
    lastPhaseSequenceReverseTime = DateTime(preferences.getLong64("phaseSequenceReverse", 0));
    preferences.end();
    return false;
}

void updateFaultDetails() {
    preferences.begin("fault-data", false);
    if (preferences.getLong64("manualOff", 0) != lastManualOffTime.unixtime()) {
        preferences.putLong64("manualOff", lastManualOffTime.unixtime());
    }
    if (preferences.getLong64("lowVoltage", 0) != lastLowVoltageTime.unixtime()) {
        preferences.putLong64("lowVoltage", lastLowVoltageTime.unixtime());
    }
    if (preferences.getLong64("highVoltage", 0) != lastHighVoltageTime.unixtime()) {
        preferences.putLong64("highVoltage", lastHighVoltageTime.unixtime());
    }
    if (preferences.getLong64("dryrun", 0) != lastDryRunTime.unixtime()) {
        preferences.putLong64("dryrun", lastDryRunTime.unixtime());
    }
    if (preferences.getLong64("overload", 0) != lastOverRunTime.unixtime()) {
        preferences.putLong64("overload", lastOverRunTime.unixtime());
    }
    if (preferences.getLong64("phaseFailure", 0) != lastPhaseFailureTime.unixtime()) {
        preferences.putLong64("phaseFailure", lastPhaseFailureTime.unixtime());
    }
    if (preferences.getLong64("phaseSequenceReverse", 0) != lastPhaseSequenceReverseTime.unixtime()) {
        preferences.putLong64("phaseSequenceReverse", lastPhaseSequenceReverseTime.unixtime());
    }
    preferences.end();
}

void clearFaultDetails() {
    preferences.begin("fault-data", false);
    preferences.clear();
    preferences.end();
}

void readPowerSettings() {
    preferences.begin("power-settings", true);
    Serial.print("Inside readPowerSettings ");
    dryRunCurrentLimit = preferences.getFloat("drCur", DEFAULT_DRY_RUN_CURRENT_LIMIT);
    dryRunCurrentThreshold = preferences.getFloat("thCur", 0.0);
    overRunCurrentLimit = preferences.getFloat("orCur", DEFAULT_OVER_RUN_CURRENT_LIMIT);
    lowVoltageLimit = preferences.getFloat("lVol", DEFAULT_LOW_VOLTAGE_LIMIT);
    highVoltageLimit = preferences.getFloat("hVol", DEFAULT_HIGH_VOLTAGE_LIMIT);

    dryRunRetryMinutes = preferences.getUShort("drMin", DEFAULT_RETRY_MINS);
    overRunRetryMinutes = preferences.getUShort("orMin", DEFAULT_RETRY_MINS);
    lowVoltageRetryMinutes = preferences.getUShort("lVolMin", DEFAULT_RETRY_MINS);
    highVoltageRetryMinutes = preferences.getUShort("hVolMin", DEFAULT_RETRY_MINS);
    startDelayMins = preferences.getUShort("stDly", DEFAULT_RESTART_DELAY_MINS);
    maxRunTimeMins = preferences.getUShort("maxRun", 0);
    breakTimeMins = preferences.getUShort("breakTime", 0);

    restartOptions = preferences.getUShort("resOpt", 0);
    isRestartEnabled = restartOptions & 0x01;
    isSlaveControlDevice = preferences.getUShort("isSlave", 0);
    preferences.end();
}

void updatePowerSettings(PowerSettings pSet) {
    preferences.begin("power-settings", false);
    preferences.putFloat("drCur", pSet.drCur);
    preferences.putFloat("thCur", pSet.thCur);
    preferences.putFloat("orCur", pSet.orCur);
    preferences.putFloat("lVol", pSet.lVol);
    preferences.putFloat("hVol", pSet.hVol);

    preferences.putUShort("drMin", pSet.drMin);
    preferences.putUShort("orMin", pSet.orMin);
    preferences.putUShort("lVolMin", pSet.lVolMin);
    preferences.putUShort("hVolMin", pSet.hVolMin);
    preferences.putUShort("stDly", pSet.stDly);
    preferences.putUShort("maxRun", pSet.maxRunTimeMins);
    preferences.putUShort("breakTime", pSet.breakTimeMins);

    preferences.putUShort("resOpt", pSet.resOpt);
    preferences.putUShort("isSlave", pSet.isSlave);
    preferences.end();
}

void clearPowerSettings() {
    preferences.begin("power-settings", false);
    preferences.clear();
    preferences.end();
}

// Unix seconds are 32-bit values everywhere else in this firmware
// (DateTime::unixtime() returns uint32_t, readEnergyData() stores them with
// putLong), so these are stored the same way rather than as 64-bit entries.
void dumpVitalsBeforeFlat(DateTime schStartTime, unsigned long relayOnTimeRTC, DateTime curDateTime, float energy) {
    preferences.begin("Energy-dump", false);
    // preferences.putLong("schStart", (int32_t)schStartTime.unixtime());
    preferences.putLong("relayStart", (int32_t)relayOnTimeRTC);
    preferences.putLong("curTime", (int32_t)curDateTime.unixtime());
    preferences.putFloat("energy", energy);
    preferences.end();
}

bool retrieveEnergyDump(unsigned long &relayStartTS, unsigned long &currentTS, float &energyConsumed) {
    preferences.begin("Energy-dump", true);
    // uint32_t schStart = (uint32_t)preferences.getLong("schStart", 0);
    uint32_t relayStart = (uint32_t)preferences.getLong("relayStart", 0);
    uint32_t curTime = (uint32_t)preferences.getLong("curTime", 0);
    float energy = preferences.getFloat("energy", 0);
    preferences.end();

    // Serial.println("=== Energy dump saved before the last power cut ===");
    // // Serial.print("Schedule start time : ");
    // // Serial.println(formatDateTime(DateTime((uint32_t)schStart)));
    // Serial.print("Relay start time    : ");
    // Serial.println(formatDateTime(DateTime((uint32_t)relayStart)));
    // Serial.print("Current time (saved) : ");
    // Serial.println(formatDateTime(DateTime((uint32_t)curTime)));
    // Serial.print("Energy (saved)      : ");
    // Serial.println(energy);
    if ( relayStart == 0 && curTime == 0) {
        return false; // nothing saved yet
    }
    relayStartTS = (unsigned long)relayStart;
    currentTS = (unsigned long)curTime;
    energyConsumed = energy;

    

    // Only a session with both ends recorded can be logged as a run.
    return relayStart != 0 && curTime != 0;
}

// The dump is a one-shot handover from the brown-out to the next boot. Once
// initEnergyData() has logged that session it has to go: the zero-cross ISR is
// live from setupPins() onwards, so zcrossTime is set within milliseconds of
// every boot, and a dump left behind would be logged again on each restart.
void clearEnergyDump() {
    preferences.begin("Energy-dump", false);
    preferences.clear();
    preferences.end();
}