# Cyclic Timer Behavior Document

## Overview

A cyclic schedule operates within a time window (Start Time to End Time) and repeats ON/OFF cycles within that window.

**Example:**
```
Schedule: 21:00 to 22:00, Cyclic ON=10min, OFF=50min
→ Cycle 1: ON 21:00–21:10, OFF 21:10–22:00
→ (No Cycle 2 — next ON at 22:00 exceeds window)
```

```
Schedule: 09:00 to 12:00, Cyclic ON=30min, OFF=30min
→ Cycle 1: ON 09:00–09:30, OFF 09:30–10:00
→ Cycle 2: ON 10:00–10:30, OFF 10:30–11:00
→ Cycle 3: ON 11:00–11:30, OFF 11:30–12:00
```

---

## 1. Manual Stop During Cyclic Schedule

**Behavior: Manual stop cancels the ENTIRE remaining schedule window, not just the current cycle.**

When the user manually stops the device (via button, web interface, MQTT, or webhook):

1. `manualOff` is set to `1`
2. `lastManualOffTime` is recorded as the current time

The schedule restart condition (main.cpp:905) checks:
```
!(lastManualOffTime >= nextEvent.schStartTime && lastManualOffTime < nextEvent.schEndTime)
```

Since the manual stop time falls within the schedule window, this condition is **false**, which **blocks all subsequent cycles** in that window.

**When does it clear?**
- `manualOff` resets to `0` only when the current time moves **outside the entire schedule window** (`!scheduledTimeOuter` at main.cpp:1120–1122)
- The next day's (or next occurrence's) schedule will start normally

**Example:**
```
Schedule: 09:00–12:00, Cyclic ON=30min, OFF=30min
User manually stops at 09:15 (during Cycle 1 ON period)
→ Cycle 1 remaining: CANCELLED
→ Cycle 2 (10:00): CANCELLED
→ Cycle 3 (11:00): CANCELLED
→ Next day 09:00: Starts normally
```

---

## 2. Fault Conditions and Cyclic Schedules

When a fault occurs during a cyclic ON period, the relay is turned OFF and `manualOff` is set to the corresponding fault code. Unlike manual stop, faults support **auto-retry** if the restart option is enabled.

### Fault Types and Their Codes

| Fault | manualOff Code | Retry Possible? | Retry Delay |
|-------|---------------|-----------------|-------------|
| Dry Run (low current) | 2 | Yes, if bit 1 of restartOptions is set | `dryRunRetryMinutes` |
| Overcurrent | 2 | Yes, if bit 2 of restartOptions is set | `overRunRetryMinutes` |
| Low Voltage | 4 | Yes, if bit 3 of restartOptions is set | `lowVoltageRetryMinutes` |
| High Voltage | 4 | Yes, if bit 4 of restartOptions is set | `highVoltageRetryMinutes` |
| Phase Failure | 4 | Yes, if bit 5 of restartOptions is set | `dryRunRetryMinutes` |
| Phase Sequence Reverse | 4 | Yes, if bit 6 of restartOptions is set | `dryRunRetryMinutes` |
| Float Alert | 3 | No auto-retry | Clears when outside schedule window |

### Behavior With Cyclic Schedules

**Important: Faults clear differently based on their type.**

The clearing logic (main.cpp:1120–1132) has two levels:
- **Outside entire schedule window** (`!scheduledTimeOuter`): ALL manualOff codes are cleared
- **Between cycles** (`!scheduledTime`, i.e., in cyclic OFF gap): Only codes **3 (float), 4 (voltage), 5 (maxRuntime)** are cleared

This means:
- **Dry run / Overcurrent (code 2)**: Blocks the **entire schedule window** — does NOT auto-clear between cycles. Can only restart via auto-retry (if enabled) or when the schedule window ends.
- **Voltage faults (code 4)**: Auto-clear when the current cycle's ON period ends. The **next cycle can start** even without auto-retry, as long as the voltage has normalized.
- **Float (code 3)**: Also auto-clears between cycles.

**With auto-retry enabled (additional path):** Even for dry run/overcurrent, the relay can restart after the retry delay elapses, using the retry check (`curDateTime > lastFaultTime + retryMinutes`).

**Example — Dry Run (code 2, does NOT clear between cycles):**
```
Schedule: 09:00–12:00, Cyclic ON=30min, OFF=30min

Without auto-retry:
  Dry run fault at 09:15 → relay OFF, manualOff=2
  → Cycle 2 at 10:00: BLOCKED (manualOff=2 not cleared between cycles)
  → Cycle 3 at 11:00: BLOCKED
  → Next day 09:00: Starts normally

With auto-retry (5-min retry delay):
  Dry run fault at 09:15 → relay OFF, manualOff=2
  → Retry allowed after 09:20
  → Cycle 2 at 10:00: CAN START (retry window passed)
```

**Example — Low Voltage (code 4, DOES clear between cycles):**
```
Schedule: 09:00–12:00, Cyclic ON=30min, OFF=30min

Low voltage fault at 09:15 → relay OFF, manualOff=4
→ At 09:30 (cycle boundary): !scheduledTime → manualOff=4 is cleared to 0
→ Cycle 2 at 10:00: CAN START (even without auto-retry enabled)
   (provided voltage has normalized — otherwise fault triggers again)
```

### Fault Detection Thresholds

| Fault | Condition | Detection Duration |
|-------|-----------|-------------------|
| Dry Run | Current < `dryRunCurrentLimit` | 4 seconds (after 7s settle) |
| Overcurrent | Current > `overRunCurrentLimit` | 4 seconds (after 7s settle) |
| Low Voltage | Voltage < `lowVoltageLimit` | 7 seconds |
| High Voltage | Voltage > `highVoltageLimit` | 7 seconds |

---

## 3. MaxRuntime and BreakTime vs Cyclic Timer

### MaxRuntime
`maxRunTimeMins` is a global safety limit — the maximum continuous ON duration regardless of schedule type.

- **Applies to all schedule types** (regular, cyclic, manual, flow-based)
- When the relay has been ON for `maxRunTimeMins`, it is forced OFF with `manualOff = 5`
- The relay OFF reason is logged as `'B'` (Break)

### BreakTime
`breakTimeMins` is the mandatory cool-down period after a MaxRuntime stop.

- After a MaxRuntime stop, the relay cannot restart until `breakTimeMins` has elapsed
- Check: `manualOff != 5 || (millis() > relayOffTime + breakTimeMins * 60 * 1000)`
- Once breakTime elapses, `manualOff = 5` is cleared when `!scheduledTime` (main.cpp:1130)

### Key Difference: Cyclic ON Duration vs MaxRuntime

| Feature | Cyclic ON Duration | MaxRuntime |
|---------|-------------------|------------|
| **Scope** | Per-schedule setting | Global device setting |
| **Purpose** | Defines the ON period within each cycle | Safety limit for continuous operation |
| **After OFF** | Next cycle starts after OFF duration | Must wait for BreakTime before any restart |
| **Blocking** | Only blocks during OFF period of cycle | Blocks ALL operation until BreakTime elapses |
| **Typical values** | Minutes (e.g., 10 min ON, 50 min OFF) | Hours (e.g., 4 hours max) |

### Interaction Between MaxRuntime and Cyclic

MaxRuntime can interrupt a cyclic ON period if the ON duration exceeds maxRunTimeMins.

**Example:**
```
MaxRuntime: 20 min, BreakTime: 10 min
Schedule: 09:00–12:00, Cyclic ON=30min, OFF=30min

09:00 → Cycle 1 ON starts
09:20 → MaxRuntime hit → relay OFF, manualOff=5
09:20–09:30 → BreakTime waiting (10 min)
09:30 → BreakTime elapsed, manualOff=5 cleared
10:00 → Cycle 2 ON starts (next scheduledTime window)
10:20 → MaxRuntime hit again → relay OFF
... pattern repeats
```

**If cyclic ON duration is less than MaxRuntime**, MaxRuntime never triggers during normal cyclic operation:
```
MaxRuntime: 60 min, BreakTime: 15 min
Schedule: 09:00–12:00, Cyclic ON=10min, OFF=50min

09:00 → Cycle 1 ON (10 min < 60 min MaxRuntime — no conflict)
09:10 → Cycle 1 OFF (cyclic OFF period)
10:00 → Cycle 2 ON
... MaxRuntime never triggers
```

---

## 4. Summary: What Stops What?

| Event | manualOff | Stops Current Cycle? | Stops Remaining Cycles? | Clears Between Cycles? | Resumes Automatically? |
|-------|-----------|---------------------|------------------------|----------------------|----------------------|
| **Manual Stop** | 1 | Yes | Yes (entire window) | No | No — next schedule occurrence only |
| **Dry Run / Overcurrent** | 2 | Yes | Yes (entire window) | No | Only via auto-retry (if enabled) |
| **Float Alert** | 3 | Yes | Temporarily | Yes | Yes — clears at next cycle boundary |
| **Voltage Fault (high/low)** | 4 | Yes | Temporarily | Yes | Yes — clears at next cycle boundary |
| **Phase Failure / Reverse** | 4 | Yes | Temporarily | Yes | Yes — clears at next cycle boundary |
| **MaxRuntime Break** | 5 | Yes | Temporarily | Yes (after BreakTime) | Yes — after BreakTime elapses |
| **Cyclic OFF period** | 0 | N/A (normal) | No | N/A | Yes — next cycle starts automatically |
| **Schedule window end** | — | Yes | N/A | N/A | Next occurrence starts normally |

---

## 5. Run Type Codes Reference

| Code | Meaning | Source |
|------|---------|--------|
| `'S'` | Scheduled start | Regular schedule |
| `'C'` | Cyclic start | Cyclic schedule |
| `'R'` | Resume | Restart after power loss within schedule |
| `'M'` | Manual OFF | Button press |
| `'W'` | Web interface OFF | Local portal |
| `'Q'` | MQTT OFF | Cloud command |
| `'H'` | Webhook OFF | HTTP webhook |
| `'A'` | Alert/Fault OFF | Dry run, overcurrent, voltage fault |
| `'F'` | Float OFF | Float switch alert |
| `'P'` | Phase failure OFF | Phase failure |
| `'B'` | Break OFF | MaxRuntime limit reached |
| `'T'` | Timer/Countdown OFF | Countdown timer expired |
| `'L'` | Flow start | Float/flow-based start |
