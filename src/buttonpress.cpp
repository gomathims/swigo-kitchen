#include "buttonpress.h"
#ifdef SPECIAL_DEBOUNCE_DELAY
#include "mqtt.h"
#include "power_monitoring.h"
#endif

// Global variables
volatile bool button1Pressed = false;
volatile bool button2Pressed = false;
volatile bool button3Pressed = false;
volatile bool button4Pressed = false;
volatile bool buttonOffPressed = false;
volatile bool buttonOffLongPressed = false;

unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
unsigned long lastDebounceTime3 = 0;
unsigned long lastDebounceTime4 = 0;
unsigned long lastDebounceTimeOff = 0;
unsigned long buttonOffPressStartTime = 0;  // Track when stop button was first pressed (0 = not pressed)

#ifdef SPECIAL_DEBOUNCE_DELAY
volatile unsigned long buttonRiseTime1 = 0;
volatile unsigned long buttonRiseTime2 = 0;
volatile unsigned long buttonRiseTime3 = 0;
volatile unsigned long buttonRiseTime4 = 0;
volatile uint8_t pendingSpikeButton = 0;
volatile unsigned long pendingSpikeDuration = 0;
#endif

volatile bool phaseFailureTrigger = false;
volatile bool phaseSequenceReverseTrigger = false;

// Leakage detection
bool leakDetectionEnabled = false;
volatile bool leakageDetected = false;
volatile bool leakageAlertState = false;
unsigned long lastDebounceTimePhaseFailure = 0;
unsigned long lastDebounceTimePhaseSequenceReverse = 0;

// Master switch tracking for slave mode 
bool masterSwitchOn = false;
bool masterSwitchOnStatusMqtt = false;
unsigned long masterSwitchOnTimeMqtt = false;

unsigned long lastMasterCheckTime = 0;
uint8_t masterTimeoutCount = 0;  // Count consecutive timeouts (max 3)
bool floatingAlertOffSent = false;  // Track if OFF message sent for floating alert
unsigned long lastFloatingAlertOffSentTime = 0;  // Timestamp when floating alert OFF message was last sent
bool lastFlowIncomingStateSent = false;  // Track last flowIncomingState value that was sent to master
bool logRefreshRequired = false;  // Flag to indicate log refresh is required when remote command sent or failure logged


// void IRAM_ATTR handleButton1Press() {
//     unsigned long currentTime = millis();
//     if ((currentTime - lastDebounceTime1 > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
//         && (currentTime - relayOffTime > EMI_DELAY)) {
//         if (! ignoreButton1FallOnce) {
// #ifdef SPECIAL_DEBOUNCE_DELAY
//             if (digitalRead(SWITCH_1) == HIGH) {
//                 // RISING edge - button pressed, record start time
//                 buttonRiseTime1 = currentTime;
//                 buttonFallTime1 = 0;
//             } else {
//                 // FALLING edge - button released, record end time
//                 buttonFallTime1 = currentTime;
//             }
// #else
//             button1Pressed = true;
// #endif
//             lastDebounceTime1 = currentTime;
//         }
//     }
// }

// void IRAM_ATTR handleButton2Press() {
//     unsigned long currentTime = millis();
//     if ((currentTime - lastDebounceTime2 > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
//         && (currentTime - relayOffTime > EMI_DELAY)) {
//         if (! ignoreButton2FallOnce){
// #ifdef SPECIAL_DEBOUNCE_DELAY
//             if (digitalRead(SWITCH_2) == HIGH) {
//                 buttonRiseTime2 = currentTime;
//                 buttonFallTime2 = 0;
//             } else {
//                 buttonFallTime2 = currentTime;
//             }
// #else
//             button2Pressed = true;
// #endif
//             lastDebounceTime2 = currentTime;
//         }
//     }
// }

// void handleButton3Press() {
//     unsigned long currentTime = millis();
//     if ((currentTime - lastDebounceTime3 > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
//         && (currentTime - relayOffTime > EMI_DELAY)) {
//         if (! ignoreButton3FallOnce){
// #ifdef SPECIAL_DEBOUNCE_DELAY
//             if (digitalRead(SWITCH_3) == HIGH) {
//                 buttonRiseTime3 = currentTime;
//                 buttonFallTime3 = 0;
//             } else {
//                 buttonFallTime3 = currentTime;
//             }
// #else
//             button3Pressed = true;
// #endif
//             lastDebounceTime3 = currentTime;
//         }
//     }
// }

// void handleButton4Press() {
//     unsigned long currentTime = millis();
//     if ((currentTime - lastDebounceTime4 > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
//         && (currentTime - relayOffTime > EMI_DELAY)) {
//         if (! ignoreButton4FallOnce){
// #ifdef SPECIAL_DEBOUNCE_DELAY
//             if (digitalRead(SWITCH_4) == HIGH) {
//                 buttonRiseTime4 = currentTime;
//                 buttonFallTime4 = 0;
//             } else {
//                 buttonFallTime4 = currentTime;
//             }
// #else
//             button4Pressed = true;
// #endif
//             lastDebounceTime4 = currentTime;
//         }
//     }
// }




void IRAM_ATTR handleButton1Press() {
    unsigned long currentTime = millis();
    if (!ignoreButton1FallOnce) {
#ifdef SPECIAL_DEBOUNCE_DELAY
        if ((digitalRead(SWITCH_1) == HIGH) && (buttonRiseTime1 == 0) && ((currentTime - relayOffTime) > EMI_DELAY)) {
            // RISING edge - button pressed, record start time
            buttonRiseTime1 = currentTime;
        } else if ((digitalRead(SWITCH_1) == LOW) && (buttonRiseTime1 != 0)) {
            // FALLING edge - confirm here and re-arm immediately, so a fast
            // subsequent press is never blocked waiting on loop()
            unsigned long duration = currentTime - buttonRiseTime1;
            if ((duration >= BUTTON_CONFIRM_DELAY) && (duration <= PRESS_TIME_ALLOWED)) {
                button1Pressed = true;
                buttonRiseTime1 = 0;
            } else {
                // Too short - this is contact bounce, so keep the original press
                // start time; clearing it would restart the measurement and break
                // the first-HIGH-to-release timing this check relies on.
                // Serial/String/MQTT are not ISR-safe - hand off to loop() for the report
                pendingSpikeButton = 1;
                pendingSpikeDuration = duration;
            }
        }
#else
        if ((currentTime - lastDebounceTime1 > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
         && (currentTime - relayOffTime > EMI_DELAY)) {
            button1Pressed = true;
            lastDebounceTime1 = currentTime;
         }
#endif
        
    }
}

void IRAM_ATTR handleButton2Press() {
    unsigned long currentTime = millis();
    if (!ignoreButton2FallOnce) {
#ifdef SPECIAL_DEBOUNCE_DELAY
        if ((digitalRead(SWITCH_2) == HIGH) && (buttonRiseTime2 == 0) && (currentTime - relayOffTime > EMI_DELAY)) {
            buttonRiseTime2 = currentTime;
        } else if ((digitalRead(SWITCH_2) == LOW) && (buttonRiseTime2 != 0)) {
            unsigned long duration = currentTime - buttonRiseTime2;
            if ((duration >= BUTTON_CONFIRM_DELAY) && (duration <= PRESS_TIME_ALLOWED)) {
                button2Pressed = true;
                buttonRiseTime2 = 0;
            } else {
                pendingSpikeButton = 2;
                pendingSpikeDuration = duration;
            }
        }
#else
        if ((currentTime - lastDebounceTime2 > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
         && (currentTime - relayOffTime > EMI_DELAY)) {
            button2Pressed = true;
            lastDebounceTime2 = currentTime;

        }
#endif

    }
}

void IRAM_ATTR handleButton3Press() {
    unsigned long currentTime = millis();

    if (!ignoreButton3FallOnce) {
#ifdef SPECIAL_DEBOUNCE_DELAY
        if ((digitalRead(SWITCH_3) == HIGH) && (buttonRiseTime3 == 0) && (currentTime - relayOffTime > EMI_DELAY)) {
            buttonRiseTime3 = currentTime;
        } else if ((digitalRead(SWITCH_3) == LOW) && (buttonRiseTime3 != 0)) {
            unsigned long duration = currentTime - buttonRiseTime3;
            if ((duration >= BUTTON_CONFIRM_DELAY) && (duration <= PRESS_TIME_ALLOWED)) {
                button3Pressed = true;
                buttonRiseTime3 = 0;
            } else {
                pendingSpikeButton = 3;
                pendingSpikeDuration = duration;
            }
        }
#else
        if ((currentTime - lastDebounceTime3 > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
         && (currentTime - relayOffTime > EMI_DELAY)) {
            button3Pressed = true;
            lastDebounceTime3 = currentTime;

        }
#endif
    }
}

void IRAM_ATTR handleButton4Press() {
    unsigned long currentTime = millis();

    if (!ignoreButton4FallOnce) {
#ifdef SPECIAL_DEBOUNCE_DELAY
        if ((digitalRead(SWITCH_4) == HIGH) && (buttonRiseTime4 == 0) && (currentTime - relayOffTime > EMI_DELAY)) {
            buttonRiseTime4 = currentTime;
        } else if ((digitalRead(SWITCH_4) == LOW) && (buttonRiseTime4 != 0)) {
            unsigned long duration = currentTime - buttonRiseTime4;
            if ((duration >= BUTTON_CONFIRM_DELAY) && (duration <= PRESS_TIME_ALLOWED)) {
                button4Pressed = true;
                buttonRiseTime4 = 0;
            } else {
                pendingSpikeButton = 4;
                pendingSpikeDuration = duration;
            }
        }
#else
        if ((currentTime - lastDebounceTime4 > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
         && (currentTime - relayOffTime > EMI_DELAY)) {
            button4Pressed = true;
            lastDebounceTime4 = currentTime;

        }
#endif
    }
}

void handleButtonOffPress() {
    unsigned long currentTime = millis();
    
    // Only handle FALLING edge (button release)
    if ((currentTime - lastDebounceTimeOff > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
        && (currentTime - relayOffTime > EMI_DELAY) && (buttonOffPressStartTime != 0) 
        && (millis() - buttonOffPressStartTime) < 5000) {
        if (!ignoreButtonOffFallOnce) {
            buttonOffPressed = true;
            lastDebounceTimeOff = currentTime;
            buttonOffPressStartTime = 0;
        }
    }
}

// The following interrupts handlePhaseFailureTrigger, handlePhaseReversalTrigger are commented out, as we handle in loop for now
// void handlePhaseFailureTrigger() {
//     unsigned long currentTime = millis();
//     if ((currentTime - lastDebounceTimePhaseFailure > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
//         && (currentTime - relayOffTime > EMI_DELAY)) {

//         uint8_t switchState = digitalRead(PHASE_FAILURE);
//         if (switchState == HIGH) {
//             digitalWrite(RELAY_OUT, LOW);
//             phaseFailureTrigger = true;
//         } else {
//             phaseFailureAlertState = false;
//         }
//         lastDebounceTimePhaseFailure = currentTime;
//     }
// }

// void handlePhaseReversalTrigger() {
//     unsigned long currentTime = millis();
//     if ((currentTime - lastDebounceTimePhaseSequenceReverse > DEBOUNCE_DELAY) && (currentTime - relayOnTime > EMI_DELAY)
//         && (currentTime - relayOffTime > EMI_DELAY)) {

//         uint8_t switchState = digitalRead(PHASE_SEQUENCE_REVERSE);
//         if (switchState == HIGH) {
//             digitalWrite(RELAY_OUT, LOW);
//             phaseSequenceReverseTrigger = true;
//         } else {
//             phaseSequenceReverseAlertState = false;
//         }
//         lastDebounceTimePhaseSequenceReverse = currentTime;
//     }
// }
#ifdef SPECIAL_DEBOUNCE_DELAY

static void reportSpike(uint8_t buttonNo, unsigned long durationMs) {
    Serial.printf("Spike rejected: button=%d duration=%lums voltage=%.1fV\n", buttonNo, durationMs, voltage);
    String payload = "{\"spike\":{\"button\":" + String(buttonNo) +
                     ",\"duration_ms\":" + String(durationMs) +
                     ",\"voltage\":" + String(voltage, 1) + "}}";
    if (isCloudConnected) {
        queueMQTTMessage(propTopic, payload);
    } else if (isGeneralMQTTEnabled && mqttPublishTopic.length() > 0) {
        queueMQTTMessage(mqttPublishTopic.c_str(), payload);
    }
}

void confirmButtonPresses() {
    // Confirmation itself happens inside the ISR on the FALLING edge (see
    // handleButtonXPress). This only has two jobs left:
    // 1) Re-arm a press start that never reached a valid release - a rejected
    //    spike, or a FALLING interrupt skipped via ignoreButtonXFallOnce. The
    //    BOUNCE_SETTLE_TIME guard keeps this from clearing a press start during
    //    the contact-bounce window, when the button is in fact still held.
    // 2) Drain the shared pending-spike-report slot set by an ISR (Serial/String/
    //    MQTT calls aren't ISR-safe, so the actual report happens here).
    unsigned long now = millis();
    if (buttonRiseTime1 != 0 && digitalRead(SWITCH_1) == LOW && (now - buttonRiseTime1) > BOUNCE_SETTLE_TIME) {
        buttonRiseTime1 = 0;
    }
    if (buttonRiseTime2 != 0 && digitalRead(SWITCH_2) == LOW && (now - buttonRiseTime2) > BOUNCE_SETTLE_TIME) {
        buttonRiseTime2 = 0;
    }
    if (buttonRiseTime3 != 0 && digitalRead(SWITCH_3) == LOW && (now - buttonRiseTime3) > BOUNCE_SETTLE_TIME) {
        buttonRiseTime3 = 0;
    }
    if (buttonRiseTime4 != 0 && digitalRead(SWITCH_4) == LOW && (now - buttonRiseTime4) > BOUNCE_SETTLE_TIME) {
        buttonRiseTime4 = 0;
    }

    if (pendingSpikeButton != 0) {
        // Uncomment to re-enable spike telemetry (Serial + MQTT):
        // reportSpike(pendingSpikeButton, pendingSpikeDuration);
        pendingSpikeButton = 0;
        pendingSpikeDuration = 0;
    }
}
#endif

void resetButtonPress() {
    button1Pressed = false;
    button2Pressed = false;
    button3Pressed = false;
    button4Pressed = false;
    buttonOffPressed = false;
#ifdef SPECIAL_DEBOUNCE_DELAY
    buttonRiseTime1 = 0;
    buttonRiseTime2 = 0;
    buttonRiseTime3 = 0;
    buttonRiseTime4 = 0;
    pendingSpikeButton = 0;
    pendingSpikeDuration = 0;
#endif
}

void handleLeakagePinSpike() {
    if (!leakDetectionEnabled) 
        return;
    if (digitalRead(LEAK_DETECTION)) {
        leakageDetected = true;
        digitalWrite(RELAY_OUT, LOW);
    }
}
