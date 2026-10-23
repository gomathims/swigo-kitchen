#include "buttontiming.h"

// Define global variables
TimerConfig buttonTimingConfig = TimerConfig::TIMERCONFIG_5M;
unsigned long button1Duration = 0;
unsigned long button2Duration = 0;
unsigned long button3Duration = 0;
unsigned long button4Duration = 0;

void initButtonTiming() {
    Serial.print("inside initButtonTiming");
    char customTiming[21];
    unsigned long duration1, duration2, duration3, duration4;
    TimerConfig tempTimingConfig;
    readButtonTimingDetails(tempTimingConfig, duration1, duration2, duration3, duration4);
    if (tempTimingConfig == TimerConfig::TIMERCONFIG_CUSTOM) {
        Serial.print("inside initButtonTiming TIMERCONFIG_CUSTOM");

        buttonTimingConfig = tempTimingConfig;
        button1Duration = duration1;
        button2Duration = duration2;
        button3Duration = duration3;
        button4Duration = duration4;
    } else {
        Serial.println("inside initButtonTiming STANDARD");

        buttonTimingConfig = tempTimingConfig;
        Serial.println((uint8_t)buttonTimingConfig);
        button1Duration = getSecondsForRegularPlates(1);
        button2Duration = getSecondsForRegularPlates(2);
        button3Duration = getSecondsForRegularPlates(3);
        button4Duration = getSecondsForRegularPlates(4);
    }
    Serial.println("button1Duration");
    Serial.println(button1Duration);
}

unsigned long getSecondsForRegularPlates(uint8_t buttonNo) {
    if (buttonNo >= 1 && buttonNo <= 4) {
        switch ((uint8_t)buttonTimingConfig) {
        case TIMERCONFIG_1H:
            return TimeSpan(0, buttonNo * 1, 0, 0).totalseconds();
        case TIMERCONFIG_4H:
            return TimeSpan(0, buttonNo * 4, 0, 0).totalseconds();
        case TIMERCONFIG_5M:
            return TimeSpan(0, 0, buttonNo * 5, 0).totalseconds();
        case TIMERCONFIG_15M:
            return TimeSpan(0, 0, buttonNo * 15, 0).totalseconds();
        default:
            return TimeSpan(0, 0, 0, buttonNo * 5).totalseconds();
        }
    }
    return TimeSpan(0, 0, 0, 0).totalseconds();
}

TimeSpan getDuration(uint8_t buttonNo) {
    // Serial.println("getduration");
    // Serial.println(button1Duration);
    // Serial.println(buttonNo);
    switch (buttonNo) {
    case 1:
        return TimeSpan(button1Duration);
    case 2:
        return TimeSpan(button2Duration);
    case 3:
        return TimeSpan(button3Duration);
    case 4:
        return TimeSpan(button4Duration);
    case 11:
        // Button 11 = 23H:59M duration
        return TimeSpan(0, 23, 59, 0);
    default:
        return TimeSpan(0,0,0,0);
    }
}

bool saveDeviceTimingConfig(unsigned char *jsonStr) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        return false;
    }
    const char *timingConfigStr = doc["tConf"];
    TimerConfig timingConfig;
    if (strcmp(timingConfigStr, "CUSTOM") != 0) {
        if (strcmp(timingConfigStr, "5M") == 0) {
            timingConfig = TIMERCONFIG_5M;
        } else if (strcmp(timingConfigStr, "15M") == 0) {
            timingConfig = TIMERCONFIG_15M;
        } else if (strcmp(timingConfigStr, "1H") == 0) {
            timingConfig = TIMERCONFIG_1H;
        } else if (strcmp(timingConfigStr, "4H") == 0) {
            timingConfig = TIMERCONFIG_4H;
        } else {
            timingConfig = TIMERCONFIG_5M;
        }
        Serial.println("before updateButtonTimingDetails - STANDARD");
        Serial.println("timingConfig");
        Serial.println(timingConfig);
        updateButtonTimingDetails(timingConfig);
        initButtonTiming();
        Serial.println("after update timingConfig");
        Serial.println(timingConfig);
        return true;
    } else {
        timingConfig = TIMERCONFIG_CUSTOM;

        int16_t button1Value = doc["b1"]["value"];
        String button1Unit = doc["b1"]["unit"];
        int16_t button2Value = doc["b2"]["value"];
        String button2Unit = doc["b2"]["unit"];
        int16_t button3Value = doc["b3"]["value"];
        String button3Unit = doc["b3"]["unit"];
        int16_t button4Value = doc["b4"]["value"];
        String button4Unit = doc["b4"]["unit"];

        if (button1Value > 0 && button1Value <= 999 && (button1Unit == "H" || button1Unit == "M" || button1Unit == "S")
            && button2Value > 0 && button2Value <= 999
            && (button2Unit == "H" || button2Unit == "M" || button2Unit == "S") && button3Value > 0
            && button3Value <= 999 && (button3Unit == "H" || button3Unit == "M" || button3Unit == "S")
            && button4Value > 0 && button4Value <= 999
            && (button4Unit == "H" || button4Unit == "M" || button4Unit == "S")) {
            unsigned long button1Config = button1Unit == "H"   ? button1Value * 60 * 60
                                          : button1Unit == "M" ? button1Value * 60
                                                               : button1Value;
            unsigned long button2Config = button2Unit == "H"   ? button2Value * 60 * 60
                                          : button2Unit == "M" ? button2Value * 60
                                                               : button2Value;
            unsigned long button3Config = button3Unit == "H"   ? button3Value * 60 * 60
                                          : button3Unit == "M" ? button3Value * 60
                                                               : button3Value;
            unsigned long button4Config = button4Unit == "H"   ? button4Value * 60 * 60
                                          : button4Unit == "M" ? button4Value * 60
                                                               : button4Value;
            if (button1Config >= 24 * 60 * 60 || button2Config >= 24 * 60 * 60 || button3Config >= 24 * 60 * 60
                || button4Config >= 24 * 60 * 60)
                return false;
            Serial.println("before updateButtonTimingDetails");
            Serial.println("timingConfig");
            Serial.println(timingConfig);
            String formattedString = "";
            formattedString += doc["b1"]["value"].as<String>() + doc["b1"]["unit"].as<String>() + "-";
            formattedString += doc["b2"]["value"].as<String>() + doc["b2"]["unit"].as<String>() + "-";
            formattedString += doc["b3"]["value"].as<String>() + doc["b3"]["unit"].as<String>() + "-";
            formattedString += doc["b4"]["value"].as<String>() + doc["b4"]["unit"].as<String>();

            updateButtonTimingDetails(timingConfig, button1Config, button2Config, button3Config, button4Config,
                                      formattedString.c_str());
            buttonTimingConfig = timingConfig;
            button1Duration = button1Config;
            button2Duration = button2Config;
            button3Duration = button3Config;
            button4Duration = button4Config;
            return true;
        } else
            return false;
    }
}

void populateCustomTimingConfig(JsonDocument &doc) {
    doc["tConf"] = "CUSTOM";
    char buttonTimingStr[20];
    readButtonTimingString(buttonTimingStr, sizeof(buttonTimingStr));
    String timingStr = String(buttonTimingStr);
    int index1 = timingStr.indexOf('-');
    int index2 = timingStr.indexOf('-', index1 + 1);
    int index3 = timingStr.indexOf('-', index2 + 1);

    String button1Str = timingStr.substring(0, index1);
    String button2Str = timingStr.substring(index1 + 1, index2);
    String button3Str = timingStr.substring(index2 + 1, index3);
    String button4Str = timingStr.substring(index3 + 1);

    JsonObject b1 = doc["b1"].to<JsonObject>();
    b1["value"] = button1Str.substring(0, button1Str.length() - 1).toInt();
    b1["unit"] = button1Str.substring(button1Str.length() - 1);

    JsonObject b2 = doc["b2"].to<JsonObject>();
    b2["value"] = button2Str.substring(0, button2Str.length() - 1).toInt();
    b2["unit"] = button2Str.substring(button2Str.length() - 1);

    JsonObject b3 = doc["b3"].to<JsonObject>();
    b3["value"] = button3Str.substring(0, button3Str.length() - 1).toInt();
    b3["unit"] = button3Str.substring(button3Str.length() - 1);

    JsonObject b4 = doc["b4"].to<JsonObject>();
    b4["value"] = button4Str.substring(0, button4Str.length() - 1).toInt();
    b4["unit"] = button4Str.substring(button4Str.length() - 1);
}

void populateStandardTimingConfig(JsonDocument &doc) {
    switch (buttonTimingConfig) {
    case TIMERCONFIG_5M:
        doc["tConf"] = "5M";
        break;
    case TIMERCONFIG_15M:
        doc["tConf"] = "15M";
        break;
    case TIMERCONFIG_1H:
        doc["tConf"] = "1H";
        break;
    case TIMERCONFIG_4H:
        doc["tConf"] = "4H";
        break;
    default:
        doc["tConf"] = "5M";
        break;
    }
}

String getDeviceTimingConfig() {
    JsonDocument doc;
    if (buttonTimingConfig == TIMERCONFIG_CUSTOM) {
        populateCustomTimingConfig(doc);
    } else {
        populateStandardTimingConfig(doc);
    }

    String jsonStr;
    serializeJson(doc, jsonStr);
    return jsonStr;
}