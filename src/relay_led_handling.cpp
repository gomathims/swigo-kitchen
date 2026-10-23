
#include "relay_led_handling.h"
#include "ble_provisioning.h"
#include "energydata.h"

void blinkLeakageErrorLed();

// Global variables
int lastBlinkTime = 0;    // Last time LED was blinked
bool lastBlinkstatus = 0; // Last LED blink status
int indicatorStartTime = 0;
unsigned long colorLedBlinkstartTime = 0;

bool volatile floatStateLedIndicate = false, highVoltageLedIndicate = false, lowVoltageLedIndicate = false,
              dryrunLedIndicate = false, overrunLedIndicate = false, leakageLedIndicate = false;
bool volatile disableWiFiLedIndicate = false, factoryResetLedIndicate = false;
bool volatile vsmrPairLedIndicate = false;

volatile bool ignoreButton1FallOnce;
volatile bool ignoreButton2FallOnce;
volatile bool ignoreButton3FallOnce;
volatile bool ignoreButton4FallOnce;
volatile bool ignoreButtonOffFallOnce;

// Define constants for various colors

//   const RGBColor RED = {0, 255, 255};
//   const RGBColor GREEN = {255, 0, 255};
//   const RGBColor BLUE = {255, 255, 0};
//   const RGBColor BLACK = {255, 255, 255};
//   const RGBColor WHITE = {0, 0, 0};
//   const RGBColor CYAN = {255, 0, 0};
//   const RGBColor MAGENTA = {0, 255, 0};
//   const RGBColor YELLOW = {35, 0, 5};
//   const RGBColor AMBER = {7, 0, 65};

//   void lightColorLED(const RGBColor& color) {
//       analogWrite(LED_RED, color.r); // Red
//       analogWrite(LED_GREEN, color.g); // Green
//       analogWrite(LED_BLUE, color.b); // Blue
//   }

//   void blinkColorLedWithDuration(const RGBColor color, BlinkType blinkType, unsigned long onDuration, unsigned long
//   offDuration, unsigned long totalDuration, volatile bool* param = nullptr) {
//     static unsigned long sequenceStartTime = 0;
//     static bool isOn = false;
//     static BlinkType lastBlinkType = BlinkType::NONE;
//     int8_t ledNo;
//     switch (blinkType) {

//         case BlinkType::DRYRUN_ERROR:
//             ledNo = LED_1;
//             break;
//         case BlinkType::OVERLOAD_ERROR:
//             ledNo = LED_2;
//             break;
//         case BlinkType::LOWVOLTAGE_ERROR:
//             ledNo = LED_3;
//             break;
//         case BlinkType::HIGHVOLTAGE_ERROR:
//             ledNo = LED_4;
//             break;
//         default:
//             ledNo = -1;
//             break;
//     }
//     if (lastBlinkType != blinkType) {
//         lastBlinkType = blinkType;
//         digitalWrite(LED_1, LOW);
//         digitalWrite(LED_2, LOW);
//         digitalWrite(LED_3, LOW);
//         digitalWrite(LED_4, LOW);
//     }
//     if (colorLedBlinkstartTime == 0) {
//         sequenceStartTime = millis();
//         colorLedBlinkstartTime = sequenceStartTime;
//         isOn = true;
//         lightColorLED(color);
//         if (ledNo!=-1){
//           digitalWrite(ledNo, HIGH);
//         }

//     }

//     unsigned long currentTime = millis();
//     unsigned long sequenceElapsed = currentTime - sequenceStartTime;

//     // Check if we've exceeded total duration
//     if (sequenceElapsed >= totalDuration) {
//         lightColorLED(BLACK);
//         if (ledNo!=-1){
//           digitalWrite(ledNo, LOW);
//         }
//         colorLedBlinkstartTime = 0;
//         if (param) { // Check if param is not null before dereferencing
//           Serial.println("Before setting it false");
//           *param = false;

//         }
//         if (blinkType == BlinkType::RESET) {
//           ignoreButton2FallOnce=false;
//           ignoreButton3FallOnce=false;
//           ignoreButton4FallOnce=false;
//           ignoreButtonOffFallOnce=false;
//         }

//         return;
//     }

//     unsigned long elapsedTime = currentTime - colorLedBlinkstartTime;

//     if (isOn && elapsedTime >= onDuration) {
//         // Serial.println("inside if (isOn && elapsedTime >= onDuration)");
//         lightColorLED(BLACK);
//         if (ledNo!=-1){
//           digitalWrite(ledNo, LOW);
//         }
//         isOn = false;
//         colorLedBlinkstartTime = currentTime;
//     }
//     else if (!isOn && elapsedTime >= offDuration) {
//         // Serial.println("inside if ( NOT ON)");
//         lightColorLED(color);
//         if (ledNo!=-1){
//           digitalWrite(ledNo, HIGH);
//         }
//         isOn = true;
//         colorLedBlinkstartTime = currentTime;
//     }
//     else {
//       // Serial.println(isOn);
//       // Serial.println(startTime);

//     }
// }

// #ifdef LED_NORMAL
void blinkColorLed(uint8_t ledNo);
// #else
struct RGBColor {
    int r, g, b;
};

void blink200ColorLed(const RGBColor color);

// Define constants for various colors

const RGBColor RED = {0, 255, 255};
const RGBColor GREEN = {255, 0, 255};
const RGBColor BLUE = {255, 255, 0};
const RGBColor BLACK = {255, 255, 255};
const RGBColor WHITE = {0, 0, 0};
const RGBColor CYAN = {255, 0, 0};
const RGBColor MAGENTA = {0, 255, 0};
const RGBColor YELLOW = {35, 0, 5};
const RGBColor AMBER = {7, 0, 65};

void lightColorLED(const RGBColor &color) {
    analogWrite(LED_RED, color.r);   // Red
    analogWrite(LED_GREEN, color.g); // Green
    analogWrite(LED_BLUE, color.b);  // Blue
}
// Interrupt-safe version using digitalWrite instead of analogWrite.
// Avoids LEDC reconfiguration which disables interrupts and can block leak detection ISR.
void lightColorLEDFast(const RGBColor &color) {
    digitalWrite(LED_RED, color.r > 127 ? HIGH : LOW);
    digitalWrite(LED_GREEN, color.g > 127 ? HIGH : LOW);
    digitalWrite(LED_BLUE, color.b > 127 ? HIGH : LOW);
}
// void blink200ColorLed(const RGBColor color) {

//   static unsigned long colorIndicatorStartTime=0;
//   if (colorIndicatorStartTime == 0) {
//     colorIndicatorStartTime = millis();
//     lightColorLED(color);
//   }
//   unsigned long durationMillis = millis()- colorIndicatorStartTime;
//   if ((durationMillis >= 0 && durationMillis < 200) || (durationMillis >= 1000 && durationMillis < 1200) ||
//   (durationMillis >= 2000 && durationMillis < 2200)) {
//     lightColorLED(color);
//   } else if ((durationMillis >= 200 && durationMillis < 400) || (durationMillis >= 1200 && durationMillis < 1400) ||
//   (durationMillis >= 2200 && durationMillis < 2400)) {
//     lightColorLED(BLACK);
//   } else if ((durationMillis >= 400 && durationMillis < 600) || (durationMillis >= 1400 && durationMillis < 1600) ||
//   (durationMillis >= 2400 && durationMillis < 2600)) {
//     lightColorLED(color);
//   } else if ((durationMillis >= 600 && durationMillis < 1000) || (durationMillis >= 1600 && durationMillis < 2000)) {
//     lightColorLED(BLACK);
//   } else {
//     lightColorLED(BLACK);
//     colorIndicatorStartTime = 0;
//     disableWiFiLedIndicate=false;
//     factoryResetLedIndicate=false;
//   }
// }

void blinkColorLedWithDuration(const RGBColor color, BlinkType blinkType, unsigned long onDuration,
                               unsigned long offDuration, unsigned long totalDuration, volatile bool *param) {
    static unsigned long sequenceStartTime = 0;
    static bool isOn = false;
    static BlinkType lastBlinkType = BlinkType::NONE;
    int8_t ledNo;
    switch (blinkType) {
    case BlinkType::DRYRUN_ERROR:
        ledNo = LED_1;
        break;
    case BlinkType::OVERLOAD_ERROR:
        ledNo = LED_2;
        break;
    case BlinkType::LOWVOLTAGE_ERROR:
        ledNo = LED_3;
        break;
    case BlinkType::HIGHVOLTAGE_ERROR:
        ledNo = LED_4;
        break;
    case BlinkType::LEAKAGE_ERROR:
        ledNo = -2; // Special: multiple LEDs handled below
        break;
    default:
        ledNo = -1;
        break;
    }
    if (lastBlinkType != blinkType) {
        lastBlinkType = blinkType;
        digitalWrite(LED_1, LOW);
        digitalWrite(LED_2, LOW);
        digitalWrite(LED_3, LOW);
        digitalWrite(LED_4, LOW);
        if (blinkType == BlinkType::LEAKAGE_ERROR) {
            // lightColorLED(BLACK);
            lightColorLEDFast(BLACK);
        }
    }
    if (colorLedBlinkstartTime == 0) {
        sequenceStartTime = millis();
        colorLedBlinkstartTime = sequenceStartTime;
        isOn = true;
        // lightColorLED(color);
        lightColorLEDFast(color);
        if (ledNo == -2) {
            digitalWrite(LED_1, HIGH);
            digitalWrite(LED_2, HIGH);
        } else if (ledNo != -1) {
            digitalWrite(ledNo, HIGH);
        }
    }

    unsigned long currentTime = millis();
    unsigned long sequenceElapsed = currentTime - sequenceStartTime;

    // Check if we've exceeded total duration
    if (sequenceElapsed >= totalDuration) {
        // lightColorLED(BLACK);
        lightColorLEDFast(BLACK);
        if (ledNo == -2) {
            digitalWrite(LED_1, LOW);
            digitalWrite(LED_2, LOW);
        } else if (ledNo != -1) {
            digitalWrite(ledNo, LOW);
        }
        colorLedBlinkstartTime = 0;
        if (param) { // Check if param is not null before dereferencing
            // Serial.println("Before setting it false");
            *param = false;
        }
        if (blinkType == BlinkType::RESET) {
            ignoreButton1FallOnce = false;
            ignoreButton2FallOnce = false;
            ignoreButton3FallOnce = false;
            ignoreButton4FallOnce = false;
            ignoreButtonOffFallOnce = false;
        }

        return;
    }

    unsigned long elapsedTime = currentTime - colorLedBlinkstartTime;

    if (isOn && elapsedTime >= onDuration) {
        // Serial.println("inside if (isOn && elapsedTime >= onDuration)");
        // lightColorLED(BLACK);
        lightColorLEDFast(BLACK);
        if (ledNo == -2) {
            digitalWrite(LED_1, LOW);
            digitalWrite(LED_2, LOW);
        } else if (ledNo != -1) {
            digitalWrite(ledNo, LOW);
        }
        isOn = false;
        colorLedBlinkstartTime = currentTime;
    } else if (!isOn && elapsedTime >= offDuration) {
        // Serial.println("inside if ( NOT ON)");
        // lightColorLED(color);
        lightColorLEDFast(color);
        if (ledNo == -2) {
            digitalWrite(LED_1, HIGH);
            digitalWrite(LED_2, HIGH);
        } else if (ledNo != -1) {
            digitalWrite(ledNo, HIGH);
        }
        isOn = true;
        colorLedBlinkstartTime = currentTime;
    } else {
        // Serial.println(isOn);
        // Serial.println(startTime);
    }
}

// #endif
void handleLEDBlink(char varRunType) {
    // Check for master switch ON state first (highest priority in slave mode)
    if ((masterSwitchOn || masterSwitchOnStatusMqtt) && masterSlaveMode == 2) {
        handleMasterSwitchStatusLed();
        return;
    }
    // Check for BLE provisioning - rapid blue LED blinking (pairing indication)
    if (bleProvisioningActive) {
        static unsigned long bleLedLastToggle = 0;
        const unsigned long BLE_BLINK_INTERVAL = 150; // 150ms for rapid blinking (pairing indication)

        unsigned long currentTime = millis();
        if (currentTime - bleLedLastToggle >= BLE_BLINK_INTERVAL) {
            bleLedLastToggle = currentTime;
            if (ledType == 0) {
                // Normal LED - toggle LED_SMART
                lastBlinkstatus = !lastBlinkstatus;
                digitalWrite(LED_SMART, lastBlinkstatus ? HIGH : LOW);
            } else {
                // RGB LED - toggle blue color
                lastBlinkstatus = !lastBlinkstatus;
                if (lastBlinkstatus) {
                    // lightColorLED(BLUE);
                    lightColorLEDFast(BLUE);
                } else {
                    // lightColorLED(BLACK);
                    lightColorLEDFast(BLACK);
                }
            }
        }
        return; // BLE provisioning takes priority, don't process other LED states
    }
    // VSMR pairing started (Button-1 5 s hold): glow LED_1 steadily for 2 s.
    // Takes over from the manual-run (runType '1') blink on the same LED;
    // normal patterns resume when the window ends.
    if (vsmrPairLedIndicate) {
        static unsigned long vsmrPairLedStart = 0;
        if (vsmrPairLedStart == 0) {
            vsmrPairLedStart = millis();
            digitalWrite(LED_1, HIGH);
        } else if (millis() - vsmrPairLedStart >= 6000) {
            digitalWrite(LED_1, LOW);
            vsmrPairLedIndicate = false;
            vsmrPairLedStart = 0;
            ignoreButton1FallOnce = false;
        }
        return;
    }
    if (disableWiFiLedIndicate || factoryResetLedIndicate) {
        if (ledType == 0) {
            blinkColorLed(LED_SMART);
        } else {
            if (disableWiFiLedIndicate) {
                // Serial.println("before blinkColorLedWithDuration -- disableWiFiLedIndicate ");
                blinkColorLedWithDuration(BLUE, BlinkType::RESET, 4000, 0, 4000, &disableWiFiLedIndicate);
            } else {
                // Serial.println("before blinkColorLedWithDuration -- factoryResetLedIndicate ");
                blinkColorLedWithDuration(BLUE, BlinkType::RESET, 4000, 0, 4000, &factoryResetLedIndicate);
            }
        }
        return;
    } else if (relayState) {
        if (runType == 'S' || runType == 'C' || runType == 'R' || runType == 'W' || runType == 'Q' || runType == 'H'
            || runType == 'L' || runType == 'f') {
            if (millis() - lastBlinkTime > 500) {
                lastBlinkTime = millis();
                lastBlinkstatus = !lastBlinkstatus;
                if (ledType == 0) {
                    digitalWrite(LED_SMART, lastBlinkstatus ? HIGH : LOW);
                } else {
                    if (lastBlinkstatus) {
                        // lightColorLED(BLUE);
                        lightColorLEDFast(BLUE);
                    } else {
                        // lightColorLED(BLACK);
                        lightColorLEDFast(BLACK);
                    }
                }
            }
        } else if (runType == '1' || runType == '2' || runType == '3' || runType == '4') {
            uint8_t ledNo = (runType == '1') ? LED_1 : ((runType == '2') ? LED_2 : ((runType == '3') ? LED_3 : LED_4));
            if (millis() - lastBlinkTime > 500) {
                lastBlinkTime = millis();
                lastBlinkstatus = !lastBlinkstatus;
                digitalWrite(ledNo, lastBlinkstatus ? HIGH : LOW);
            }
        }
    } else if (floatStateLedIndicate) {
        if (indicatorStartTime == 0) {
            setAllIndicatorsOff();
            indicatorStartTime = millis();
            digitalWrite(LED_4, HIGH);
        }
        unsigned long durationMillis = millis() - indicatorStartTime;
        if (durationMillis < 500) {
            digitalWrite(LED_4, HIGH);
        } else if (durationMillis < 1000 || (durationMillis >= 2500 && durationMillis < 3000)) {
            digitalWrite(LED_4, LOW);
            digitalWrite(LED_3, HIGH);
        } else if (durationMillis < 1500 || (durationMillis >= 3000 && durationMillis < 3500)) {
            digitalWrite(LED_3, LOW);
            digitalWrite(LED_2, HIGH);
        } else if (durationMillis < 2000 || (durationMillis >= 3500 && durationMillis < 4000)) {
            digitalWrite(LED_2, LOW);
            digitalWrite(LED_1, HIGH);
        } else if (durationMillis < 2500) {
            digitalWrite(LED_1, LOW);
            digitalWrite(LED_4, HIGH);
        } else {
            digitalWrite(LED_1, LOW);
            floatStateLedIndicate = false;
            indicatorStartTime = 0;
            digitalWrite(LED_1, LOW);
        }
    } else if (dryrunLedIndicate) {
        if (ledType == 0) {
            blinkErrorLed(LED_1);
        } else {
            blinkColorLedWithDuration(RED, BlinkType::DRYRUN_ERROR, 200, 200, 6000, &dryrunLedIndicate);
        }

    } else if (overrunLedIndicate) {
        if (ledType == 0) {
            blinkErrorLed(LED_2);
        } else {
            blinkColorLedWithDuration(RED, BlinkType::OVERLOAD_ERROR, 200, 200, 6000, &overrunLedIndicate);
        }

    } else if (leakageLedIndicate) {
        if (ledType == 0) {
            blinkLeakageErrorLed();
        } else {
            blinkColorLedWithDuration(RED, BlinkType::LEAKAGE_ERROR, 200, 200, 6000, &leakageLedIndicate);
        }

    } else if (lowVoltageAlertState) {
        if (ledType == 0) {
            if (lowVoltageLedIndicate) {
                blinkErrorLed(LED_3);
            }
        } else {
            blinkColorLedWithDuration(RED, BlinkType::LOWVOLTAGE_ERROR, 200, 200, 1000);
        }
    } else if (highVoltageAlertState) {
        if (ledType == 0) {
            if (highVoltageLedIndicate) {
                blinkErrorLed(LED_4);
            }
        } else {
            blinkColorLedWithDuration(RED, BlinkType::HIGHVOLTAGE_ERROR, 200, 200, 1000);
        }

    }
    // else if (disableWiFiLedIndicate || factoryResetLedIndicate) {
    //     if (ledType == 0) {
    //         blinkColorLed(LED_SMART);
    //     } else {
    //         if (disableWiFiLedIndicate) {
    //             // Serial.println("before blinkColorLedWithDuration -- disableWiFiLedIndicate ");
    //             blinkColorLedWithDuration(BLUE, BlinkType::RESET, 4000, 0, 4000, &disableWiFiLedIndicate);
    //         } else {
    //             // Serial.println("before blinkColorLedWithDuration -- factoryResetLedIndicate ");
    //             blinkColorLedWithDuration(BLUE, BlinkType::RESET, 4000, 0, 4000, &factoryResetLedIndicate);
    //         }
    //     }
    //     return;
    // }
    else {
        // Serial.println("inside else VoltageAlertState");
        // digitalWrite(LED_1, LOW);
        // digitalWrite(LED_2, LOW);
        // digitalWrite(LED_3, LOW);
        // digitalWrite(LED_4, LOW);
        // lastBlinkstatus = false;
        // #ifndef LED_NORMAL
        //   lightColorLED(BLACK);
        // #endif
        setAllIndicatorsOff();
    }
}

void blinkErrorLed(uint8_t ledNo) {
    if (indicatorStartTime == 0) {
        indicatorStartTime = millis();
        digitalWrite(ledNo, HIGH);
    }
    unsigned long durationMillis = millis() - indicatorStartTime;
    if ((durationMillis >= 0 && durationMillis < 200) || (durationMillis >= 1000 && durationMillis < 1200)
        || (durationMillis >= 2000 && durationMillis < 2200)) {
        digitalWrite(ledNo, HIGH);
    } else if ((durationMillis >= 200 && durationMillis < 400) || (durationMillis >= 1200 && durationMillis < 1400)
               || (durationMillis >= 2200 && durationMillis < 2400)) {
        digitalWrite(ledNo, LOW);
    } else if ((durationMillis >= 400 && durationMillis < 600) || (durationMillis >= 1400 && durationMillis < 1600)
               || (durationMillis >= 2400 && durationMillis < 2600)) {
        digitalWrite(ledNo, HIGH);
    } else if ((durationMillis >= 600 && durationMillis < 1000) || (durationMillis >= 1600 && durationMillis < 2000)) {
        digitalWrite(ledNo, LOW);
    } else {
        digitalWrite(ledNo, LOW);
        indicatorStartTime = 0;
        lowVoltageLedIndicate = false;
        highVoltageLedIndicate = false;
        dryrunLedIndicate = false;
        overrunLedIndicate = false;
        leakageLedIndicate = false;
    }
}
void blinkLeakageErrorLed() {
    static unsigned long leakIndicatorStartTime = 0;
    if (leakIndicatorStartTime == 0) {
        leakIndicatorStartTime = millis();
    }
    unsigned long durationMillis = millis() - leakIndicatorStartTime;
    bool on = false;
    if ((durationMillis < 200) || (durationMillis >= 1000 && durationMillis < 1200)
        || (durationMillis >= 2000 && durationMillis < 2200)) {
        on = true;
    } else if ((durationMillis >= 400 && durationMillis < 600) || (durationMillis >= 1400 && durationMillis < 1600)
               || (durationMillis >= 2400 && durationMillis < 2600)) {
        on = true;
    }
    digitalWrite(LED_1, on ? HIGH : LOW);
    digitalWrite(LED_2, on ? HIGH : LOW);
    if (on) {
        lightColorLEDFast(RED);
    } else {
        lightColorLEDFast(BLACK);
    }

    if (durationMillis >= 6000) {
        digitalWrite(LED_1, LOW);
        digitalWrite(LED_2, LOW);
        lightColorLEDFast(BLACK);
        leakIndicatorStartTime = 0;
        leakageLedIndicate = false;
    }
}
void blinkColorLed(uint8_t ledNo) {
    static unsigned long colorIndicatorStartTime = 0;
    if (colorIndicatorStartTime == 0) {
        colorIndicatorStartTime = millis();
        digitalWrite(ledNo, HIGH);
    }
    unsigned long durationMillis = millis() - colorIndicatorStartTime;
    if ((durationMillis >= 0 && durationMillis < 200) || (durationMillis >= 1000 && durationMillis < 1200)
        || (durationMillis >= 2000 && durationMillis < 2200)) {
        digitalWrite(ledNo, HIGH);
    } else if ((durationMillis >= 200 && durationMillis < 400) || (durationMillis >= 1200 && durationMillis < 1400)
               || (durationMillis >= 2200 && durationMillis < 2400)) {
        digitalWrite(ledNo, LOW);
    } else if ((durationMillis >= 400 && durationMillis < 600) || (durationMillis >= 1400 && durationMillis < 1600)
               || (durationMillis >= 2400 && durationMillis < 2600)) {
        digitalWrite(ledNo, HIGH);
    } else if ((durationMillis >= 600 && durationMillis < 1000) || (durationMillis >= 1600 && durationMillis < 2000)) {
        digitalWrite(ledNo, LOW);
    } else {
        digitalWrite(ledNo, LOW);
        colorIndicatorStartTime = 0;
        disableWiFiLedIndicate = false;
        factoryResetLedIndicate = false;
        ignoreButton1FallOnce = false;
        ignoreButton2FallOnce = false;
        ignoreButton3FallOnce = false;
        ignoreButton4FallOnce = false;
        ignoreButtonOffFallOnce = false;
    }
}

void setAllIndicatorsOff() {
    if (ledType == 0) {
        digitalWrite(LED_SMART, LOW);
    } else {
        // lightColorLED(BLACK);
        lightColorLEDFast(BLACK);
    }
    digitalWrite(LED_1, LOW);
    digitalWrite(LED_2, LOW);
    digitalWrite(LED_3, LOW);
    digitalWrite(LED_4, LOW);
    lastBlinkTime = 0;   // last time LED was blink
    lastBlinkstatus = 0; // last time LED was blink
    indicatorStartTime = 0;
    colorLedBlinkstartTime = 0;
}

/*Funtion to show the log when the flow is not there*/
void logStartFlowAlert(char varRunType) {
    ModeOfAction errorMode;
    switch (varRunType) {
    case 'S':
    case 'C':
        errorMode = ModeOfAction::FLOW_NOT_DETECTED;
        break;
    case 'R':
        errorMode = ModeOfAction::FLOW_NOT_DETECTED;
        break;
    case 'W':
        errorMode = ModeOfAction::FLOW_NOT_DETECTED;
        break;
    case 'Q':
    case 'H':
        errorMode = ModeOfAction::FLOW_NOT_DETECTED;
        break;
    case '1':
        errorMode = ModeOfAction::FLOW_NOT_DETECTED;
        break;
    case '2':
        errorMode = ModeOfAction::FLOW_NOT_DETECTED;
        break;
    case '3':
        errorMode = ModeOfAction::FLOW_NOT_DETECTED;
        break;
    case '4':
        errorMode = ModeOfAction::FLOW_NOT_DETECTED;
        break;
    default:
        break;
    }
    logAction(errorMode, 0, 0, 0, ScheduleType::SCH_NONE);
}
void logStartFloatAlert(char varRunType) {
    // Serial.println("Not turning on - float off");
    // Serial.println(varRunType);
    floatStateLedIndicate = true;

    String messageForMQTT;
    ModeOfAction errorMode;
    switch (varRunType) {
    case 'S':
    case 'C':
        errorMode = ModeOfAction::START_SCHEDULED_FLOAT_REJECT;
        break;
    case 'R':
        errorMode = ModeOfAction::START_POWER_RESUME_FLOAT_REJECT;
        break;
    case 'W':
        errorMode = ModeOfAction::START_WEB_FLOAT_REJECT;
        break;
    case 'Q':
    case 'H':
        errorMode = ModeOfAction::START_MQTT_FLOAT_REJECT;
        break;
    case '1':
        errorMode = ModeOfAction::START_MANUAL_1_FLOAT_REJECT;
        break;
    case '2':
        errorMode = ModeOfAction::START_MANUAL_2_FLOAT_REJECT;
        break;
    case '3':
        errorMode = ModeOfAction::START_MANUAL_3_FLOAT_REJECT;
        break;
    case '4':
        errorMode = ModeOfAction::START_MANUAL_4_FLOAT_REJECT;
        break;
    default:
        break;
    }
    logAction(errorMode, 0, 0, 0, ScheduleType::SCH_NONE);
    messageForMQTT = modeMessages[errorMode];
}

void logStartHVAlert(char varRunType, float alertVoltage) {
    Serial.println("Not turning on - High voltage off");
    highVoltageLedIndicate = true;

    String messageForMQTT;
    ModeOfAction errorMode;
    switch (varRunType) {
    case 'S':
    case 'C':
        errorMode = ModeOfAction::START_SCHEDULED_HV_REJECT;
        break;
    case 'R':
        errorMode = ModeOfAction::START_POWER_RESUME_HV_REJECT;
        break;
    case 'W':
        errorMode = ModeOfAction::START_WEB_HV_REJECT;
        break;
    case 'Q':
    case 'H':
        errorMode = ModeOfAction::START_MQTT_HV_REJECT;
        break;
    case '1':
        errorMode = ModeOfAction::START_MANUAL_1_HV_REJECT;
        break;
    case '2':
        errorMode = ModeOfAction::START_MANUAL_2_HV_REJECT;
        break;
    case '3':
        errorMode = ModeOfAction::START_MANUAL_3_HV_REJECT;
        break;
    case '4':
        errorMode = ModeOfAction::START_MANUAL_4_HV_REJECT;
        break;
    default:
        break;
    }

    messageForMQTT = modeMessages[errorMode];
    if (alertVoltage != 0) //&& alertVoltage>VOLTAGE_NOT_AVAILABLE)
        messageForMQTT = messageForMQTT + "(Voltage : " + alertVoltage + " )";
    logAction(errorMode, 0, alertVoltage, 0, ScheduleType::SCH_NONE);
}

void logStartLVAlert(char varRunType, float alertVoltage) {
    Serial.println("Not turning on - Low voltage off");
    lowVoltageLedIndicate = true;
    String messageForMQTT;
    ModeOfAction errorMode;
    switch (varRunType) {
    case 'S':
    case 'C':
        errorMode = ModeOfAction::START_SCHEDULED_LV_REJECT;
        break;
    case 'R':
        errorMode = ModeOfAction::START_POWER_RESUME_LV_REJECT;
        break;
    case 'W':
        errorMode = ModeOfAction::START_WEB_LV_REJECT;
        break;
    case 'Q':
    case 'H':
        errorMode = ModeOfAction::START_MQTT_LV_REJECT;
        break;
    case '1':
        errorMode = ModeOfAction::START_MANUAL_1_LV_REJECT;
        break;
    case '2':
        errorMode = ModeOfAction::START_MANUAL_2_LV_REJECT;
        break;
    case '3':
        errorMode = ModeOfAction::START_MANUAL_3_LV_REJECT;
        break;
    case '4':
        errorMode = ModeOfAction::START_MANUAL_4_LV_REJECT;
        break;
    default:
        break;
    }

    messageForMQTT = modeMessages[errorMode];
    if (alertVoltage != 0) //&& alertVoltage>VOLTAGE_NOT_AVAILABLE)
        messageForMQTT = messageForMQTT + "(Voltage : " + alertVoltage + " )";
    logAction(errorMode, 0, alertVoltage, 0, ScheduleType::SCH_NONE);
}

int handleRelayOn(char varRunType, DateTime paraDateTime, TimeSpan duration) {
    // Returns 0 for successful start, -1 for float alert and hence not started

    // Serial.println(" : switch on " + String(varRunType) + formatDateTime(paraDateTime));

    // Set before the rejection checks below, not after a successful start: a start
    // refused by a prevailing fault changes the schedule status just as much as one
    // that runs, and the cloud needs to hear about it either way.
    schStatusPublishPending = true;

    if (floatingAlertState) {
        logStartFloatAlert(varRunType);
        manualOff = 3;
        return -1;
    }
    if (highVoltageAlertState) {
        logStartHVAlert(varRunType);
        // lastHighVoltageTime = paraDateTime;
        // storeDateTimeToEEPROM(paraDateTime, LAST_HIGHVOLTAGE_TIME_ADDRESS);
        if (varRunType == 'S' || varRunType == 'C' || varRunType == 'R')
            highVoltageAlertLogged = true;
        resetPowerTimer();
        return -1;
    }
    if (lowVoltageAlertState) {
        logStartLVAlert(varRunType);
        lastLowVoltageTime = paraDateTime;
        // storeDateTimeToEEPROM(paraDateTime, LAST_LOWVOLTAGE_TIME_ADDRESS);
        if (varRunType == 'S' || varRunType == 'C' || varRunType == 'R')
            lowVoltageAlertLogged = true;
        resetPowerTimer();
        return -1;
    }
    if (phaseFailureAlertState || phaseSequenceReverseAlertState) {
        Serial.println("Not turning on - Phase fault");
        // One generic code for every start path - no per-source switch like
        // logStartLVAlert()/logStartHVAlert(); the fault is the same whoever asked.
        //
        // phaseAlertLogged is the same idea as lowVoltageAlertLogged, shared by both
        // phase faults: the scheduled and flow starts re-enter every LOOP_INTERVEL
        // while the fault lasts, so without it this would write a log line every
        // 200ms. handleSwitching() skips the whole start block once it is set, and
        // clears it when the fault goes away or the window ends - so the refusal is
        // recorded once per fault episode. No stamp of lastPhaseFailureTime here,
        // unlike the low-voltage branch above: checkPhaseFaults() already holds it
        // at "now" for every cycle the fault is asserted, so a reject can never see
        // a stale one.
        logAction(phaseFailureAlertState ? ModeOfAction::PHASE_FAILURE_START_REJECT
                                         : ModeOfAction::PHASE_SEQUENCE_REVERSE_START_REJECT,
                  0, 0, 0, ScheduleType::SCH_NONE);
        if (varRunType == 'S' || varRunType == 'C' || varRunType == 'R' || varRunType == 'L')
            phaseAlertLogged = true;
        resetPowerTimer();
        return -1;
    }
    // if (!flowIncomingState && isFloatEnabled == 2) {
    // logStartFlowAlert(varRunType);
    // return -1;
    // Devices with Flow start configured. Float mode= flow.
    if (isFloatEnabled == 2) {
        flowIncomingState = true;
        flow_stop_time = 0;
        Flow_stopped_manual = false;
        // When forced Flow start is done, it will be a manual start like '1' or 'W'. In flow enebled devices, it will be started with 'f'
        if (varRunType != 'L')
            varRunType = 'f';
        duration = TimeSpan(0, 23, 59, 0);
    }
    setAllIndicatorsOff();
    resetPowerTimer();

    // Add timeout for relay control
    unsigned long relayStartTime = millis();
    const unsigned long RELAY_TIMEOUT_MS = 5000; // 5 second timeout

    while (!relayState && (millis() - relayStartTime < RELAY_TIMEOUT_MS)) {
        // Serial.println("_");
        // noInterrupts();

        if (digitalRead(ZERO_CROSS)) {
            while (digitalRead(ZERO_CROSS) == 1) {
                if ((millis() - relayStartTime) > 15) {
                    break;
                }
            }
            delayMicroseconds(1400);
            digitalWrite(RELAY_OUT, HIGH);

        } else {
            while (digitalRead(ZERO_CROSS) == 0) {
                if ((millis() - relayStartTime) > 15) {
                    break;
                }
            }
            delayMicroseconds(1400);
            digitalWrite(RELAY_OUT, HIGH);
        }

        if (tappingRequired == 2 && tappingDuration > 0) {
            digitalWrite(RELAY_OUT_POWER_TAP, HIGH);
            tappingStartTime = millis();
            // Serial.println("Tapping starts");

        } else if (tappingRequired == 1) {
            digitalWrite(RELAY_OUT_POWER_TAP, HIGH);
        }
        // delay(1);
        relayState = digitalRead(RELAY_OUT);
        // interrupts();

        // If leak was detected during relay-on, stop immediately
        // if (leakageDetected) {
        //     relayState = 0;
        //     break;
        // }

        if (!relayState) {
            logAction(ModeOfAction::ABNORMAL_CASE_ON_NOT_TRIGGERED, 0, 0, 0, ScheduleType::SCH_NONE);
            yield();   // Allow other tasks to run
            delay(10); // Small delay to prevent tight loop
        }
    }

    if (!relayState) {
        Serial.println("Relay ON timeout - relay may be stuck");
        return -1; // Indicate failure
    }

    relayOnTime = millis(); // relayOnTime
    manualOff = 0;
    relayOnDuration = duration;
    // Cleared for every run; the Duration Based start paths set it right after this
    // call, the only ones that can end on the owed duration rather than the window.
    runEndsOnDurationSpent = false;
    runType = varRunType;
    calculatedScheduledOffTime = paraDateTime + duration;
    ;
    // BL0939_setup(); // commented out as it is causing delay and watchdog timeout
    if (varRunType != 'R' && varRunType != 'S' && varRunType != 'C') {
        tempEvent.startTime = paraDateTime;
        tempEvent.endTime = calculatedScheduledOffTime;
        tempEvent.scheduleType = ScheduleType::SCH_NONE;
        // tempEvent.restartOption = PowerResumeMode::YES ;
        updateTempScheduleDetails(tempEvent, varRunType);
        scheduleChanged = true;
    }
    // Save energy data from time to time
    relayOnRTCTime = paraDateTime.unixtime(); // reuse current time passed in (relay-on instant)
    // Serial.println("energy-when on");
    // Serial.println(energy);
    lastEnergySaveTime = millis();
    lastMQTTUpdateTime = 0;
    // energyOnStart = energy;
    if (energy < 0) {
        char msg[DEBUG_MESSAGE_SIZE];
        snprintf(msg, sizeof(msg),
                 "Negative energy at relay-on: energy=%.3f", energy);
        logDebugAction(DebugAction::SYSTEM_INFO, msg);
    }
    energyOnStart = energy > 0 ? energy : 0; // Ensure energyOnStart is not negative
    //if(!zcrossTime){
        initiateTempEnergyData(relayOnRTCTime);
    //}
    // Serial.println("energy-when on");
    // Serial.println(energy);

    // calculatedOffTime= DateTime(0,0,0,0,0,0);
    // manualOff = 0;
    String messageForMQTT;
    ModeOfAction actionMode;
    switch (varRunType) {
    case 'S':
        actionMode = ModeOfAction::START_SCHEDULED;
        break;
    case 'C':
        actionMode = ModeOfAction::START_CYCLIC;
        break;
    case 'R':
        actionMode = ModeOfAction::START_POWER_RESUME;
        break;
    case 'W':
        actionMode = ModeOfAction::START_WEB;
        break;
    case 'Q':
        actionMode = ModeOfAction::START_MQTT;
        break;
    case 'H':
        actionMode = ModeOfAction::START_WEBHOOK;
        break;
    case 'L':
        actionMode = ModeOfAction::START_FLOW_SENSOR;
        break;
    case 'f':
        actionMode = ModeOfAction::FORCE_START_FLOW;
        break;
    case '1':
        actionMode = ModeOfAction::START_MANUAL_1;
        break;
    case '2':
        actionMode = ModeOfAction::START_MANUAL_2;
        break;
    case '3':
        actionMode = ModeOfAction::START_MANUAL_3;
        break;
    case '4':
        actionMode = ModeOfAction::START_MANUAL_4;
        break;
    }

    messageForMQTT = modeMessages[actionMode];
    countMQTTMessagesAfterRelayOff = 0;

    // Calculate duration in minutes for logging
    uint16_t durationMinutes = duration.totalseconds() / 60;
    ScheduleType scheduleType = ScheduleType::SCH_NONE;
    // Serial.println("inside relayOn - duration : ");
    // Serial.println(duration.totalseconds());

    if (varRunType == 'S' || varRunType == 'C' || varRunType == 'R') {
        // Scheduled start, enter the type of schedule
        scheduleType = nextEvent.scheduleType;
    }

    // paraDateTime is the relay-on instant - stamp the log with it so queue lag
    // can't push the recorded time later than the event.
    logAction(actionMode, currentToOutput, voltageToOutput, durationMinutes, scheduleType, paraDateTime.unixtime());

    resetButtonPress();
    resetOtherStartCommands();
    sendMQTTAttrib = true;
    // notifyClients();
    return 0;
}
void resetOtherStartCommands() {
    onTriggerFromMQTT = 0;
    onTriggerDurationFromMQTT = 0;
    onTriggerFromWebhook = 0;
    onTriggerDurationFromWebhook = 0;
    onTriggerFromInterface = 0;
    onTriggerDurationFromInterface = 0;
}
void handleRelayOnSchedule(DateTime scheduledOnTime, DateTime scheduledOffTime, DateTime paraDateTime) {
    // Daily/weekly schedules run to the window's end, so "window end - now" IS
    // the run length.
    TimeSpan duration = scheduledOffTime - paraDateTime; // reuse current time; keeps calculatedScheduledOffTime consistent
    bool endsOnDurationSpent = false;

    if (nextEvent.scheduleType == ScheduleType::DURATION) {
        // A Duration Based schedule owes a fixed amount of ON time anywhere
        // inside [schStartTime, schEndTime], so the window end (scheduledOffTime)
        // is only a clamp - the run length is what is still owed. getNextSchedule()
        // already netted off whatever ran earlier in this window (power cut, manual
        // off and restart), so nextEvent.remainingDuration is that figure.
        long windowRemainingSeconds = (long)(scheduledOffTime - paraDateTime).totalseconds();
        long secondsToRun = nextEvent.remainingDuration;
        // Whether this run stops on its owed time or on the window closing is
        // settled here, where both figures are in hand - at the stop only
        // relayOnDuration is left, and nextEvent may by then describe the next
        // occurrence instead of this one.
        endsOnDurationSpent = (secondsToRun < windowRemainingSeconds);
        if (secondsToRun > windowRemainingSeconds) {
            secondsToRun = windowRemainingSeconds;
        }

        if (secondsToRun <= 0) {
            // The owed duration has already been delivered in this window, or the
            // window has no room left - nothing to start, let the scheduler move
            // on to the next occurrence.
            Serial.println("handleRelayOnSchedule: DURATION target already met - skipping start");
            scheduleChanged = true;
            return;
        }
        duration = TimeSpan((uint32_t)secondsToRun);
    }

    handleRelayOn('S', paraDateTime, duration);
    // handleRelayOn() clears the flag for every run, so this must follow it.
    runEndsOnDurationSpent = endsOnDurationSpent;
    // Same instant as before for daily/weekly (paraDateTime + duration ==
    // scheduledOffTime there); for DURATION this is the real off time instead of
    // the window end, so the countdown shown to the user matches relayOnDuration.
    calculatedScheduledOffTime = paraDateTime + duration;
    notifyClients();
}

void handleRelayOnCyclic(DateTime scheduledOnTime, DateTime scheduledOffTime, DateTime paraDateTime) {
    TimeSpan duration = scheduledOffTime - paraDateTime; // reuse current time; keeps calculatedScheduledOffTime consistent
    handleRelayOn('C', paraDateTime, duration);
    calculatedScheduledOffTime = scheduledOffTime;
    notifyClients();
}

void handleRelayOnResume(DateTime scheduledOnTime, DateTime scheduledOffTime, DateTime paraDateTime) {
    TimeSpan duration;
    bool endsOnDurationSpent = false;

    if (nextEvent.scheduleType == ScheduleType::DURATION) {
        // Duration schedules must deliver a fixed total ON time within
        // [schStartTime, schEndTime] regardless of how many power cuts
        // interrupt it. "scheduledOffTime - paraDateTime" alone assumes no
        // outage ever happened, so it under-delivers by exactly the outage
        // length. Instead, find how much ON time has actually accumulated
        // so far (across however many segments the energy log has for this
        // window) and resume for only what's still owed.
        // The target is the schedule's own stored duration - NOT the window
        // length: for a DURATION event scheduledOnTime/scheduledOffTime are the
        // window bounds, so subtracting them would ask for the whole window.
        //
        // The elapsed figure is read here rather than taken from
        // getNextSchedule(): the energy entry for the segment that just ended is
        // written by the logging task, so at the instant getNextSchedule() ran it
        // may not have reached the file yet - which would resume for time already
        // delivered. By the time a resume is decided the write has landed.
        long targetSeconds = nextEvent.totalDuration;
        long elapsedSeconds = (long)getElapsedEnergyDurationInWindow(nextEvent.schStartTime, paraDateTime);
        long remainingSeconds = targetSeconds - elapsedSeconds;

        // Never run past the schedule's own window end either.
        long windowRemainingSeconds = (long)(nextEvent.schEndTime - paraDateTime).totalseconds();
        // As in handleRelayOnSchedule(): record now whether this run stops on the
        // owed time or on the window closing, so the stop can be logged for what
        // it is without re-deriving it from a by-then-stale nextEvent.
        endsOnDurationSpent = (remainingSeconds < windowRemainingSeconds);
        if (remainingSeconds > windowRemainingSeconds) {
            remainingSeconds = windowRemainingSeconds;
        }
        if (remainingSeconds < 0) {
            remainingSeconds = 0;
        }
        // Store the fresher figure back once, so every other reader - the start
        // gate in handleSwitching(), the DURATION_OVER status, a later resume -
        // works from this one instead of getNextSchedule()'s earlier estimate.
        nextEvent.remainingDuration = remainingSeconds;

        if (remainingSeconds <= 0) {
            // Target duration already met during the segments run before
            // this resume, or the window has closed - the relay is already
            // off (we're deciding whether to turn it on, not turning it
            // off), so just let the schedule recompute to the next occurrence.
            Serial.println("handleRelayOnResume: DURATION target already met - skipping resume");
            scheduleChanged = true;
            return;
        }
        duration = TimeSpan((uint32_t)remainingSeconds);
    } else {
        duration = scheduledOffTime - paraDateTime; // reuse current time; keeps calculatedScheduledOffTime consistent
    }

    if (handleRelayOn('R', paraDateTime, duration) == -1)
        return;
    // handleRelayOn() clears the flag for every run, so this must follow it.
    runEndsOnDurationSpent = endsOnDurationSpent;
    // calculatedScheduledOffTime=scheduledOffTime;
    notifyClients();
}

void handleRelayOnManualPress(uint8_t buttonNo, DateTime paraDateTime) {
    if (buttonNo >= 1 && buttonNo <= 4) {
        char buttonChar = static_cast<char>('1' + buttonNo - 1);
        TimeSpan duration = getDuration(buttonNo);

        // Serial.println("Duration: " + String(duration.days()) + "d " + String(duration.hours()) + "h " +
        // String(duration.minutes()) + "m " + String(duration.seconds()) + "s ");
        if (handleRelayOn(buttonChar, paraDateTime, duration) == -1)
            return;

        notifyClients();
    }
}
void handleRelayOnfromWeb(DateTime paraDateTime, TimeSpan duration) {
    // Serial.println("inside web on... duration: ");
    // Serial.println(duration.totalseconds());
    if (handleRelayOn('W', paraDateTime, duration) == -1)
        return;

    notifyClients();
    // Serial.print(calculatedScheduledOffTime.hour());
    // Serial.print(":");
    // Serial.println(calculatedScheduledOffTime.minute()); //calculatedScheduledOffTime.hour()+
    // calculatedScheduledOffTime.minute());
}

void handleRelayOnfromMQTT(DateTime paraDateTime, TimeSpan duration) {
    // Serial.println("inside mqtt on: ");
    if (handleRelayOn('Q', paraDateTime, duration) == -1)
        return;
    // Serial.println("after handle relayon mqtt: ");
    // calculatedScheduledOffTime= paraDateTime + duration;;

    notifyClients();
}

void handleRelayOnfromWebhook(DateTime paraDateTime, TimeSpan duration) {
    // Serial.println("inside webhook on: ");
    if (handleRelayOn('H', paraDateTime, duration) == -1)
        return;
    // Serial.println("after handle relayon webhook: ");

    notifyClients();
}

void handleRelayOnFlow(DateTime paraDateTime) {
    // Serial.println("inside mqtt on: ");
    TimeSpan duration = TimeSpan(0, 23, 59, 0);
    if (handleRelayOn('L', paraDateTime, duration) == -1)
        return;
    // Serial.println("after handle relayon mqtt: ");
    // calculatedScheduledOffTime= paraDateTime + duration;;

    notifyClients();
}
void handleRelayOFF(char varOffType, DateTime paraDateTime, bool phaseProblem) {
    // relayState=0;
    // Snapshot the guarded session energy BEFORE switching: monitorPower()
    // zeroes energyToOutput once relayState is 0, so it must be read here.
    // energyToOutput is protected against CF_CNT decreases (chip reset,
    // rollover, glitch frames), unlike a raw energy - energyOnStart delta.
    float sessionEnergy = energyToOutput;
    // Serial.println(" : switch off - "+String(varOffType)+formatDateTime(paraDateTime));

    // Add timeout for relay control
    unsigned long relayStartTime = millis();
    const unsigned long RELAY_TIMEOUT_MS = 5000; // 5 second timeout

    while ((relayState || phaseProblem)
           && (millis() - relayStartTime < RELAY_TIMEOUT_MS)) { // wait for the relay to be off
        Serial.println("inside ");
        noInterrupts();
        if (digitalRead(ZERO_CROSS)) {
            while (digitalRead(ZERO_CROSS) == 1) {
                if ((millis() - relayStartTime) > 15) {
                    break;
                }
            }
            delayMicroseconds(4300);
            digitalWrite(RELAY_OUT, LOW);

        } else {
            while (digitalRead(ZERO_CROSS) == 0) {
                if ((millis() - relayStartTime) > 15) {
                    break;
                }
            }
            delayMicroseconds(4300);
            digitalWrite(RELAY_OUT, LOW);
        }
        digitalWrite(RELAY_OUT_POWER_TAP, LOW);
        digitalWrite(RELAY_OUT_POWER_TAP2, LOW);
        // Serial.println("Tapping finished by relay off");
        phaseProblem = false;

        // delay(1);
        relayState = digitalRead(RELAY_OUT);
        interrupts();

        if (relayState) {
            logAction(ModeOfAction::ABNORMAL_CASE_OFF_NOT_TRIGGERED, 0, 0, 0, ScheduleType::SCH_NONE);
            yield();   // Allow other tasks to run
            delay(10); // Small delay to prevent tight loop
        }
    }

    if (relayState) {
        Serial.println("Relay OFF timeout - relay may be stuck");
        // May need to restart the device, if it is struck.
        return;
    }

    relayOffTime = millis();
    lastMQTTUpdateTime = 0;
    runType = '0';
    runEndsOnDurationSpent = false; // belongs to the run that just ended
    schStatusPublishPending = true; // the stop's reason goes to the cloud from handleMQTTMessaging()
    // calculatedOffTime= DateTime(0, 0, 0, 0, 0, 0);
    calculatedScheduledOffTime = DateTime(0, 0, 0, 0, 0, 0);
    setAllIndicatorsOff();
    if (varOffType != 'A' && varOffType != 'B' && varOffType != 'P' && varOffType != 'R') {
        Serial.println("Clearing temp sch");
        clearTempScheduleDetails();
    }
    String messageForMQTT;
    ModeOfAction actionMode;

    switch (varOffType) {
    case 'S':
        actionMode = ModeOfAction::STOP_SCHEDULED;
        break;
    case 'D':
        // Duration Based schedule whose owed ON time is spent, with the window
        // still open - distinct from 'S', the window itself ending.
        actionMode = ModeOfAction::STOP_DURATION_COMPLETED;
        break;
    case 'M':
        actionMode = ModeOfAction::STOP_MANUAL;
        break;
    case 'F':
        floatStateLedIndicate = true;
        actionMode = ModeOfAction::STOP_FLOAT;
        break;
    case 'L':
        actionMode = ModeOfAction::STOP_FLOW_SENSOR;
        break;
    case 'W':
        actionMode = ModeOfAction::STOP_WEB;
        break;
    case 'Q':
        actionMode = ModeOfAction::STOP_MQTT;
        break;
    case 'H':
        actionMode = ModeOfAction::STOP_WEBHOOK;
        break;
    case 'B':
        actionMode = ModeOfAction::STOP_MAXTIME_BREAK;
        break;
    case 'T':
        actionMode = ModeOfAction::STOP_TIMER;
        break;
    case 'P':
        actionMode = ModeOfAction::STOP_PHASE_FAILURE;
        break;
    case 'R':
        actionMode = ModeOfAction::STOP_PHASE_SEQUENCE_REVERSE;
        break;
    case 'A':
        if (leakageDetected) {
            Serial.println("inside Relay Stop- leakageDetected");
            leakageLedIndicate = true;
            // overrunLedIndicate = true;
            actionMode = ModeOfAction::LEAKAGE_DETECTED;
        } else if (dryRunAlertState) {
            dryrunLedIndicate = true;
            actionMode = ModeOfAction::STOP_DRYRUN;
        } else if (overRunAlertState) {
            overrunLedIndicate = true;
            actionMode = ModeOfAction::STOP_OVERRUN;
        } else if (lowVoltageAlertState) {
            lowVoltageLedIndicate = true;
            actionMode = ModeOfAction::STOP_LOWVOLTAGE;
        } else if (highVoltageAlertState) {
            highVoltageLedIndicate = true;
            actionMode = ModeOfAction::STOP_HIGHVOLTAGE;
        }
        break;
    }
    notifyClients(false);
    // paraDateTime is the relay-off instant - see the matching call in handleRelayOn().
    logAction(actionMode, currentToOutput, voltageToOutput, 0, ScheduleType::SCH_NONE, paraDateTime.unixtime());

    // Serial.println("energy-when off");
    // Serial.println(energy);
    scheduleChanged = true;
    // Use the guarded session energy so a CF_CNT decrease during the session
    // can never subtract from (or corrupt) the cumulative total / energy logs.
    // (Anomalies are already logged in monitorPower()/Decode().)
    logEnergyData(relayOnRTCTime, paraDateTime.unixtime(), sessionEnergy); // reuse current time passed in (relay-off instant)
    cumulativeEnergy = cumulativeEnergy + sessionEnergy;

    resetTempEnergyData();
    resetButtonPress();
    resetPowerTimer();
    lastUIUpdateTime = millis() - 9000; // To trigger next update to UI in a second.
}

// Function to periodically check master state when slave sent a button press
void checkMasterState() {
    // Only check if masterSwitchOn flag is set and master URL is configured
    if (!masterSwitchOn || masterDeviceUrl.length() == 0) {
        return;
    }

    unsigned long currentTime = millis();
    // Check every 20 seconds (
    // Todo - need to tune
    if (currentTime - lastMasterCheckTime < 20000) {
        return;
    }

    lastMasterCheckTime = currentTime;

    if (WiFi.status() != WL_CONNECTED && !ethernetConnected) {
        Serial.println("WiFi/Ethernet not connected, cannot check master state");
        return;
    }

    HTTPClient http;
    WiFiClient client;

    // Build the getState URL
    String url = masterDeviceUrl;
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
        url = "http://" + url;
    }

    if (url.endsWith("/")) {
        url = url.substring(0, url.length() - 1);
    }

    String fullUrl = url + "/api/getState";

    Serial.println("Checking master state: " + fullUrl);

    bool requestSuccessful = false;

    if (http.begin(client, fullUrl)) {
        http.setTimeout(3000); // 3 second timeout for status check

        int httpResponseCode = http.GET();

        if (httpResponseCode == 200) {
            String response = http.getString();
            response.trim(); // Remove whitespace
            Serial.println("Master state: " + response);

            if (response == "OFF") {
                // Master is OFF, stop monitoring
                Serial.println("Master is OFF, stopping monitoring");
                masterSwitchOn = false;
                masterTimeoutCount = 0;
            } else if (response == "ON") {
                // Master is still ON, reset timeout count and continue blinking
                Serial.println("Master is still ON, continuing LED blink");
                masterTimeoutCount = 0;
            }
            requestSuccessful = true;
        } else {
            Serial.println("Master state check failed: " + String(httpResponseCode));
        }

        http.end();
    } else {
        Serial.println("Unable to connect to master device");
    }

    // Handle timeout logic for any failure
    if (!requestSuccessful) {
        masterTimeoutCount++;
        Serial.println("Timeout count: " + String(masterTimeoutCount) + "/3");

        if (masterTimeoutCount >= 3) {
            Serial.println("3 consecutive timeouts, stopping monitoring");
            masterSwitchOn = false;
            masterTimeoutCount = 0;
        }
    }
}

// Function to handle LED indication when master switch is ON
void handleMasterSwitchStatusLed() {
    if (!masterSwitchOn && !masterSwitchOnStatusMqtt) {
        return;
    }

    if (millis() - lastBlinkTime > 500) {
        lastBlinkTime = millis();
        lastBlinkstatus = !lastBlinkstatus;
        if (ledType == 0) {
            digitalWrite(LED_SMART, lastBlinkstatus ? HIGH : LOW);
        } else {
            if (lastBlinkstatus) {
                // lightColorLED(BLUE);
                lightColorLEDFast(BLUE);
            } else {
                // lightColorLED(BLACK);
                lightColorLEDFast(BLACK);
            }
        }
    }
}

// Function to send button press event to master device
void sendButtonPressToMaster(uint8_t buttonNo, String state, bool shouldLog) {
    if (masterDeviceUrl.length() == 0) {
        Serial.println("Master device URL is not configured");
        return;
    }

    if (WiFi.status() != WL_CONNECTED && !ethernetConnected) {
        Serial.println("WiFi/Ethernet not connected, cannot send to master");
        return;
    }

    HTTPClient http;
    WiFiClient client;

    // Build the URL with state and button number
    String url = masterDeviceUrl;
    if (!url.startsWith("http://") && !url.startsWith("https://")) {
        url = "http://" + url;
    }

    // Remove trailing slash if present
    if (url.endsWith("/")) {
        url = url.substring(0, url.length() - 1);
    }

    String fullUrl = url + "/api/setState?state=" + state;
    if ((buttonNo >= 1 && buttonNo <= 4) || buttonNo == 11) {
        fullUrl += "&mode=buttonpress&buttonNo=" + String(buttonNo);
    }

    Serial.println("Sending request to master: " + fullUrl);

    // Create JSON payload with PIN
    JsonDocument doc;
    doc["pin"] = mPin;
    String payload;
    serializeJson(doc, payload);

    // Helper lambda to log failures based on state (only if shouldLog is true)
    auto logFailure = [&state, shouldLog]() {
        if (shouldLog) {
            ModeOfAction action = (state == "ON") ? ModeOfAction::START_MASTER_BUTTON_PRESS_SENDING_FAILED
                                                  : ModeOfAction::STOP_MASTER_BUTTON_PRESS_SENDING_FAILED;
            logAction(action, 0, 0, 0, ScheduleType::SCH_NONE);
            logRefreshRequired = true; // Set flag to refresh logs for failure log
        }
    };

    if (http.begin(client, fullUrl)) {
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(5000); // 5 second timeout

        int httpResponseCode = http.POST(payload);

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println("Master device response code: " + String(httpResponseCode));
            Serial.println("Master device response: " + response);

            if (httpResponseCode == 200) {
                setAllIndicatorsOff();
                // Successful communication - set flag to start monitoring master state
                if (state == "ON" && ((buttonNo >= 1 && buttonNo <= 4) || buttonNo == 11)) {
                    masterSwitchOn = true;
                    lastMasterCheckTime = millis();
                    masterTimeoutCount = 0;
                    Serial.println("Started monitoring master state for button " + String(buttonNo));
                    if (shouldLog) {
                        logAction(ModeOfAction::START_MASTER_BUTTON_PRESS_SUCCESS, 0, 0, 0, ScheduleType::SCH_NONE);
                    }
                } else if (state == "OFF") {
                    // OFF command sent successfully, stop monitoring
                    masterSwitchOn = false;
                    masterTimeoutCount = 0;
                    if (shouldLog) {
                        logAction(ModeOfAction::STOP_MASTER_BUTTON_PRESS_SUCCESS, 0, 0, 0, ScheduleType::SCH_NONE);
                    }
                }
            } else if (httpResponseCode == 201) {
                if (state == "ON") {
                    Serial.println("Master device returned error code: " + String(httpResponseCode));
                    masterSwitchOn = true;
                    lastMasterCheckTime = millis();
                    masterTimeoutCount = 0;
                    if (shouldLog) {
                        logAction(ModeOfAction::START_MASTER_BUTTON_PRESS_ALREADY_ON, 0, 0, 0, ScheduleType::SCH_NONE);
                        logRefreshRequired = true; // Set flag to refresh logs for failure log
                    }
                } else {
                    masterSwitchOn = false;
                    masterTimeoutCount = 0;
                    if (shouldLog) {
                        logAction(ModeOfAction::STOP_MASTER_BUTTON_PRESS_ACTION_FAILED, 0, 0, 0,
                                  ScheduleType::SCH_NONE);
                        logRefreshRequired = true; // Set flag to refresh logs for failure log
                    }
                }

            } else if (httpResponseCode == 202) {
                if (state == "ON") {
                    Serial.println("Master device returned error code: " + String(httpResponseCode));
                    masterSwitchOn = false;
                    lastMasterCheckTime = millis();
                    masterTimeoutCount = 0;
                    if (shouldLog) {
                        logAction(ModeOfAction::START_MASTER_BUTTON_PRESS_ACTION_FAILED, 0, 0, 0,
                                  ScheduleType::SCH_NONE);
                        logRefreshRequired = true; // Set flag to refresh logs for failure log
                    }
                }

            } else {
                Serial.println("Master device returned error code: " + String(httpResponseCode));
                logFailure();
            }
        } else {
            Serial.println("Error sending to master device: " + String(httpResponseCode));
            logFailure();
        }

        http.end();
    } else {
        Serial.println("Unable to connect to master device");
        logFailure();
    }
}
