#ifndef BUTTONTIME_H
#define BUTTONTIME_H

enum TimerConfig : uint8_t {
  TIMERCONFIG_5M = 0,
  TIMERCONFIG_15M = 1,
  TIMERCONFIG_1H = 3,
  TIMERCONFIG_4H = 4,
  TIMERCONFIG_CUSTOM = 5
};

extern TimerConfig buttonTimingConfig;
extern unsigned long button1Duration;
extern unsigned long button2Duration;
extern unsigned long button3Duration;
extern unsigned long button4Duration;

#endif