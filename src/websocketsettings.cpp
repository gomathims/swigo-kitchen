#include "websocketsettings.h"
#include "buttonpress.h"
#include "vsmr_link.h"

// Defined in api.cpp. Declared locally rather than by including api.h, which
// includes this module's own header.
bool isFirmwareUploadActive();

RTC_DATA_ATTR SessionData rtcSessionData;

const unsigned long sessionTimeout = MAX_SESSION_TIMEOUT * 60 * 1000; // In milliseconds

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;

    // Serial.print(millis());
    // Serial.println(" : WS message: ");
    // Serial.println((char*)data);
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        JsonDocument json;
        // WS frame data is not null-terminated - bound the parse by len
        DeserializationError error = deserializeJson(json, (char *)data, len);
        if (error) {
            // Serial.print(F("deserializeJson() failed: "));
            // Serial.println(error.f_str());
        } else {
            if (relayState == 1 && json["sControl"] == "OFF") {
                // Serial.println("offTriggerFromInterface=1");
                offTriggerFromInterface = 1;
                return;
            }
            if (relayState == 0 && json["sControl"] == "ON") {
                uint8_t hours = json["hours"];
                uint8_t minutes = json["minutes"];
                // String strrestartOption = json["restartOption"];
                // PowerResumeMode restartOption =
                // json["restartOption"]=="yes"?PowerResumeMode::YES:PowerResumeMode::NO;
                if (hours * 60 + minutes < MAX_RUNTIME_HOURS_WEB * 60) {
                    // DateTime currentTime = getDateTimeFromRTC();
                    TimeSpan duration(hours * 3600 + minutes * 60);

                    // webOffTime = currentTime + duration;
                    //Serial.println("onTriggerFromInterface=1");
                    onTriggerFromInterface = 1;
                    onTriggerDurationFromInterface = duration.totalseconds();

                    // storeDateTimeToEEPROM(currentTime, TEMP_SCHEDULE_START_TIME_ADDRESS);
                    // storeDateTimeToEEPROM(webOffTime, TEMP_SCHEDULE_END_TIME_ADDRESS);
                    // EEPROM.write(TEMP_SCHEDULE_RESTART_OPTION_ADDRESS, restartOption);

                    scheduleChanged = true;
                }
            }
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data,
             size_t len) {
    switch (type) {
    case WS_EVT_CONNECT:
        // ESPAsyncWebServer defaults to closeWhenFull=true: once this client's
        // outbound queue reaches WS_MAX_QUEUED_MESSAGES (32) the library DROPS
        // THE CONNECTION. That triggers whenever the async task cannot drain for
        // a while - a blocking call in a request handler, a long flash erase, a
        // slow client - and is why the UI kept losing its socket and going stale.
        // Everything sent here is disposable state telemetry (the next message
        // carries the full current state), so discarding a message costs nothing
        // while losing the socket costs the user their live view.
        client->setCloseClientOnQueueFull(false);

        // Serial.printf("WebSocket client #%u connected from %s\nCount: %d", client->id(),
        // client->remoteIP().toString().c_str(), String(ws.count()).c_str());
        notifyClients();
        break;
    case WS_EVT_DISCONNECT:
        // Serial.printf("WebSocket client #%u disconnected\n", client->id());

        break;
    case WS_EVT_DATA:
        if (len == 4 && strncmp((char *)data, "ping", 4) == 0) {
            // Serial.println("Custom ping received, sending pong...");
            client->text("pong");
        } else
            handleWebSocketMessage(arg, data, len);
        break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
        break;
    }
}

void initWebSocket() {
    ws.onEvent(onEvent); // per-client queue policy is set in WS_EVT_CONNECT
    server.addHandler(&ws);
}

void initializeSessions() {
    // Check if this is the first boot or RTC memory was reset
    if (rtcSessionData.magicNumber != RTC_MAGIC_NUMBER) {
        // First boot or RTC memory reset - initialize everything
        rtcSessionData.magicNumber = RTC_MAGIC_NUMBER;
        for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
            rtcSessionData.sessions[i].active = false;
            rtcSessionData.sessions[i].lastActivityTime = 0;
            rtcSessionData.sessions[i].token[0] = '\0';
        }
    }
}

uint8_t getFaultResetOption(DateTime scheduleStartTime, DateTime scheduleEndTime, char faultType) {
    // Serial.println("inside getFaultResetOption");
    // An untrusted boot time counts as a restart inside the window. It sits at 2000-01-01,
    // which would otherwise read as "booted long before this schedule" and clear andValue's
    // restart guard - promising a resume that handleSwitching() will refuse, since it now
    // requires isRestartEnabled whenever the boot instant cannot be believed.
    bool isEspRestarted =
        !espStartTimeTrusted || (espStartTime >= scheduleStartTime && espStartTime < scheduleEndTime);
    // Serial.println(isEspRestarted);
    uint8_t andValue = 0;
    if (!isEspRestarted || isRestartEnabled) {
        andValue = 1;
    }
    if (faultType == 'C') {
        if (scheduleStartTime < lastDryRunTime && scheduleEndTime > lastDryRunTime) {
            // Serial.println(restartOptions >>1 & 0x01 & andValue);
            // Serial.println((restartOptions >>1 & 0x01) & andValue);
            return restartOptions >> 1 & 0x01 & andValue;
        } else if (scheduleStartTime < lastOverRunTime && scheduleEndTime > lastOverRunTime) {
            return restartOptions >> 2 & 0x01 & andValue;
        } else {
            return 0;
        }
    } else {
        if (scheduleStartTime < lastLowVoltageTime && scheduleEndTime > lastLowVoltageTime) {
            return restartOptions >> 3 & 0x01 & andValue;
        } else if (scheduleStartTime < lastHighVoltageTime && scheduleEndTime > lastHighVoltageTime) {
            return restartOptions >> 4 & 0x01 & andValue;
        } else if (scheduleStartTime < lastPhaseFailureTime && scheduleEndTime > lastPhaseFailureTime) {
            return restartOptions >> 3 & 0x01 & andValue;
        } else if (scheduleStartTime < lastPhaseSequenceReverseTime && scheduleEndTime > lastPhaseSequenceReverseTime) {
            return restartOptions >> 3 & 0x01 & andValue;
        } else {
            return 0;
        }
    }
}
// The one place that decides what the schedule status is. notifyClients() turns
// the code into the websocket fields the portal reads, and the MQTT publish sends
// the same code to the cloud widget, so the two UIs cannot drift apart.
// Returns "" when there is nothing to report, "NEXT_SCHEDULE" when the only thing
// to show is the upcoming window, "DURATION_OVER" when a Duration Based window
// has already delivered its owed time, or one of the SCHTIME_* interruption codes.
const char *getScheduleStatusCode(DateTime curDateTime) {
    bool manualInterruption = false;
    bool restartOccurred = false;
    // if (nextEvent.startTime < lastManualOffTime && nextEvent.endTime > lastManualOffTime) {
    if (nextEvent.schStartTime < lastManualOffTime && nextEvent.schEndTime > lastManualOffTime) {
        manualInterruption = true;
    }
    // Untrusted boot time => assume we restarted inside the window, so the
    // RESUME_YES / RESUME_NO banners still reflect isRestartEnabled. Reading
    // the 2000-01-01 placeholder literally suppresses both of them.
    if (!espStartTimeTrusted || espStartTime > nextEvent.schStartTime) {
        restartOccurred = true;
    }

    uint8_t voltageFaultOccuredAndRestartOption =
        checkFaultOccuredAndRestartOption(curDateTime, nextEvent.schStartTime, nextEvent.schEndTime, 'V');
    uint8_t currentFaultOccuredAndRestartOption =
        checkFaultOccuredAndRestartOption(curDateTime, nextEvent.schStartTime, nextEvent.schEndTime, 'C');
    uint8_t voltageAlertState = lowVoltageAlertState || highVoltageAlertState || phaseFailureAlertState
                                || phaseSequenceReverseAlertState;
    if (scheduledTimeOuter && manualInterruption == true && isFloatEnabled != 2) {
        if (!restartOccurred || (restartOccurred && isRestartEnabled)) {
            return "SCHTIME_MANUAL_OFF_RESUME_YES_NOQUICKLINK";
        }
        return "SCHTIME_RESUME_NO";
    } else if (Flow_stopped_manual) {
        return "FLOW_TIME_MANUAL_INTERRUPTION";
    } else if (scheduledTimeOuter && manualOff == 3) {
        return "SCHTIME_FLOAT_OFF";
    } else if (scheduledTimeOuter && currentFaultOccuredAndRestartOption == 2) { // DryRun or Overload alert
        return "SCHTIME_FAULT_RESUME_YES";
    } else if (scheduledTimeOuter && currentFaultOccuredAndRestartOption == 1) { // DryRun or Overload alert
        return "SCHTIME_FAULT_RESUME_NO";
    } else if (scheduledTimeOuter && voltageAlertState && voltageFaultOccuredAndRestartOption == 2) {
        return "SCHTIME_FAULT_RESUME_YES_NOQUICKLINK";
    } else if (scheduledTimeOuter && voltageAlertState && voltageFaultOccuredAndRestartOption == 1) {
        return "SCHTIME_FAULT_RESUME_NO_NOQUICKLINK";
    } else if (scheduledTimeOuter && !voltageAlertState && voltageFaultOccuredAndRestartOption == 2) {
        // LOW/HIGH voltage or Phase problem
        return "SCHTIME_FAULT_RESUME_SOON";
    } else if (scheduledTimeOuter && !voltageAlertState && voltageFaultOccuredAndRestartOption == 1) {
        return "SCHTIME_FAULT_RESUME_NO";
    } else if (scheduledTimeOuter && restartOccurred && !isRestartEnabled) {
        // In case of cyclic timer, if restart is not enabled
        return "SCHTIME_RESUME_NO";
    } else if (scheduledTime && manualOff == 5 && maxRunTimeMins > 0) {
        // If maxRunTimeMins is reached, this will be shown
        return "SCHTIME_MAXTIME_BREAK";
    } else if (scheduledTimeOuter && nextEvent.scheduleType == ScheduleType::DURATION
               && nextEvent.remainingDuration <= 0 && isFloatEnabled != 2) {
        // This has to be reported before the restart branch below: a power cut
        // inside such a window would otherwise read as SCHTIME_RESUME_YES
        // ("switching will be retried"), promising a restart that will never
        // come - there is nothing left to run.
        return "DURATION_OVER";
    } else if (scheduledTime && restartOccurred && isRestartEnabled) {
        // In case of cyclic timer, only during ON duration, if system is waiting for restart delay duration this will be shown
        return "SCHTIME_RESUME_YES";
    } else if (isScheduleSet && (nextEvent.startTime != DateTime(0, 0, 0, 0, 0, 0)) && isFloatEnabled != 2) {
        return "NEXT_SCHEDULE";
    }
    return "";
}

uint8_t checkFaultOccuredAndRestartOption(DateTime curDateTime, DateTime scheduleStartTime, DateTime scheduleEndTime, char faultType) {
    // Untrusted boot time => assume the restart happened inside the window (see getFaultResetOption).
    bool isEspRestarted =
        !espStartTimeTrusted || (espStartTime >= scheduleStartTime && espStartTime < scheduleEndTime);
    // Serial.println(isEspRestarted);
    uint8_t andValue = 0;
    if (!isEspRestarted || isRestartEnabled) {
        andValue = 1;
    }
    uint8_t falueRestartEnabled =0;
    bool faultFound = false; 
    if (faultType == 'V') {
        if (scheduleStartTime < lastLowVoltageTime && scheduleEndTime > lastLowVoltageTime) {
            faultFound = true; 
            falueRestartEnabled = restartOptions >> 3 & 0x01 & andValue;
            if (falueRestartEnabled && (curDateTime > lastLowVoltageTime + lowVoltageRetryMinutes * 60))
                faultFound = false;
        } else if (scheduleStartTime < lastHighVoltageTime && scheduleEndTime > lastHighVoltageTime) {
            faultFound = true;
            falueRestartEnabled= restartOptions >> 4 & 0x01 & andValue;
            if (falueRestartEnabled && (curDateTime > lastHighVoltageTime + highVoltageRetryMinutes * 60))
                faultFound = false;
        } else if (scheduleStartTime < lastPhaseFailureTime && scheduleEndTime > lastPhaseFailureTime) {
            faultFound = true;
            falueRestartEnabled= restartOptions >> 3 & 0x01 & andValue;
            // Phase faults follow the low voltage retry setting - clear the fault
            // once that has elapsed, exactly as the two branches above do, so the
            // UI stops reporting a block the restart gate would no longer apply.
            if (falueRestartEnabled && (curDateTime > lastPhaseFailureTime + lowVoltageRetryMinutes * 60))
                faultFound = false;
        } else if (scheduleStartTime < lastPhaseSequenceReverseTime && scheduleEndTime > lastPhaseSequenceReverseTime) {
            faultFound = true;
            falueRestartEnabled= restartOptions >> 3 & 0x01 & andValue;
            if (falueRestartEnabled && (curDateTime > lastPhaseSequenceReverseTime + lowVoltageRetryMinutes * 60))
                faultFound = false;
        }
        if (!faultFound) 
            return 0;
        else if (faultFound && falueRestartEnabled == 0) 
            return 1;
        else 
            return 2;

    } else if (faultType == 'C') {
        if (manualOff == 6)
            return 1;
        if (scheduleStartTime < lastDryRunTime && scheduleEndTime > lastDryRunTime) {
            faultFound = true;
            falueRestartEnabled = restartOptions >> 1 & 0x01 & andValue;
            if (falueRestartEnabled && (curDateTime > lastDryRunTime + dryRunRetryMinutes * 60))
                faultFound = false;
        } 
        if (scheduleStartTime < lastOverRunTime && scheduleEndTime > lastOverRunTime && !faultFound) {
            faultFound = true;
            falueRestartEnabled = restartOptions >> 2 & 0x01 & andValue;
            if (falueRestartEnabled && (curDateTime > lastOverRunTime + overRunRetryMinutes * 60))
                faultFound = false;
        } 
        // else {
        //     faultFound = 0;
        // }
        if (!faultFound) 
            return 0;
        else if (faultFound && falueRestartEnabled == 0) 
            return 1;
        else 
            return 2;
    }
    return 0;

}

// Rate limiting for notifyClients(). A single relay event fans out to several
// notifyClients() calls (nine call sites in relay_led_handling.cpp), and each one
// queues a frame per connected client. That burst is what fills the 32-deep
// per-client queue. Coalescing is lossless here because the payload below is
// rebuilt from the current globals on every send - a suppressed call is simply
// superseded by the next one, which carries the same or fresher state.
// The first call of a burst still goes out immediately; only the followers wait.
#define NOTIFY_MIN_INTERVAL_MS 250
static uint32_t lastNotifySentMs = 0;
static bool notifyPending = false;
static bool notifyPendingShowSchedule = true;

static void sendClientsNow(bool showScheduleStatus) {
    // Stand down while a firmware upload is streaming in. These frames go out
    // up to four times a second, and each one builds a JsonDocument, takes
    // rtcMutex for getDateTimeFromRTC(), and queues a frame per client on the
    // same AsyncTCP task that is blocking inside Update.write() on 4 KB flash
    // erase/write. Suppressing them is lossless: the payload is rebuilt from
    // the current globals on every send, so the first frame after the upload
    // carries the live state anyway (and a successful upload reboots).
    //
    // Both callers have already cleared notifyPending and stamped
    // lastNotifySentMs by the time they get here, so nothing accumulates or
    // re-fires while this is suppressed.
    if (isFirmwareUploadActive()) return;
    // Serial.println(relayState?"sending ON":"sending OFF");
    if (ws.count() > 0) {
        JsonDocument json;
        json["sstat"] = (relayState ? "ON" : "OFF");
        json["cval"] = round(currentToOutput * 1000.0) / 1000.0;
        json["vval"] = round(voltageToOutput * 10.0) / 10.0;
        json["power"] = round(powerToOutput * 100.0) / 100.0;
        // json["pf"] = round(pf * 100.0) / 100.0;
        json["energy"] = round(energyToOutput * 100.0) / 100.0;
        json["cumEnergy"] = round((cumulativeEnergy + energyToOutput) * 100.0) / 100.0;
        DateTime curDateTime = getDateTimeFromRTC();
        String currentTime = formatDateTime(curDateTime);
        json["stime"] = currentTime;
        json["wMode"] = ethernetConnected? "E" : String(getWiFiMode());
        if (WiFi.status() == WL_CONNECTED) {
            json["wName"] = ssidBuffer;
        } else {
            json["wName"] = String(getSwitchName()) + "_AP";
        }
        if (!isAPPassSet){
            json["apPassSet"] = isAPPassSet;   
        }
        if (rtcBatteryLow) {
            json["rtcBattLow"] = rtcBatteryLow; 
        }
        json["mqttCon"] =  getMQTTConnectionStatus();
        if (vsmrLinkEnabled()) {
            float vsmrR, vsmrY, vsmrB;
            uint8_t vsmrFlags;
            bool vsmrRelay;
            if (vsmrLinkGetVoltages(vsmrR, vsmrY, vsmrB, vsmrFlags, vsmrRelay)) {
                json["vsmrR"] = vsmrR;
                json["vsmrY"] = vsmrY;
                json["vsmrB"] = vsmrB;
                json["vsmrRelay"] = vsmrRelay;
                if (vsmrFlags) {
                    json["vsmrFlags"] = vsmrFlags;
                }
            }
        }
        if (lowVoltageAlertState) {
            json["lowVoltageAlertState"] = lowVoltageAlertState;
        }
        if (highVoltageAlertState) {
            json["highVoltageAlertState"] = highVoltageAlertState;
        }
        if (phaseSequenceReverseAlertState) {
            json["phaseSequenceReverseAlertState"] = phaseSequenceReverseAlertState;
        }
        if (phaseFailureAlertState) {
            json["phaseFailureAlertState"] = phaseFailureAlertState;
        }
        if (leakageAlertState) {
            json["leakageAlertState"] = true;
        }
        if (masterSlaveMode == 2) {
            json["remoteMode"] = true;
            json["remoteStatus"] = (masterSwitchOn || masterSwitchOnStatusMqtt) ? "ON" : "OFF";
        }
        if (floatingAlertState) {
            json["floatingAlertState"] = floatingAlertState;
        }
        if (flowIncomingState) {
            json["flowIncomingState"] = flowIncomingState;
        }
        // Include log refresh flag if set (when remote command was sent or failure was logged)
        if (logRefreshRequired) {
            json["logRefresh"] = true;
            logRefreshRequired = false;  // Reset flag after sending
        }
        if (flow_start_time > 0 && !flowIncomingState && (millis() - flow_start_time) <= flowDetectionDelaySec * 1000 && isFloatEnabled==2) {
            long flowOnDuration = (millis() - flow_start_time) / 1000;
            json["flowOnDuration"] = flowOnDuration;
        }
        if (flow_stop_time > 0 && flowIncomingState && (millis() - flow_stop_time) <= flowStopDelaySec * 1000 && isFloatEnabled==2) {
            long flowOffDuration = (millis() - flow_stop_time) / 1000;
            json["flowOffDuration"] = flowOffDuration;
        }
        if (floatLowStartTime > 0 && (!floatingAlertState) && isFloatEnabled==1) {
            long floatDuration = (millis() - floatLowStartTime) / 1000;
            json["floatDuration"] = floatDuration;
        }
        // json["rawenergy"] = energy;
        // json["rawpower"] = power;
        // json["rawEnergyOnStart"] = energyOnStart;
        if (getWiFiMode() == 'W') {
            int wiFiStrength = WiFi.RSSI();
            // Serial.println(relayState?"sending ON":"sending OFF");
            json["wiFiStrength"] = wiFiStrength;
        }
        if (relayState) {
            // json["Flow_Detection"] = flowIncomingState;
            // runType: S/C/R => show "Current Schedule"; a manual/button/web start
            // that interrupted a schedule => hide it (short run wouldn't match the
            // schedule window).
            json["runType"] = String(runType);
            if (calculatedScheduledOffTime != DateTime(0, 0, 0, 0, 0, 0)) {
                String offTime = formatDateTime(calculatedScheduledOffTime);
                json["offTime"] = offTime;
                json["offDuration"] = formatDuration(calculatedScheduledOffTime - curDateTime);
            }

            if (current) {
                if (dryRunStartTime > 0) {
                    long dryRunDuration = (millis() - dryRunStartTime) / 1000;
                    json["dryRunDuration"] = dryRunDuration;
                }
                if (overRunStartTime > 0) {
                    long overRunDuration = (millis() - overRunStartTime) / 1000;
                    json["overRunDuration"] = overRunDuration;
                }
            }
            if (lowVoltageStartTime > 0) {
                long lowVoltageDuration = (millis() - lowVoltageStartTime) / 1000;
                json["lowVoltageDuration"] = lowVoltageDuration;
            }
            if (highVoltageStartTime > 0) {
                long highVoltageDuration = (millis() - highVoltageStartTime) / 1000;
                json["highVoltageDuration"] = highVoltageDuration;
            }
            

        } else {
           
            if (showScheduleStatus) {
                const char *schCode = getScheduleStatusCode(curDateTime);
                if (strcmp(schCode, "DURATION_OVER") == 0) {
                    // Duration Based, still inside the window, but the owed ON time has
                    // already been delivered.
                    json["upSchST"] = formatDateTime(nextEvent.startTime);
                    json["upSchET"] = formatDateTime(nextEvent.endTime);
                    json["upSchType"] = "Duration";
                    json["upSchDur"] = nextEvent.totalDuration;
                    json["upSchDurOver"] = 1;
                } else if (strcmp(schCode, "NEXT_SCHEDULE") == 0) {
                    // In case of cyclic timer, during OFF duration, even though system is waiting for restart delay duration this will be shown,
                    // as anyhow this is waiting for OFF duration
                    json["upSchST"] = formatDateTime(nextEvent.startTime);
                    json["upSchET"] = formatDateTime(nextEvent.endTime);
                    if (nextEvent.scheduleType==ScheduleType::CYCLIC)
                         json["upSchType"] = "Cyclic";
                    else if (nextEvent.scheduleType == ScheduleType::DURATION) {
                        json["upSchType"] = "Duration";
                        // The owed ON time, in seconds. The window shown is only the
                        // period the run may fall in, so the UI spells the duration
                        // out ("Duration-10mins only") instead of letting the window
                        // read as the run length.
                        json["upSchDur"] = nextEvent.totalDuration;
                    }
                } else if (schCode[0] != '\0') {
                    json["schStatus"] = schCode;
                }
            }
        }
        String output;
        serializeJson(json, output);
        // Serial.print(millis());
        // Serial.println(" : sending WS message "  );
        // Serial.println(output);
        ws.textAll(output);
    } else {
        // Serial.println("No ws clients connected");
    }
}

void notifyClients(bool showScheduleStatus) {
    uint32_t now = millis();
    if (lastNotifySentMs != 0 && (now - lastNotifySentMs) < NOTIFY_MIN_INTERVAL_MS) {
        // Too soon - remember that an update is owed and let flushPendingNotify()
        // send one covering frame once the window has passed.
        notifyPending = true;
        notifyPendingShowSchedule = showScheduleStatus;
        return;
    }
    lastNotifySentMs = now;
    notifyPending = false;
    sendClientsNow(showScheduleStatus);
}

// Called once per loop() pass - emits the single frame owed by any calls that
// were coalesced above.
void flushPendingNotify() {
    if (!notifyPending) return;
    uint32_t now = millis();
    if ((now - lastNotifySentMs) < NOTIFY_MIN_INTERVAL_MS) return;
    notifyPending = false;
    lastNotifySentMs = now;
    sendClientsNow(notifyPendingShowSchedule);
}

void cleanUpSessions() {
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (rtcSessionData.sessions[i].active
            && (millis() - rtcSessionData.sessions[i].lastActivityTime > sessionTimeout)) {
            rtcSessionData.sessions[i].active = false;
        }
    }
}

void generateSessionToken(char *tokenBuffer) {
    const char hexChars[] = "0123456789abcdef";
    for (int i = 0; i < MAX_TOKEN_LENGTH - 1; i++) {
        tokenBuffer[i] = hexChars[random(0, 16)];
    }
    tokenBuffer[MAX_TOKEN_LENGTH - 1] = '\0'; // Null terminate the string
}

Session *findSessionByToken(String token) {
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (rtcSessionData.sessions[i].active
            && strcmp(rtcSessionData.sessions[i].token, token.c_str()) == 0) {
            return &rtcSessionData.sessions[i];
        }
    }
    return nullptr;
}

bool startNewSession(String &token) {
    // Serial.println("Current sessions:");
    uint8_t oldestSession = 0;
    unsigned long oldestSessionTime = millis();
    // for (uint8_t i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
    //   Serial.print("Session ");
    //   Serial.print(i);
    //   Serial.print(": active=");
    //   Serial.print(sessions[i].active);
    //   Serial.print(", token=");
    //   Serial.print(sessions[i].token);
    //   Serial.print(", lastActivityTime=");
    //   Serial.println(sessions[i].lastActivityTime);
    // }
    for (uint8_t i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (!rtcSessionData.sessions[i].active) {
            char newToken[MAX_TOKEN_LENGTH];
            generateSessionToken(newToken);
            strlcpy(rtcSessionData.sessions[i].token, newToken, MAX_TOKEN_LENGTH);
            rtcSessionData.sessions[i].lastActivityTime = millis();
            rtcSessionData.sessions[i].active = true;
            token = rtcSessionData.sessions[i].token;
            // Serial.println("GENERATED sessions:");
            // Serial.println(rtcSessionData.sessions[i].token);
            return true;
        } else if (rtcSessionData.sessions[i].lastActivityTime < oldestSessionTime) {
            oldestSession = i;
            oldestSessionTime = rtcSessionData.sessions[i].lastActivityTime;
        }
    }
    // All session slots are occupied, but some are older than the others
    // sessions[oldestSession].token = generateSessionToken();
    char newToken[MAX_TOKEN_LENGTH];
    generateSessionToken(newToken);
    strlcpy(rtcSessionData.sessions[oldestSession].token, newToken, MAX_TOKEN_LENGTH);
    rtcSessionData.sessions[oldestSession].lastActivityTime = millis();
    rtcSessionData.sessions[oldestSession].active = true;
    token = rtcSessionData.sessions[oldestSession].token;
    Serial.println("REUSING sessions:");
    Serial.println(rtcSessionData.sessions[oldestSession].token);
    return true;
}