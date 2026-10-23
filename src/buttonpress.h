#ifndef BUTTONPRESS_H
#define BUTTONPRESS_H

#include "Arduino.h"
#include "pinmapping.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

// Constants
#define SHORT_PRESS_TIME 500
#define LONG_PRESS_TIME 5000  // 5 seconds for long press

#ifdef SPECIAL_DEBOUNCE_DELAY

#define DEBOUNCE_DELAY 50        // Extended debounce for special conditions
#define EMI_DELAY 100           // Extended EMI delay for special conditions
#define BUTTON_CONFIRM_DELAY 20 // Button must be held for this long (ms) to confirm press
#define PRESS_TIME_ALLOWED 4500
#define BOUNCE_SETTLE_TIME 5    // Contact bounce window (ms) after press start - a LOW seen
                                // within this window is bounce, not a real release

#else

#define DEBOUNCE_DELAY 50 // To avoid Repeat of the same button press event
#define EMI_DELAY 200     // Electro magnetic interference delay

#endif

// External variables
extern uint8_t relayState;

extern volatile bool button1Pressed;
extern volatile bool button2Pressed;
extern volatile bool button3Pressed;
extern volatile bool button4Pressed;
extern volatile bool buttonOffPressed;
extern volatile bool buttonOffLongPressed;
extern unsigned long buttonOffPressStartTime;  // Track when stop button was first pressed (0 = not pressed)
extern volatile bool phaseFailureTrigger;
extern volatile bool phaseSequenceReverseTrigger;

extern volatile bool phaseFailureAlertState;
extern volatile bool phaseSequenceReverseAlertState;

extern volatile unsigned long relayOnTime;
extern volatile unsigned long relayOffTime;

extern volatile bool ignoreButton1FallOnce;
extern volatile bool ignoreButton2FallOnce;
extern volatile bool ignoreButton3FallOnce;
extern volatile bool ignoreButton4FallOnce;
extern volatile bool ignoreButtonOffFallOnce;

#ifdef SPECIAL_DEBOUNCE_DELAY
// RISING timestamp (press start) recorded by ISR; confirm/reject is resolved
// in the same ISR call on the matching FALLING edge, so no fall timestamp is kept.
extern volatile unsigned long buttonRiseTime1;
extern volatile unsigned long buttonRiseTime2;
extern volatile unsigned long buttonRiseTime3;
extern volatile unsigned long buttonRiseTime4;

// Shared pending-spike-report slot, filled by whichever button ISR rejects a
// release (too short/too long). Single shared pair, not one per button - a
// diagnostic report only, never used for press-confirmation logic.
extern volatile uint8_t pendingSpikeButton;   // 0 = none, else button number 1-4
extern volatile unsigned long pendingSpikeDuration;
#endif

// External variables for master/slave communication
extern String masterDeviceUrl;
extern String mPin;
extern char devicePin[9];
extern volatile bool ethernetConnected;

// Master switch tracking for slave mode
extern bool masterSwitchOn;
extern bool masterSwitchOnStatusMqtt;
extern unsigned long masterSwitchOnTimeMqtt;

extern unsigned long lastMasterCheckTime;
extern uint8_t masterTimeoutCount;  // Count consecutive timeouts
extern bool floatingAlertOffSent;  // Track if OFF message sent for floating alert
extern unsigned long lastFloatingAlertOffSentTime;  // Timestamp when floating alert OFF message was last sent
extern bool lastFlowIncomingStateSent;  // Track last flowIncomingState value that was sent to master
extern bool logRefreshRequired;  // Flag to indicate log refresh is required when remote command sent or failure logged
extern volatile bool disableWiFiLedIndicate;

// Leakage detection
extern bool leakDetectionEnabled;
extern volatile bool leakageDetected;
extern volatile bool leakageAlertState;
void handleLeakagePinSpike();

// Function declarations
void handleButton1Press();
void handleButton2Press();
void handleButton3Press();
void handleButton4Press();
void handleButtonOffPress();
// void handlePhaseFailureTrigger();
// void handlePhaseReversalTrigger();

void resetButtonPress();
#ifdef SPECIAL_DEBOUNCE_DELAY
void confirmButtonPresses();
#endif


#endif  // BUTTONPRESS_H