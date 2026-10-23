#include "timesettings.h"
#include <Wire.h>
#include <string.h>

// Constants
const char *ntpServer = "pool.ntp.org"; // NTP server to check
const int ntpPort = 123;                // NTP uses UDP port 123
const long utcOffsetInSeconds = 5.5 * 3600;

// Global variables
WiFiUDP udp;
NTPClient timeClient(udp, ntpServer, utcOffsetInSeconds, 60000); // Update every 60 seconds
TimeSchedule gScheduleArray[MAX_SCHEDULE_OBJECTS];
uint8_t gScheduleCount = 0;

// Cached RTC reading (see getDateTimeFromRTC below), shared across tasks - every
// access is made under rtcMutex, which measures ~2-3us uncontended against the
// ~1.3ms I2C read it avoids. Declared here so the clock-write sites above
// getDateTimeFromRTC can drop the cache too.
static DateTime rtcCacheTime;
static uint32_t rtcCacheAtMs = 0;
static bool rtcCacheValid = false;

void print_time(DateTime dt) {
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(),
                  dt.second());
}

// Called after the RTC has been set from a trusted source (NTP, or a portal login
// on internet-less devices). rtc.adjust() has just cleared OSF, so if the one-time
// baseline is still pending we can complete it now: any OSF seen from here on is a
// genuine backup-battery event, not the historical flag left set by the old driver.
// It only advances the baseline - it never clears the battery alarm (a valid time
// set does not prove the cell is healthy; only surviving a real power-off does).
void onRtcTimeValidated() {
    if (!rtcOsfBaselined) {
        rtcOsfBaselined = true;
        saveRtcHealth();
        logDebugAction(DebugAction::AUTO_RTC_ADJUST, (char *)"RTC OSF baseline completed after first valid time set");
    }
}

// Call after every rtc.adjust() that moves the clock, passing the RTC time read just
// before the write and the time written.
//
// espStartTime is latched once, from the RTC, at the end of setup(). At that moment the
// clock may not yet be synced - on a device with a weak backup cell it reads hours (or
// years) behind - and the sync that corrects it lands later, from handleRTCAdjust() as
// soon as WiFi is up. That leaves espStartTime expressed in the OLD timescale while
// every comparison against it uses the new one, and two schedule decisions read wrong:
//
//   main.cpp "curDateTime >= espStartTime + startDelayMins" - the post-powercut resume
//       delay, which a backdated espStartTime makes already-elapsed, so the relay comes
//       back with no delay at all; and
//   main.cpp "espStartTime < nextEvent.schStartTime || isRestartEnabled" - an
//       espStartTime stuck behind the window start satisfies the first term on its own,
//       so the device resumes even when restart-after-powercut is switched OFF.
//
// Shifting espStartTime by the same correction keeps it meaning "the instant this device
// booted" in whatever timescale is current. nextEvent was built from the same stale
// clock, so it is rebuilt too - handleSwitching() does that on scheduleChanged.
void rebaseEspStartTime(const DateTime &beforeAdjust, const DateTime &afterAdjust) {
    if (!espStartTimeLatched) {
        return; // boot latch not taken yet - setup() will read the corrected clock anyway
    }
    if (afterAdjust.unixtime() <= RTC_TIME_SANITY_EPOCH) {
        return; // the time just written is not real either - nothing to anchor to
    }

    DateTime oldStart = espStartTime;
    long adjustSec;

    if (!espStartTimeTrusted) {
        // The cell held nothing across the outage, so espStartTime is a placeholder near
        // 2000-01-01, not a boot instant - shifting it by the correction would only move
        // garbage. millis() does know how long we have been up, so with a real clock now
        // in hand the boot instant can be reconstructed outright instead.
        espStartTime = DateTime(afterAdjust.unixtime() - (uint32_t)(millis() / 1000UL));
        espStartTimeTrusted = true;
        adjustSec = (long)espStartTime.unixtime() - (long)oldStart.unixtime();
    } else {
        adjustSec = (long)afterAdjust.unixtime() - (long)beforeAdjust.unixtime();
        if (adjustSec > -2 && adjustSec < 2) {
            return; // routine drift correction - not worth rebuilding the schedule for
        }
        int64_t rebased = (int64_t)espStartTime.unixtime() + adjustSec;
        if (rebased < (int64_t)SECONDS_FROM_1970_TO_2000) {
            rebased = (int64_t)SECONDS_FROM_1970_TO_2000; // clamp: DateTime cannot hold pre-2000
        }
        espStartTime = DateTime((uint32_t)rebased);
    }
    scheduleChanged = true;

    // char logMessage[200];
    // String oldStr = formatDateTime(oldStart);
    // String newStr = formatDateTime(espStartTime);
    // snprintf(logMessage, sizeof(logMessage), "Boot time rebased by %lds: %s -> %s", adjustSec, oldStr.c_str(),
    //          newStr.c_str());
    // logDebugAction(DebugAction::AUTO_RTC_ADJUST, logMessage);
}
// bool setRTCFromNTP() {
//     esp_task_wdt_reset();
//     if (isInternetAvailable()) {
//         Serial.println("Checking internet - available");
//         delay(400);
//         timeClient.setTimeOffset(deviceTZOffset * 60); // Set the timezone offset in seconds
//         timeClient.update();
//         delay(100);
//         timeClient.update();
//         delay(50);
//         esp_task_wdt_reset();
//         unsigned long epochTime = timeClient.getEpochTime();
//         Serial.println ("epochTime");
//         Serial.println (epochTime);
//         if (epochTime > 1672531200) { // Valid epoch time
//             if (xSemaphoreTake(rtcMutex, portMAX_DELAY) == pdTRUE) {
//                 DateTime ntpTime = DateTime(epochTime);
//                 rtc.adjust(ntpTime);
//                 xSemaphoreGive(rtcMutex);
//             }
//             return true;
//         }
//     }
//     logDebugAction(DebugAction::TIME_ADJUSTMENT, (char *)"Failed to connect to time server");
//     return false;
// }
bool setRTCFromNTP() {
    esp_task_wdt_reset();

    // Get current RTC time before update to detect if NTP actually changed the time
    DateTime currentRTC = getDateTimeFromRTC();
    unsigned long currentEpoch = currentRTC.unixtime();
    Serial.println("Checking internet");
    if (!isInternetAvailable()) {
        Serial.println("Checking internet - not available");

        char logMessage[200];
        String currentTimeStr = formatDateTime(currentRTC);
        snprintf(logMessage, sizeof(logMessage), "Internet not available. RTC: %s", currentTimeStr.c_str());
        logDebugAction(DebugAction::AUTO_RTC_ADJUST, logMessage);
        return false;
    }
    Serial.println(" internet available");

    timeClient.setTimeOffset(deviceTZOffset * 60); // Set the timezone offset in seconds

    // Attempt NTP update - call twice to ensure we get a fresh response
    timeClient.update();
    delay(100);
    timeClient.update();
    delay(50);
    esp_task_wdt_reset();

    unsigned long epochTime = timeClient.getEpochTime();

    // Validate epoch time is reasonable (after Jan 1, 2023)
    if (epochTime <= 1672531200) {
        char logMessage[200];
        String currentTimeStr = formatDateTime(currentRTC);
        DateTime ntpTime = DateTime(epochTime);
        String ntpTimeStr = formatDateTime(ntpTime);
        snprintf(logMessage, sizeof(logMessage), "Invalid epoch time from NTP. RTC: %s, NTP: %s",
                 currentTimeStr.c_str(), ntpTimeStr.c_str());
        logDebugAction(DebugAction::AUTO_RTC_ADJUST, logMessage);
        return false;
    }

    // Additional validation: Check if NTP time is reasonable compared to current RTC
    // This helps detect stale/cached values when RTC is already set
    if (currentEpoch > 1672531200) { // RTC is already set and valid
        long timeDiff = (long)epochTime - (long)currentEpoch;
        // Record the NTP-vs-RTC delta for battery diagnostics (negative = RTC behind).
        // A healthy DS3231 is within a second; a large delta means the RTC lost time.
        rtcLastDriftSec = timeDiff;

        // Guard only against a stale/cached NTP response that would roll the clock
        // BACKWARDS. NTP being far behind a valid RTC is suspect (RTC doesn't gain
        // time on its own), and moving a scheduler's clock backward can re-fire
        // schedules, so reject it. We do NOT reject when NTP is ahead: a device that
        // was powered off (or has a weak RTC battery) legitimately drifts behind by
        // any amount, and correcting that drift is exactly what this sync is for.
        if (timeDiff < -86400) {
            char logMessage[200];
            String currentTimeStr = formatDateTime(currentRTC);
            DateTime ntpTime = DateTime(epochTime);
            String ntpTimeStr = formatDateTime(ntpTime);
            snprintf(logMessage, sizeof(logMessage),
                     "NTP time significantly behind RTC (likely stale). RTC: %s, NTP: %s", currentTimeStr.c_str(),
                     ntpTimeStr.c_str());
            logDebugAction(DebugAction::AUTO_RTC_ADJUST, logMessage);
            return false;
        }
    }

    // Absolute upper sanity bound, absolute upper ceiling (Jan 1 2100) check.
    if (epochTime > 4102444800UL) {
        char logMessage[200];
        String currentTimeStr = formatDateTime(currentRTC);
        DateTime ntpTime = DateTime(epochTime);
        String ntpTimeStr = formatDateTime(ntpTime);
        snprintf(logMessage, sizeof(logMessage), "NTP time beyond absolute ceiling. RTC: %s, NTP: %s",
                 currentTimeStr.c_str(), ntpTimeStr.c_str());
        logDebugAction(DebugAction::AUTO_RTC_ADJUST, logMessage);
        return false;
    }

    // Verify internet is still available after the update attempt
    // This helps catch cases where connection was lost during update
    if (!isInternetAvailable()) {
        char logMessage[200];
        String currentTimeStr = formatDateTime(currentRTC);
        DateTime ntpTime = DateTime(epochTime);
        String ntpTimeStr = formatDateTime(ntpTime);
        snprintf(logMessage, sizeof(logMessage), "Internet lost during time sync. RTC: %s, NTP: %s",
                 currentTimeStr.c_str(), ntpTimeStr.c_str());
        logDebugAction(DebugAction::AUTO_RTC_ADJUST, logMessage);
        return false;
    }

    // Update RTC with validated NTP time. Only the rtc.adjust() write touches the RTC
    // hardware, so the mutex is held for just that. All the logging below uses values
    // already captured (currentRTC / ntpTime) and never touches the RTC, so it runs
    // outside the lock to keep the critical section minimal.
    DateTime ntpTime = DateTime(epochTime);
    if (xSemaphoreTake(rtcMutex, portMAX_DELAY) != pdTRUE) {
        char logMessage[200];
        String currentTimeStr = formatDateTime(currentRTC);
        String ntpTimeStr = formatDateTime(ntpTime);
        snprintf(logMessage, sizeof(logMessage), "Failed to acquire RTC mutex. RTC: %s, NTP: %s",
                 currentTimeStr.c_str(), ntpTimeStr.c_str());
        logDebugAction(DebugAction::AUTO_RTC_ADJUST, logMessage);
        return false;
    }
    rtc.adjust(ntpTime);
    rtcCacheValid = false; // clock moved - drop the cached baseline (already holding rtcMutex)
    xSemaphoreGive(rtcMutex);

    // The clock just moved - keep espStartTime (and nextEvent) in the same timescale.
    rebaseEspStartTime(currentRTC, ntpTime);

    // Log a successful time adjustment at most once per calendar day. The routine
    // 6-hourly / midnight resyncs otherwise flood the debug log with near-zero
    // corrections; one entry a day is enough to confirm syncing is healthy.
    static uint8_t lastAdjustLogDay = 0xFF;
    if (ntpTime.day() != lastAdjustLogDay) {
        lastAdjustLogDay = ntpTime.day();
        char logMessage[200];
        String currentTimeStr = formatDateTime(currentRTC);
        String newTimeStr = formatDateTime(ntpTime);
        snprintf(logMessage, sizeof(logMessage), "RTC: %s -> Adjusted to: %s", currentTimeStr.c_str(),
                 newTimeStr.c_str());
        logDebugAction(DebugAction::AUTO_RTC_ADJUST, logMessage);
    }
    onRtcTimeValidated(); // OSF now cleared -> complete the one-time baseline
    return true;
}

String formatDateTime(const DateTime &dt) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(),
             dt.second());
    return String(buf);
}

#ifdef LOG_TIMING
// Temporary instrumentation - split of the last RTC read, so a slow
// getDateTimeFromRTC() can be attributed to (a) waiting for rtcMutex,
// (b) the I2C transaction itself, or (c) neither, which means the calling task
// was starved/stalled (e.g. an SPI-flash erase elsewhere disabling the cache).
volatile uint32_t rtcLastMutexWaitUs = 0;
volatile uint32_t rtcLastReadUs = 0;
#endif

void invalidateRtcCache() {
    if (xSemaphoreTake(rtcMutex, portMAX_DELAY) == pdTRUE) {
        rtcCacheValid = false;
        xSemaphoreGive(rtcMutex);
    }
}

DateTime getDateTimeFromRTC(bool returnImmediate) {
    // Serial.println("getDateTimeFromRTC called");
    DateTime tempDateTime = DateTime(0, 0, 0, 0, 0, 0);
    while (tempDateTime == DateTime(0, 0, 0, 0, 0, 0)) {
        // Serial.println(" RTC xSemaphoreTake before...");

        uint32_t nowMs = millis();
        if (xSemaphoreTake(rtcMutex, portMAX_DELAY) == pdTRUE) {
            // Serial.println(" RTC xSemaphoreTake after...");
            // Recent enough reading available - carry it forward by the whole
            // elapsed seconds instead of touching the chip. DateTime has no
            // sub-second resolution, so the truncation loses nothing.
            if (rtcCacheValid && (nowMs - rtcCacheAtMs) < RTC_CACHE_MAX_AGE_MS) {
                DateTime cached = rtcCacheTime + TimeSpan((int32_t)((nowMs - rtcCacheAtMs) / 1000));
                xSemaphoreGive(rtcMutex);
                return cached;
            }

            tempDateTime = rtc.now();

            if (tempDateTime.year() >= 2000) {
                rtcCacheTime = tempDateTime;
                rtcCacheAtMs = millis();
                rtcCacheValid = true;
            }
            xSemaphoreGive(rtcMutex);
        }
        if (tempDateTime.year() >= 2000) {
            return tempDateTime;
        }
        delay(50);
    }
}

void initiateTimeComponents() {
    esp_task_wdt_reset();
    timeClient.begin();
    timeClient.update();

    uint8_t rtc_trial = 0;
    while (!rtc.begin() && rtc_trial < 10) {
        Serial.println("Couldn't find RTC");
        delay(100);
        rtc_trial++;
    }

    if (!rtc.begin()) {
        Serial.println("Couldn't find RTC. Continuing without it...");
        return;
    }

    // A DS3231 keeps time on its backup cell whenever mains power is off. If the
    // oscillator-stop flag (OSF) is set, the oscillator stopped while powered off
    // - the backup cell is weak/dead/missing - so the RTC time is unreliable. This
    // is the "clock runs hours behind" case that used to go undetected.
    // Any trusted time-set (NTP, or a portal
    // login on internet-less devices) calls rtc.adjust(), which clears OSF and
    // completes the baseline via onRtcTimeValidated().
    if (rtc.lostPower()) {
        if (rtcOsfBaselined) {
            // Genuine oscillator stop after baseline: the backup cell did not hold.
            rtcLostPowerCount++;
            rtcBatteryLow = true;
            Serial.println("Inside  rtc.lostPower():");
            Serial.println(rtcBatteryLow);

            char logMessage[200];
            String rtcTimeStr = formatDateTime(getDateTimeFromRTC());
            snprintf(logMessage, sizeof(logMessage),
                     "RTC lost power (OSF set) - backup battery weak. Count: %d, RTC: %s", rtcLostPowerCount,
                     rtcTimeStr.c_str());
            logDebugAction(DebugAction::RTC_ERROR, logMessage);
            saveRtcHealth();
        } else {
            // First OSF on this firmware is historical (the old driver never cleared
            // it). Do not alarm. The baseline completes when the time is next validly
            // set - by NTP below if online, or at the portal login for internet-less
            // devices - because either path clears OSF and calls onRtcTimeValidated().
            Serial.println("RTC OSF historical - baseline pending until first valid time set");
            // logDebugAction(DebugAction::TIME_ADJUSTMENT,
            //                (char *)"RTC OSF historical - baseline pending until first time set");
        }
        // Correct the time + clear OSF now if online. Internet-less devices stay with
        // OSF set (and, once baselined, the alarm) until their next portal login sets
        // the time - which is exactly how those devices are expected to be synced.
        setRTCFromNTP();
    } else {
        // OSF is clear - but that only proves the cell is healthy if the oscillator was
        // actually exercised, i.e. this boot followed a REAL power-on reset (VCC dropped
        // and the cell had to hold time). On a clean reboot (software/OTA/watchdog) VCC
        // never fell, so OSF=0 says nothing about the battery and must not clear a
        // standing alarm. OSF re-latches on every genuine outage with a dead cell
        // (datasheet: "set any time the oscillator stops; remains 1 until written 0"),
        // so a power-on reset that still finds OSF=0 means the cell held -> it is good.
        bool powerCycled = (esp_reset_reason() == ESP_RST_POWERON);

        if (!rtcOsfBaselined) {
            rtcOsfBaselined = true;
            saveRtcHealth();
        }
        if (rtcBatteryLow && powerCycled) {
            // Cell was tested by a real outage and held the clock -> it has been replaced.
            rtcBatteryLow = false;
            saveRtcHealth();
            Serial.println("RTC held time through a power-on reset - clearing battery alarm");
        }
    }
}

String getTimeString() {
    DateTime dt = getDateTimeFromRTC();
    return formatDateTime(dt);
}

bool isInternetAvailable() {
    IPAddress google(8, 8, 8, 8); // Google's public DNS
    esp_task_wdt_reset();
    return Ping.ping(google, 3); // Ping 3 times
}

String formatDuration(TimeSpan duration) {
    long totalSeconds = duration.totalseconds();
    uint8_t hours = totalSeconds / 3600;
    uint8_t minutes = (totalSeconds % 3600) / 60;
    uint8_t seconds = totalSeconds % 60;
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, minutes, seconds);
    return String(buf);
}

// Compact API: s=schedule, t=type(D/W), e=enabled(1/0), d=days(0-6), "1"-"4"=start/end H,M, c=isCyclic(1/0),
// "5"-"8"=cyclic durations. m="D" marks a Duration Based schedule (details bit 5); in that case "5"/"6"
// are reused to carry the plain duration (onDurHrs/onDurMins) instead of the cyclic on-duration, and
// "7"/"8" (off-duration) are unused - isCyclic and Duration Based are mutually exclusive per schedule.
String getScheduleJsonString(TimeSchedule *scheduleArray) {
    JsonDocument doc;
    JsonArray scheduleArrayJson = doc["s"].to<JsonArray>();

    uint8_t numObjects = gScheduleCount;
    for (uint8_t i = 0; i < numObjects; i++) {
        JsonObject obj = scheduleArrayJson.add<JsonObject>();
        bool isWeekly = (scheduleArray[i].details & 0x03) != 0;
        obj["t"] = isWeekly ? "W" : "D";
        obj["e"] = ((scheduleArray[i].details >> 3) & 0x01);

        if (isWeekly) {
            JsonArray daysArray = obj["d"].to<JsonArray>();
            for (uint8_t j = 0; j < 7; j++) {
                if (scheduleArray[i].days & (1 << j)) {
                    daysArray.add(j);
                }
            }
        }

        obj["1"] = scheduleArray[i].startHour;
        obj["2"] = scheduleArray[i].startMinute;
        obj["3"] = scheduleArray[i].endHour;
        obj["4"] = scheduleArray[i].endMinute;

        bool isDurationBased = ((scheduleArray[i].details >> 5) & 0x01) == 1;
        if (isDurationBased) {
            obj["m"] = "D";
            obj["5"] = scheduleArray[i].onDurHrs;
            obj["6"] = scheduleArray[i].onDurMins;
        } else {
            obj["c"] = (uint8_t)((scheduleArray[i].details >> 4) & 0x01);
            obj["5"] = scheduleArray[i].onDurHrs;
            obj["6"] = scheduleArray[i].onDurMins;
            obj["7"] = scheduleArray[i].offDurHrs;
            obj["8"] = scheduleArray[i].offDurMins;
        }
    }

    String jsonString;
    serializeJson(doc, jsonString);
    return jsonString;
}

// Compact API: s=schedule, t,e,d, "1"-"4"=start/end H,M, c=isCyclic, "5"-"8"=onDurHrs,onDurMins,offDurHrs,offDurMins,
// m="D"+"5"/"6"=Duration Based (see getScheduleJsonString comment above)
void saveScheduleToEEPROM(JsonDocument doc) {
    JsonArray scheduleJsonArray = doc["s"];
    if (!scheduleJsonArray.isNull() && scheduleJsonArray.size() > 0) {
        uint8_t numObjects = scheduleJsonArray.size();
        for (uint8_t i = 0; i < numObjects; i++) {
            TimeSchedule timeSchedule;
            JsonObject obj = scheduleJsonArray[i];
            timeSchedule.startHour = obj["1"].as<uint8_t>();
            timeSchedule.startMinute = obj["2"].as<uint8_t>();
            timeSchedule.endHour = obj["3"].as<uint8_t>();
            timeSchedule.endMinute = obj["4"].as<uint8_t>();
            const char *typeStr = obj["t"].as<const char *>();
            if (typeStr == nullptr)
                typeStr = "D";
            bool isWeekly = (typeStr[0] == 'W' || typeStr[0] == 'w');
            const char *modeStr = obj["m"].as<const char *>();
            bool isDurationBased = (modeStr != nullptr) && (modeStr[0] == 'D' || modeStr[0] == 'd');
            timeSchedule.details = (isWeekly ? 1 : 0)
                                 | ((obj["e"].as<int>() ? 1 : 0) << 3)
                                 | ((!isDurationBased && obj["c"].as<int>() ? 1 : 0) << 4)
                                 | ((isDurationBased ? 1 : 0) << 5);
            if (isWeekly) {
                timeSchedule.days = 0;
                JsonArray daysJsonArray = obj["d"];
                for (uint8_t j = 0; j < daysJsonArray.size(); j++) {
                    int dayNum = daysJsonArray[j].as<int>();
                    if (dayNum >= 0 && dayNum <= 6)
                        timeSchedule.days |= 1 << dayNum;
                }
            }
            timeSchedule.onDurHrs = obj["5"].isNull() ? 0 : obj["5"].as<uint8_t>();
            timeSchedule.onDurMins = obj["6"].isNull() ? 0 : obj["6"].as<uint8_t>();
            timeSchedule.offDurHrs = (isDurationBased || obj["7"].isNull()) ? 0 : obj["7"].as<uint8_t>();
            timeSchedule.offDurMins = (isDurationBased || obj["8"].isNull()) ? 0 : obj["8"].as<uint8_t>();
            gScheduleArray[i] = timeSchedule;
        }
        gScheduleCount = numObjects;
        saveScheduleToPreferences(numObjects);
        savePreference((char *)"isScheduleSet", true);
        isScheduleSet = true;
    } else {
        saveScheduleToPreferences(0);
        savePreference((char *)"isScheduleSet", false);
        isScheduleSet = false;
    }
    scheduleChanged = true;
    // Update the schedule to cloud
    publishScheduleAttributeToCloud();
}

SwitchingEvent getNextSchedule(DateTime currentTime) {
    DateTime nextEventStartTime, nextEventEndTime = DateTime(0, 0, 0, 0, 0, 0);

    TimeSchedule *nextEventSchedule = nullptr;
    bool foundNextDaySchedule = false;
    PowerResumeMode nextEventRestartOption = PowerResumeMode::NO;
    ScheduleType nextEventScheduleType = ScheduleType::SCH_NONE;

    // isTempSchedule is a global variable, it cannot be given a default value before setting right value
    // isTempSchedule = 0;
    //  Serial.println("in getNextSchedule");

    // if (isRestartEnabled && tempEvent.startTime<=currentTime && tempEvent.endTime>currentTime){
    if (tempEvent.startTime <= currentTime && tempEvent.endTime > currentTime) {
        // Serial.println("in getNextSchedule - tempEvent");
        isTempSchedule = 1;
        return {tempEvent.startTime,   tempEvent.endTime,   nextEventRestartOption,
                nextEventScheduleType, 0, 0, tempEvent.startTime, tempEvent.endTime};
    } else {
        isTempSchedule = 0;
    }

    // If the RTC battery has died, a power restart leaves the clock at 2000-01-01
    // until a fresh sync (NTP/portal) lands. A stored schedule's hour/minute could
    // coincidentally match that bogus wall-clock time, so refuse to surface a
    // recurring schedule as "next" until the clock is trustworthy (year > 2023).
    if (!isScheduleSet || currentTime.unixtime() <= RTC_TIME_SANITY_EPOCH) {
        return {nextEventStartTime,    nextEventEndTime,   nextEventRestartOption,
                nextEventScheduleType, 0, 0, nextEventStartTime, nextEventEndTime};
    }
    // uint8_t numObjects = EEPROM.read(SCHEDULE_ADDRESS);
    uint8_t numObjects = gScheduleCount;

    // if (numObjects > MAX_SCHEDULE_OBJECTS) {
    //   numObjects = 0;
    // }
    // Serial.println("Get next schedule");

    for (uint8_t i = 0; i < numObjects; i++) {
        TimeSchedule &schedule = gScheduleArray[i];
        uint8_t scheduleType = schedule.details & 0x03;

        PowerResumeMode restartOption = static_cast<PowerResumeMode>(((schedule.details >> 2) & 0x01));
        bool enabled = ((schedule.details >> 3) & 0x01) == 1 ? true : false;
        if (!enabled) {
            continue;
        }
        if (scheduleType == 0) {
            // daily
            // get todays' start and end time- as per schedule
            DateTime startTime(currentTime.year(), currentTime.month(), currentTime.day(), schedule.startHour,
                               schedule.startMinute, 0);
            DateTime endTime(currentTime.year(), currentTime.month(), currentTime.day(), schedule.endHour,
                             schedule.endMinute, 0);

            // Adjust the days, if the schedule is to start in the evening and run to next day
            // TODO - To restest Bug testing time 9:32 sch time 9:31 to 9:05
            if (endTime < startTime && endTime > currentTime) {
                startTime = startTime - TimeSpan(1, 0, 0, 0);
            } else if (endTime < startTime && endTime <= currentTime) {
                // End time is on the next day
                endTime = endTime + TimeSpan(1, 0, 0, 0);
            }

            // If current day's schedule has already passed, need to consider next day's schedule time only
            if (startTime < currentTime && endTime <= currentTime) {
                startTime = startTime + TimeSpan(1, 0, 0, 0);
                endTime = endTime + TimeSpan(1, 0, 0, 0);
            }
            if (startTime > currentTime) {
                // If the daily scheduled time yet to start
                if (!nextEventSchedule || startTime < nextEventStartTime) {
                    nextEventStartTime = startTime;
                    nextEventEndTime = endTime;
                    nextEventRestartOption = restartOption;
                    nextEventScheduleType = ScheduleType::DAILY;
                    // nextEventSchedule = const_cast<TimeSchedule *>(&schedule);
                    nextEventSchedule = &gScheduleArray[i];
                }
            } else if (endTime > currentTime) {
                // If the daily scheduled time already started
                if (!nextEventSchedule || endTime < nextEventStartTime) {
                    nextEventStartTime = startTime;
                    nextEventEndTime = endTime;
                    nextEventRestartOption = restartOption;
                    nextEventScheduleType = ScheduleType::DAILY;
                    // nextEventSchedule = const_cast<TimeSchedule *>(&schedule);
                    nextEventSchedule = &gScheduleArray[i];
                }
            }

        } else if (scheduleType == 1) {
            for (uint8_t j = 0; j < 7; j++) {
                // For all seven days of week, it checks what is the next occurance day that matches
                // Serial.println(schedule.days);
                if (schedule.days & (1 << j)) {
                    int8_t daysUntilEvent = (j - currentTime.dayOfTheWeek() + 8) % 7;
                    if (daysUntilEvent == 6
                        && (currentTime.hour() < schedule.endHour
                            || (currentTime.hour() == schedule.endHour && currentTime.minute() < schedule.endMinute))) {
                        // If the event is today and the end time is yet to reach, but start time already started the
                        // previous day. Here as of current time the event is already started. So we need adjust the
                        // daysUntilEvent to -1
                        daysUntilEvent = -1;
                    }
                    DateTime eventDay = currentTime + TimeSpan(daysUntilEvent, 0, 0, 0);
                    DateTime testMinusDay = currentTime + TimeSpan(-1, 0, 0, 0);
                    testMinusDay = currentTime - TimeSpan(1, 0, 0, 0);
                    DateTime startTime(eventDay.year(), eventDay.month(), eventDay.day(), schedule.startHour,
                                       schedule.startMinute, 0);
                    DateTime endTime(eventDay.year(), eventDay.month(), eventDay.day(), schedule.endHour,
                                     schedule.endMinute, 0);
                    // Serial.print("inside weekly j - ");
                    // Serial.println(j);

                    if (endTime < startTime) {
                        // End time is on the next day
                        endTime = endTime + TimeSpan(1, 0, 0, 0);
                    }
                    if (startTime > currentTime || endTime > currentTime) {
                        if (!nextEventSchedule || startTime < nextEventStartTime) {
                            nextEventStartTime = startTime;
                            nextEventEndTime = endTime;
                            nextEventRestartOption = restartOption;
                            nextEventScheduleType = ScheduleType::WEEKLY;
                            // nextEventSchedule = const_cast<TimeSchedule *>(&schedule);
                            nextEventSchedule = &gScheduleArray[i];
                        }
                    }
                }
            }
        }
    }

    // After finding the next event window, check for cyclic/duration mode and return the next ON period if needed
    if (nextEventSchedule) {
        bool isCyclic = ((nextEventSchedule->details >> 4) & 0x01) == 1 ? true : false;
        bool isDurationBased = ((nextEventSchedule->details >> 5) & 0x01) == 1 ? true : false;
        if (isDurationBased) {
            // Duration Based: ON starts at the window start and runs for the stored duration
            // (reusing onDurHrs/onDurMins), clamped to the window's end time as a safety net.
            long durationSeconds = nextEventSchedule->onDurHrs * 3600L + nextEventSchedule->onDurMins * 60L;
            long elapsedSeconds = (long)getElapsedEnergyDurationInWindow(nextEventStartTime, currentTime);
            Serial.print("elapsedSeconds :");
            Serial.println(elapsedSeconds);
            long remainingSeconds = durationSeconds - elapsedSeconds;

            // The still-owed duration may not spill past the window: running it
            // from where the ON period can actually begin (now, or the window
            // start if the window hasn't opened yet) must not cross
            // nextEventEndTime, so cut it off at the window end.
            DateTime runFrom = (currentTime > nextEventStartTime) ? currentTime : nextEventStartTime;
            long windowRemainingSeconds = (long)(nextEventEndTime - runFrom).totalseconds();
            if (remainingSeconds > windowRemainingSeconds) {
                remainingSeconds = windowRemainingSeconds;
            }
            if (remainingSeconds < 0) {
                remainingSeconds = 0;
            }
            // DateTime onStart = nextEventStartTime;

            // DateTime onEnd = onStart + TimeSpan(durationSeconds);
            // if (onEnd > nextEventEndTime)
            //     onEnd = nextEventEndTime;

            // // Use the window's real end (nextEventEndTime), not the naive onEnd
            // // estimate, to decide whether this occurrence is still live - a
            // // power cut can push currentTime past onEnd while the window
            // // itself (and the still-owed duration) is very much still open.
            if (currentTime < nextEventEndTime) {
                return {nextEventStartTime,           nextEventEndTime, nextEventRestartOption, ScheduleType::DURATION, 
                        durationSeconds, remainingSeconds, nextEventStartTime, nextEventEndTime};
            } else {
                // The window itself has closed - look for the next occurrence
                return getNextSchedule(nextEventEndTime);
            }
        }
        if (isCyclic) {
            Serial.println("inside cyclic");

            long onDuration = nextEventSchedule->onDurHrs * 3600 + nextEventSchedule->onDurMins * 60;
            Serial.print("onDuration: ");
            Serial.println(onDuration);

            long offDuration = nextEventSchedule->offDurHrs * 3600 + nextEventSchedule->offDurMins * 60;
            Serial.print("offDuration: ");
            Serial.println(offDuration);

            long cycleLength = onDuration + offDuration;
            if (onDuration > 0 && cycleLength > 0) {
                // Calculate how many seconds have passed since the schedule start time
                long secondsSinceStart = (currentTime - nextEventStartTime).totalseconds();
                if (secondsSinceStart < 0) {
                    // Current time is before schedule start, return the first ON period
                    DateTime onStart = nextEventStartTime;
                    DateTime onEnd = onStart + TimeSpan(onDuration);
                    if (onEnd > nextEventEndTime)
                        onEnd = nextEventEndTime;
                    return {onStart,         onEnd, nextEventRestartOption, ScheduleType::CYCLIC, 0, 0,nextEventStartTime,
                            nextEventEndTime};
                }

                // Calculate which cycle we're in
                long cyclesPassed = secondsSinceStart / cycleLength;
                long secondsIntoCurrentCycle = secondsSinceStart % cycleLength;

                // Calculate the start of the current cycle
                DateTime currentCycleStart = nextEventStartTime + TimeSpan(cyclesPassed * cycleLength);

                // Calculate ON and OFF periods for current cycle using total seconds
                DateTime onStart = currentCycleStart;
                DateTime onEnd = onStart + TimeSpan(onDuration);
                DateTime offStart = onEnd;
                DateTime offEnd = offStart + TimeSpan(offDuration);

                // Ensure we don't exceed the schedule end time
                if (onEnd > nextEventEndTime)
                    onEnd = nextEventEndTime;
                if (offEnd > nextEventEndTime)
                    offEnd = nextEventEndTime;

                // Check if we're in an ON period
                if (currentTime >= onStart && currentTime < onEnd) {
                    // Serial.println("Currently in ON period");
                    return {onStart,         onEnd, nextEventRestartOption, ScheduleType::CYCLIC, 0, 0, nextEventStartTime,
                            nextEventEndTime};
                }
                // Check if we're in an OFF period
                else if (currentTime >= offStart && currentTime < offEnd) {
                    // Serial.println("Currently in OFF period, next ON will be after OFF");
                    // Calculate next ON period
                    DateTime nextOnStart = offEnd;
                    DateTime nextOnEnd = nextOnStart + TimeSpan(onDuration);
                    if (nextOnEnd > nextEventEndTime)
                        nextOnEnd = nextEventEndTime;

                    if (nextOnStart < nextEventEndTime) {
                        return {nextOnStart,          nextOnEnd,          nextEventRestartOption,
                                ScheduleType::CYCLIC, 0, 0, nextEventStartTime, nextEventEndTime};
                    } else {
                        // No more ON periods in this schedule window, look for next schedule
                        return getNextSchedule(nextEventEndTime);
                    }
                }
                // Current time is before the current cycle starts
                else if (currentTime < onStart) {
                    // Serial.println("Before current cycle, return current ON period");
                    return {onStart,         onEnd, nextEventRestartOption, ScheduleType::CYCLIC, 0, 0, nextEventStartTime,
                            nextEventEndTime};
                }
                // Current time is after the current cycle ends
                else {
                    // Serial.println("After current cycle, look for next schedule");
                    return getNextSchedule(nextEventEndTime);
                }
            }
        }
    }
    return {nextEventStartTime,    nextEventEndTime,   nextEventRestartOption,
            nextEventScheduleType, 0, 0, nextEventStartTime, nextEventEndTime};
}
