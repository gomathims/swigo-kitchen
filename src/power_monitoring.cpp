#include "power_monitoring.h"
#include "buttonpress.h"
#include "vsmr_link.h"

float voltage = 0;
float current = 0;
float power = 0;
float pf = 0;
float energy = 0;

float voltageToOutput = 0;
float currentToOutput = 0;
float powerToOutput = 0;
float energyToOutput = 0;

float dryRunCurrentLimit = 0;
float overRunCurrentLimit = 0;
float lowVoltageLimit = 0;
float highVoltageLimit = 0;
float dryRunCurrentThreshold = 0;
DateTime lastManualOffTime, lastDryRunTime, lastOverRunTime, lastLowVoltageTime, lastHighVoltageTime,
    lastPhaseFailureTime, lastPhaseSequenceReverseTime;

// float currentPrevious = 0;
// float voltagePrevious = 0;

unsigned long dryRunStartTime = 0;
unsigned long overRunStartTime = 0;
unsigned long lowVoltageStartTime = 0;
unsigned long highVoltageStartTime = 0;
unsigned long factoryResetStartTime = 0;
unsigned long disableWiFiStartTime = 0;
unsigned long vsmrPairStartTime = 0;
unsigned long floatLowStartTime = 0;
unsigned long bleEnableStartTime = 0;

// Phase fault monitoring variables
volatile bool phaseFailureAlertState = false;
volatile bool phaseSequenceReverseAlertState = false;

// BLE enable button press flag
volatile bool buttonBLEEnablePressed = false;
// Set when the BLE-enable combo muted Button 1/2; cleared by checkEnableBLE()
// once both are physically released, which re-arms them.
volatile bool bleComboRearmPending = false;
unsigned long flow_start_time = 0;
unsigned long flow_stop_time = 0;
uint16_t flowDetectionDelaySec = 10; // Default 10 seconds
uint16_t flowStopDelaySec = 10;      // Default 10 seconds

bool isThreePhaseDevice;
uint8_t tappingRequired;
uint8_t tappingDuration;
uint8_t ledType;
String shuntOrCT;
uint8_t masterSlaveMode = 0;
String masterDeviceUrl = "";
String mPin = "";
String remoteMethods = "";

unsigned long tappingStartTime;
unsigned long lastUIUpdateTime = 0;

void checkFloat() {
    // static bool oldFloatingAlertState = false;
    uint8_t floatPinState = digitalRead(FLOAT_SWITCH);
    if ((floatPinState == HIGH) && (floatLowStartTime == 0)) {
        floatLowStartTime = millis();
    } else if ((floatPinState == HIGH) && !floatingAlertState && (floatLowStartTime != 0)
               && (millis() - floatLowStartTime > TEN_SECS)) {
        floatingAlertState = true;
        // Log when floatingAlertState is set, only in remote mode
        if (masterSlaveMode == 2) {
            logAction(ModeOfAction::STOP_FLOAT, 0, 0, 0, ScheduleType::SCH_NONE);
        }
    } else if (floatPinState == LOW) {
        floatLowStartTime = 0;
        floatingAlertState = false;
    }
    // oldFloatingAlertState = floatingAlertState;
}

void checkFlow() {
    // Check flow function for the relay on when flow signal is high
    // static bool oldFlowIncomingState = false;
    uint8_t floatPinState = digitalRead(FLOAT_SWITCH);

    /*This if portion is to detect the flow is there*/
    if (floatPinState == HIGH) {
        if (flow_start_time == 0 && !flowIncomingState) {
            flow_start_time = millis();
        } else if (!flowIncomingState && flow_start_time > 0
                   && ((millis() - flow_start_time) >= (flowDetectionDelaySec * 1000))) {
            flowIncomingState = true;
            flow_start_time = 0;
            Flow_stopped_manual = false;
            // Log when flowIncomingState is set to true
            logAction(ModeOfAction::FLOW_DETECTED, 0, 0, 0, ScheduleType::SCH_NONE);
            if (masterSlaveMode == 2) {
                lastUIUpdateTime = millis() - 9000; // To trigger next update to UI in a second.
            }
        }
        flow_stop_time = 0;
    }
    // else if (floatPinState == LOW && flowIncomingState == false) {
    //     // Detect flow; if fluctuation occurs, reset value to zero. After OFF
    //     // button is pressed, flow detection should only trigger once flow fully
    //     // stops for 10 seconds, preventing early detection
    //     flow_start_time = 0;
    // }
    /*This if portion is to detect the flow is not there*/
    if (floatPinState == LOW) {
        if (flow_stop_time == 0 && flowIncomingState) {
            flow_stop_time = millis();
        } else if (flowIncomingState && flow_stop_time > 0
                   && ((millis() - flow_stop_time) > (flowStopDelaySec * 1000))) {
            flowIncomingState = false;
            flow_stop_time = 0;
            Flow_stopped_manual = false;
            // Log when flowIncomingState is reset to false
            logAction(ModeOfAction::FLOW_STOPPED, 0, 0, 0, ScheduleType::SCH_NONE);
            if (masterSlaveMode == 2) {
                lastUIUpdateTime = millis() - 9000; // To trigger next update to UI in a second.
            }
        }
        flow_start_time = 0;
    }
    // oldFlowIncomingState = flowIncomingState;
}

void checkPhaseFaults(DateTime curDateTime) {
    static bool oldPhaseSequenceReverseAlertState = false;
    static bool oldPhaseFailureAlertState = false;

    bool phaseFailure = LOW;
    bool sequenceReverse = LOW;
    if (vsmrLinkEnabled()) {
        // 3-Phase with VSMR: faults come from the VSMR status flags instead of
        // the phase-monitor GPIOs. Bits 0/1/2 = phase failure, bit 3 = sequence
        // reverse. A stale link (no fresh data) reads as no fault -> alerts clear.
        float vR, vY, vB;
        uint8_t vFlags;
        bool vRelay;
        bool fresh = vsmrLinkGetVoltages(vR, vY, vB, vFlags, vRelay);
        sequenceReverse = fresh && (vFlags & 0x08);
        // The branch logic below expects both inputs HIGH for a sequence alert.
        phaseFailure = (fresh && (vFlags & 0x07)) || sequenceReverse;

        // // Trace raw VSMR flags on every change so the fault ordering (e.g. a
        // // transient sequence bit ahead of a phase failure) can be correlated
        // // with the operation log entries.
        // static uint8_t lastDbgFlags = 0;
        // static bool lastDbgFresh = false;
        // if (fresh != lastDbgFresh || (fresh && vFlags != lastDbgFlags)) {
        //     char dbgBuf[64];
        //     snprintf(dbgBuf, sizeof(dbgBuf), "VSMR vFlags=0x%02X fresh=%d relay=%d", vFlags, fresh, vRelay);
        //     Serial.printf("[VLINK] %s\n", dbgBuf);
        //     logDebugAction(DebugAction::SYSTEM_INFO, dbgBuf);
        //     lastDbgFlags = vFlags;
        //     lastDbgFresh = fresh;
        // }
    }
    // else {
    //     phaseFailure = digitalRead(PHASE_FAILURE);
    //     sequenceReverse = digitalRead(PHASE_SEQUENCE_REVERSE);
    // }

    //   Serial.println("phaseFailure");
    //   Serial.println(phaseFailure);
    //   Serial.println("sequenceReverse");
    //   Serial.println(sequenceReverse);

    // Generally, sequence reverse trigger will not come when power is already on.
    // Phase failure trigger will come when power is on, but when the motor is off only.
    // Due to reverse voltage, phase failure will be triggered when ON.
    // So, we need to only log the events of phase failure or reversal.
    // But in future, if we get the phase failure when the motor is ON, we are handing in handleSwitching()

    if (phaseFailure == HIGH && sequenceReverse == HIGH) {
        // Serial.println("Inside phaseFailure && sequenceReverse");
        if (oldPhaseSequenceReverseAlertState == false) {
            Serial.println("Phase Sequence Reverse Alert is set");
            // digitalWrite(RELAY_OUT, LOW);
            phaseSequenceReverseAlertState = true;
            logAction(ModeOfAction::PHASE_SEQUENCE_REVERSE_ALERT, 0, 0, 0, ScheduleType::SCH_NONE);
        }

    } else if (phaseFailure == HIGH) {
        // Serial.println("Inside phaseFailure ");
        phaseSequenceReverseAlertState = false;

        if (oldPhaseFailureAlertState == false) {
            Serial.println("Phase Failure Alert State is false");
            // digitalWrite(RELAY_OUT, LOW);
            phaseFailureAlertState = true;
            logAction(ModeOfAction::PHASE_FAILURE_ALERT, 0, 0, 0, ScheduleType::SCH_NONE);
        }
    } else {
        phaseSequenceReverseAlertState = false;
        phaseFailureAlertState = false;
        // Fault gone - arm the reject log again for the next episode, the way the
        // low/high voltage recovery paths clear their own Logged flags.
        phaseAlertLogged = false;
    }

    // Hold the fault timestamp at "now" for as long as the fault is asserted, the
    // way monitorPower() keeps re-stamping lastLowVoltageTime while the voltage is
    // down. The restart gate in handleSwitching() counts lowVoltageRetryMinutes
    // from this stamp, so without it the retry is measured from when the phase
    // went away instead of from when it came back - the attempt then falls inside
    // the outage, gets refused by handleRelayOn(), and the eventual restart lands
    // with no delay at all. RAM only: the NVS copy is written by
    // updateFaultDetails() at relay-off, not every cycle.
    if (phaseFailureAlertState) {
        lastPhaseFailureTime = curDateTime;
    }
    if (phaseSequenceReverseAlertState) {
        lastPhaseSequenceReverseTime = curDateTime;
    }

    if (phaseSequenceReverseAlertState == false && oldPhaseSequenceReverseAlertState == true) {
        Serial.println("Phase Sequence Reverse Alert is cleared");
        logAction(ModeOfAction::PHASE_SEQUENCE_REVERSE_ALERT_CLEARED, 0, 0, 0, ScheduleType::SCH_NONE);
    }
    if (phaseFailureAlertState == false && oldPhaseFailureAlertState == true) {
        Serial.println("Phase Failure Alert is cleared");
        logAction(ModeOfAction::PHASE_FAILURE_ALERT_CLEARED, 0, 0, 0, ScheduleType::SCH_NONE);
    }
    if ((phaseFailureAlertState != oldPhaseFailureAlertState)
        || (phaseSequenceReverseAlertState != oldPhaseSequenceReverseAlertState)) {
        notifyClients();
        lastUIUpdateTime = millis();
    }
    oldPhaseSequenceReverseAlertState = phaseSequenceReverseAlertState;
    oldPhaseFailureAlertState = phaseFailureAlertState;
}

void monitorPower(DateTime curDateTime) {
    power_sensing_loop();
    static bool negativeEnergyLogged = false;
    if (relayState) {
        if (isSlaveControlDevice) {
            if ((current > dryRunCurrentThreshold) && (current < dryRunCurrentLimit) && (dryRunStartTime == 0)) {
                dryRunStartTime = millis();
            } else if ((current > dryRunCurrentThreshold) && (current < dryRunCurrentLimit) && (dryRunStartTime != 0)
                       && (millis() - dryRunStartTime > DRYRUN_MONITOR_DURATION)) {
                if ((millis() - relayOnTime) > DRYRUN_CURRENT_SETTLE_DURATION) {
                    dryRunAlertState = true;
                }
            } else if (current >= dryRunCurrentLimit || current <= dryRunCurrentThreshold) {
                dryRunStartTime = 0;
                dryRunAlertState = false;
            }
        } else {
            if ((current < dryRunCurrentLimit) && (dryRunStartTime == 0)) {
                dryRunStartTime = millis();
            } else if ((current < dryRunCurrentLimit) && (dryRunStartTime != 0)
                       && (millis() - dryRunStartTime > DRYRUN_MONITOR_DURATION)) {
                if ((millis() - relayOnTime) > DRYRUN_CURRENT_SETTLE_DURATION) {
                    dryRunAlertState = true;
                }
            } else if (current >= dryRunCurrentLimit) {
                dryRunStartTime = 0;
                dryRunAlertState = false;
            }
        }

        if ((current > overRunCurrentLimit) && (overRunStartTime == 0)) {
            overRunStartTime = millis();
        } else if ((current > overRunCurrentLimit) && (overRunStartTime != 0)
                   && (millis() - overRunStartTime > OVERLOAD_MONITOR_DURATION)) {
            if ((millis() - relayOnTime) > (INRUSH_CURRENT_SETTLE_DURATION + (unsigned long)inRushExtraSecs * 1000UL)) {
                overRunAlertState = true;
            }
        } else if (current <= overRunCurrentLimit) {
            overRunStartTime = 0;
            overRunAlertState = false;
        }

        currentToOutput = current;
        powerToOutput = power;
        if (energy < energyOnStart) {
            if (!negativeEnergyLogged) {
                char msg[DEBUG_MESSAGE_SIZE];
                snprintf(msg, sizeof(msg), "Energy below energyOnStart: energy=%.3f energyOnStart=%.3f", energy,
                         energyOnStart);
                logDebugAction(DebugAction::SYSTEM_FAILURE, msg);
                negativeEnergyLogged = true;
            }
            // Sensor glitch / chip-register decrease: skip this update and keep
            // the previous energyToOutput. Missing a single reading is harmless.
        } else {
            // Glitch frames are already rejected at the source (Decode()'s
            // CF_CNT plausibility filter), and the guard above makes this
            // delta always >= 0 — no start-up blanking window needed (the old
            // 10s gate only existed to hide negative transients after
            // relay-on).
            energyToOutput = energy - energyOnStart;
        }

    } else {
        overRunStartTime = 0;
        overRunAlertState = false;
        dryRunStartTime = 0;
        dryRunAlertState = false;
        currentToOutput = 0;
        powerToOutput = 0;
        energyToOutput = 0;
        negativeEnergyLogged = false;
    }

    if ((voltage < lowVoltageLimit) && (lowVoltageStartTime == 0)) {
        lowVoltageStartTime = millis();
    } else if ((voltage < lowVoltageLimit) && (lowVoltageStartTime != 0)
               && (millis() - lowVoltageStartTime > FAULT_MONITOR_DURATION)) {
        lowVoltageAlertState = true;
        lastLowVoltageTime = curDateTime;
    } else if (voltage >= lowVoltageLimit) {
        lowVoltageStartTime = 0;
        lowVoltageAlertState = false;
        lowVoltageAlertLogged = false;
    }

    if ((voltage > highVoltageLimit) && (highVoltageStartTime == 0)) {
        highVoltageStartTime = millis();
    } else if ((voltage > highVoltageLimit) && (highVoltageStartTime != 0)
               && (millis() - highVoltageStartTime > FAULT_MONITOR_DURATION)) {
        highVoltageAlertState = true;
        lastHighVoltageTime = curDateTime;
    } else if (voltage <= highVoltageLimit) {
        highVoltageStartTime = 0;
        highVoltageAlertState = false;
        highVoltageAlertLogged = false;
    }
    voltageToOutput = voltage;

    // The 1-second cadence exists only to animate a countdown that notifyClients() is
    // actually going to send. Every float/flow clause below is therefore gated on
    // isFloatEnabled exactly the way the matching payload fields are gated in
    // notifyClients() - without that guard a stale timer from a disabled mode keeps
    // the UI at 1 Hz while the message carries no float/flow field at all.
    bool floatCountdownActive = (isFloatEnabled == 1) && (floatLowStartTime != 0) && !floatingAlertState;
    bool flowCountdownActive = (isFloatEnabled == 2) && (flow_start_time != 0 || flow_stop_time != 0);

    // VSMR frame changed materially (>= 2 V on a single phase, or flags/relay
    // change - detected in the RX callback): refresh the local UI immediately.
    static uint32_t lastVsmrChangeNotified = 0;
    bool vsmrChanged = vsmrLinkEnabled() && (vsmrLinkChangeCount() != lastVsmrChangeNotified);

    if (millis() - lastUIUpdateTime > 10000
        || ((highVoltageStartTime != 0 || lowVoltageStartTime != 0 || dryRunStartTime != 0 || overRunStartTime != 0
             || (isFloatEnabled == 1 && floatLowStartTime != 0))
            && relayState == 1 && millis() - lastUIUpdateTime > 1000)
        || ((floatCountdownActive || flowCountdownActive || logRefreshRequired)
            && millis() - lastUIUpdateTime > 1000) //&& relayState == 1
        || vsmrChanged) {
        notifyClients();
        // Every notify carries the latest VSMR readings, so mark them seen.
        lastVsmrChangeNotified = vsmrLinkChangeCount();
        lastUIUpdateTime = millis();
    }
}

void checkFactoryReset() {
    uint8_t switch2State = digitalRead(SWITCH_2);
    uint8_t switch3State = digitalRead(SWITCH_3);
    uint8_t switchOffState = digitalRead(SWITCH_OFF);

    if ((switch2State == HIGH) && (switch3State == HIGH) && (switchOffState == HIGH) && (factoryResetStartTime == 0)) {
        factoryResetStartTime = millis();
    } else if ((switch2State == HIGH) && (switch3State == HIGH) && (switchOffState == HIGH)
               && (factoryResetStartTime != 0) && (millis() - factoryResetStartTime > FACTORY_RESET_PRESS_DURATION)) {
        buttonFactoryResetPressed = true;
        factoryResetLedIndicate = true;
    } else if ((switch2State == LOW) || (switch3State == LOW) || (switchOffState == LOW)) {
        factoryResetStartTime = 0;
    }
}

void checkDisableWiFi() {
    uint8_t switch4State = digitalRead(SWITCH_4);
    uint8_t switchOffState = digitalRead(SWITCH_OFF);

    if ((switch4State == HIGH) && (switchOffState == HIGH) && (disableWiFiStartTime == 0)) {
        disableWiFiStartTime = millis();
    } else if ((switch4State == HIGH) && (switchOffState == HIGH) && (disableWiFiStartTime != 0)
               && (millis() - disableWiFiStartTime > DISABLE_WIFI_PRESS_DURATION)) {
        buttonDisableWiFiPressed = true;
        disableWiFiLedIndicate = true;
        disableWiFiStartTime = 0;
    } else if ((switch4State == LOW) || (switchOffState == LOW)) {
        disableWiFiStartTime = 0;
        buttonDisableWiFiPressed = false;
    }
}

// Button 1 alone (all other buttons released) held 5 s -> start the VSMR
// beacon/pairing broadcast so a re-learning VSMR can (re)discover this device.
void checkVsmrPairing() {
    uint8_t switch1State = digitalRead(SWITCH_1);
    uint8_t switch2State = digitalRead(SWITCH_2);
    uint8_t switch3State = digitalRead(SWITCH_3);
    uint8_t switch4State = digitalRead(SWITCH_4);
    uint8_t switchOffState = digitalRead(SWITCH_OFF);

    if ((switch1State == HIGH) && (switch2State == LOW) && (switch3State == LOW) && (switch4State == LOW)
        && (switchOffState == LOW) && (vsmrPairStartTime == 0)) {
        vsmrPairStartTime = millis();
    } else if ((switch1State == HIGH) && (switch2State == LOW) && (switch3State == LOW) && (switch4State == LOW)
               && (switchOffState == LOW) && (vsmrPairStartTime != 0)
               && (millis() - vsmrPairStartTime > VSMR_PAIR_PRESS_DURATION)) {
        vsmrLinkStartPairing();
        vsmrPairLedIndicate = true; // LED_1 steady glow for 2 s (handleLEDBlink)
        ignoreButton1FallOnce = true;
        vsmrPairStartTime = 0; // one-shot until released
        Serial.println("VSMR pairing broadcast triggered by Button 1 (5 sec hold)");
    } else if ((switch1State == LOW) || (switch2State == HIGH) || (switch3State == HIGH) || (switch4State == HIGH)
               || (switchOffState == HIGH)) {
        vsmrPairStartTime = 0;
    }
}

void checkEnableBLE() {
    uint8_t switch1State = digitalRead(SWITCH_1);
    uint8_t switch2State = digitalRead(SWITCH_2);
    uint8_t switch3State = digitalRead(SWITCH_3);
    uint8_t switch4State = digitalRead(SWITCH_4);
    uint8_t switchOffState = digitalRead(SWITCH_OFF);

    // Re-arm Button 1 / Button 2 once the BLE-enable combo is let go. BLE starts
    // while both are still held, so their release is swallowed (ignore flags) to
    // keep it from switching a load. Unlike the factory-reset and disable-WiFi
    // combos, nothing clears those flags afterwards: their reset rides on the
    // RESET LED blink, and handleLEDBlink() returns early the whole time BLE is
    // advertising - so both buttons stayed dead until the next reboot.
    if (bleComboRearmPending && (switch1State == LOW) && (switch2State == LOW)) {
        ignoreButton1FallOnce = false;
        ignoreButton2FallOnce = false;
#ifdef SPECIAL_DEBOUNCE_DELAY
        buttonRiseTime1 = 0;
        buttonRiseTime2 = 0;
#endif
        bleComboRearmPending = false;
        Serial.println("Button 1/2 re-armed after BLE enable combo released");
    }

    // Check for Button 1 AND Button 2 pressed simultaneously for 10 seconds
    // Only Button 1 and Button 2 should be HIGH, all others should be LOW
    if ((switch1State == HIGH) && (switch2State == HIGH) && (switch3State == LOW) && (switch4State == LOW)
        && (switchOffState == LOW) && (bleEnableStartTime == 0)) {
        bleEnableStartTime = millis();
    } else if ((switch1State == HIGH) && (switch2State == HIGH) && (switch3State == LOW) && (switch4State == LOW)
               && (switchOffState == LOW) && (bleEnableStartTime != 0)
               && (millis() - bleEnableStartTime > BLE_ENABLE_PRESS_DURATION)) {
        buttonBLEEnablePressed = true;
        Serial.println("buttonBLEEnablePressed = true");
        bleEnableStartTime = 0;
        Serial.println("BLE Enable triggered by Button 1 + Button 2 (10 sec hold)");
    } else if ((switch1State == LOW) || (switch2State == LOW) || (switch3State == HIGH) || (switch4State == HIGH)
               || (switchOffState == HIGH)) {
        bleEnableStartTime = 0;
    }
}

void checkButtonOffLongPress() {
    uint8_t switch1State = digitalRead(SWITCH_1);
    uint8_t switch2State = digitalRead(SWITCH_2);
    uint8_t switch3State = digitalRead(SWITCH_3);
    uint8_t switch4State = digitalRead(SWITCH_4);
    uint8_t switchOffState = digitalRead(SWITCH_OFF);

    if (leakageDetected) {
        uint8_t testSpikeRead = digitalRead(LEAK_DETECTION);
        Serial.println(" LEAK_DETECTION  status");
        Serial.println(testSpikeRead);
    }

    // Don't process if long press already detected (will be reset in core0Task)
    if (buttonOffLongPressed) {
        return;
    }

    // Ensure no other buttons are pressed - only stop button should be pressed for long press
    bool otherButtonPressed =
        (switch1State == HIGH) || (switch2State == HIGH) || (switch3State == HIGH) || (switch4State == HIGH);

    // Track when button is first pressed (HIGH) and no other buttons are pressed
    if (switchOffState == HIGH && !otherButtonPressed && buttonOffPressStartTime == 0) {
        buttonOffPressStartTime = millis();
    }
    // Check for long press while button is still held and no other buttons are pressed
    else if (switchOffState == HIGH && !otherButtonPressed && buttonOffPressStartTime != 0
             && (millis() - buttonOffPressStartTime) >= LONG_PRESS_TIME) {
        buttonOffLongPressed = true;
        disableWiFiLedIndicate = true;
        buttonOffPressed = false;
        buttonOffPressStartTime = 0;
        ignoreButtonOffFallOnce = true;
        Serial.println("Stop button long press detected!");
    }
    // Reset tracking if stop button is released, other button is pressed, or short press case
    else if (switchOffState == LOW || otherButtonPressed) {
        if (buttonOffPressStartTime != 0 && (millis() - buttonOffPressStartTime) < LONG_PRESS_TIME) {
            // Short press or other button pressed - reset tracking
            if (!buttonOffPressed) {
                buttonOffPressStartTime = 0;
            }
        } else if (otherButtonPressed && buttonOffPressStartTime != 0) {
            // Other button pressed during long press tracking - reset
            buttonOffPressStartTime = 0;
        }
    }
}

bool readSensorWithTimeout(void (*sensorFunction)(DateTime), unsigned long timeoutMs, DateTime curDateTime) {
    unsigned long startMillis = millis();
    sensorFunction(curDateTime);
    if (millis() - startMillis > timeoutMs) {
        return true;
    }
    return false;
}

void resetPowerTimer() {
    overRunStartTime = 0;
    overRunAlertState = false;
    dryRunStartTime = 0;
    dryRunAlertState = false;
    floatLowStartTime = 0;
}

void saveCurLimit(JsonDocument doc) {
    PowerSettings psTemp = {};
    psTemp.drCur = doc["drCur"].as<float>();
    psTemp.orCur = doc["orCur"].as<float>();
    psTemp.lVol = doc["lVol"].as<float>();
    psTemp.hVol = doc["hVol"].as<float>();
    psTemp.thCur = doc["thCur"].as<float>();
    psTemp.drMin = doc["drMin"].as<uint8_t>();
    psTemp.orMin = doc["orMin"].as<uint8_t>();
    psTemp.lVolMin = doc["lVolMin"].as<uint8_t>();
    psTemp.hVolMin = doc["hVolMin"].as<uint8_t>();
    psTemp.stDly = doc["stDly"].as<uint8_t>();
    psTemp.maxRunTimeMins = doc["mR"].as<uint16_t>();
    psTemp.breakTimeMins = doc["bT"].as<uint16_t>();

    uint8_t flag, isRst = 0, isRstDr = 0, isRstOl = 0, isRstLv = 0, isRstHv = 0; //, isRstPf = 0, isRstPsr = 0;
    uint8_t restartOptionsAll = 0;

    if (doc["isRst"] == "1") {
        isRst = 1;
    }
    if (doc["isRstDr"] == "1") {
        isRstDr = 1;
    }
    if (doc["isRstOl"] == "1") {
        isRstOl = 1;
    }
    if (doc["isRstLv"] == "1") {
        isRstLv = 1;
    }
    if (doc["isRstHv"] == "1") {
        isRstHv = 1;
    }
    // if (doc["isRstPf"] == "1") {
    //     isRstPf = 1;
    // }
    // if (doc["isRstPsr"] == "1") {
    //     isRstPsr = 1;
    // }

    psTemp.resOpt = isRst | (isRstDr << 1) | (isRstOl << 2) | (isRstLv << 3)
                    | (isRstHv << 4); // | (isRstPf << 5) | (isRstPsr << 6);

    flag = 0;
    if (doc["is3Ph"] == "1") {
        flag = 1;
    }
    psTemp.is3Ph = flag;

    flag = 0;
    if (doc["isSlave"] == "1") {
        flag = 1;
    }
    psTemp.isSlave = flag;

    updatePowerSettings(psTemp);
    readCurSettings(true);
}

void readCurSettings(bool afterSave) {
    // read3PhaseSettings(isThreePhaseDevice, tappingRequired, tappingDuration, ethernetEnabled, ledType, shuntOrCT,
    // masterSlaveMode, masterDeviceUrl);
    if (isPLimitSet || afterSave) {
        readPowerSettings();
    } else {
        dryRunCurrentLimit = DEFAULT_DRY_RUN_CURRENT_LIMIT;
        overRunCurrentLimit = DEFAULT_OVER_RUN_CURRENT_LIMIT;
        lowVoltageLimit = DEFAULT_LOW_VOLTAGE_LIMIT;
        highVoltageLimit = DEFAULT_HIGH_VOLTAGE_LIMIT;
        dryRunCurrentThreshold = 0;
        dryRunRetryMinutes = DEFAULT_RETRY_MINS;
        overRunRetryMinutes = DEFAULT_RETRY_MINS;
        lowVoltageRetryMinutes = DEFAULT_RETRY_MINS;
        highVoltageRetryMinutes = DEFAULT_RETRY_MINS;
        isRestartEnabled = 0;
        restartOptions = 0;
        startDelayMins = 0;
        maxRunTimeMins = 0;
        breakTimeMins = 0;
        isSlaveControlDevice = 0;
    }
}

void resetFaultConditionsRoutine() {
    DateTime oldDate = DateTime(2001, 0, 0, 0, 0, 0);
    clearFaultDetails();
    lastManualOffTime = oldDate;
    lastLowVoltageTime = oldDate;
    lastDryRunTime = oldDate;
    lastOverRunTime = oldDate;
    lastHighVoltageTime = oldDate;
    manualOff = 0;
    lastPhaseFailureTime = oldDate;
    lastPhaseSequenceReverseTime = oldDate;
    if (isFloatEnabled == 2 && Flow_stopped_manual) {
        flowIncomingState = false;
        Flow_stopped_manual = false;
    }
    // leakageAlertState = false;
}

void setupPowerSensor() {
    readCurSettings(false);
    Serial2.begin(4800, SERIAL_8N1, RXD_ENERGY, TXD_ENERGY);
    delay(100);
    BL0939_setup();
}

JsonDocument getCurSettings() {
    JsonDocument doc;
    doc["drCur"] = round(dryRunCurrentLimit * 1000.0) / 1000.0;
    doc["thCur"] = round(dryRunCurrentThreshold * 1000.0) / 1000.0;
    doc["orCur"] = round(overRunCurrentLimit * 1000.0) / 1000.0;
    doc["lVol"] = round(lowVoltageLimit * 100.0) / 100.0;
    doc["hVol"] = round(highVoltageLimit * 100.0) / 100.0;
    doc["drMin"] = dryRunRetryMinutes;
    doc["orMin"] = overRunRetryMinutes;
    doc["lVolMin"] = lowVoltageRetryMinutes;
    doc["hVolMin"] = highVoltageRetryMinutes;
    doc["stDly"] = startDelayMins;
    doc["mR"] = maxRunTimeMins;
    doc["bT"] = breakTimeMins;
    doc["isRst"] = isRestartEnabled;
    doc["isRstDr"] = restartOptions >> 1 & 0x01;
    doc["isRstOl"] = restartOptions >> 2 & 0x01;
    doc["isRstLv"] = restartOptions >> 3 & 0x01;
    doc["isRstHv"] = restartOptions >> 4 & 0x01;
    // doc["isRstPf"] = restartOptions >> 5 & 0x01;
    // doc["isRstPsr"] = restartOptions >> 6 & 0x01;
    doc["is3Ph"] = isThreePhaseDevice ? (vsmrLinkEnabled() ? 2 : 1) : 0;
    doc["isSlave"] = isSlaveControlDevice;
    // #ifdefined(LED_RGB_FLOATRELAY)
    if (ledType == 2 || ledType == 3) {
        doc["floatRelayadded"] = true;
        doc["floatMode"] = isFloatEnabled;
        // Include flow detection delays (stored in seconds)
        doc["flowDetectDelay"] = flowDetectionDelaySec;
        doc["flowStopDelay"] = flowStopDelaySec;
    }
    // #endif
    return doc;
}