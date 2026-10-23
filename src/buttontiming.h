#ifndef BUTTONTIMING_H
#define BUTTONTIMING_H

#include <String.h>

#include <string>

#include "ArduinoJson.h"
#include "buttontime.h"
#include "preference_management.h"

// Function declarations
unsigned long getSecondsForRegularPlates(uint8_t buttonNo);
void initButtonTiming();
TimeSpan getDuration(uint8_t buttonNo);
bool saveDeviceTimingConfig(unsigned char *jsonStr);
void populateCustomTimingConfig(JsonDocument &doc);
void populateStandardTimingConfig(JsonDocument &doc);
String getDeviceTimingConfig();

#endif  // BUTTONTIMING_H