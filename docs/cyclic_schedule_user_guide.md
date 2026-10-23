# Cyclic Schedule & Protection Behavior — User Guide

## 1. What is a Cyclic Schedule?

A cyclic schedule repeatedly turns the device ON and OFF within a defined time window. You configure:

- **Start Time** and **End Time** — the overall schedule window
- **ON Duration** — how long the device stays ON in each cycle
- **OFF Duration** — how long the device stays OFF before the next cycle begins

The device automatically repeats these ON/OFF cycles until the schedule window ends.

**Example 1:**
```
Schedule: 21:00 to 22:00 | ON: 10 min, OFF: 50 min

  21:00 ─── ON ───▶ 21:10 ─── OFF ───▶ 22:00 (window ends)
  (Only 1 cycle fits in this window)
```

**Example 2:**
```
Schedule: 09:00 to 12:00 | ON: 30 min, OFF: 30 min

  09:00 ── ON ──▶ 09:30 ── OFF ──▶ 10:00 ── ON ──▶ 10:30 ── OFF ──▶ 11:00 ── ON ──▶ 11:30 ── OFF ──▶ 12:00
  Cycle 1                          Cycle 2                          Cycle 3
```

---

## 2. Manual Stop During a Cyclic Schedule

When you manually stop the device (via physical button, app, cloud, or webhook) during a cyclic schedule:

> **The entire schedule is stopped — not just the current cycle.**

All remaining cycles within that schedule window are cancelled. The device will not turn ON again until the **next schedule occurrence** (e.g., the next day).

**Example:**
```
Schedule: 09:00–12:00 | ON: 30 min, OFF: 30 min

  09:00 ── ON ──▶ 09:15  User presses STOP
                          ✗ Cycle 2 at 10:00 — CANCELLED
                          ✗ Cycle 3 at 11:00 — CANCELLED
                          ✓ Next day 09:00 — Starts normally
```

**Why?** Manual stop is treated as an intentional user decision. The device respects it for the remainder of that schedule window.

---

## 3. How Faults Affect a Cyclic Schedule

When a protection fault occurs during a cyclic ON period, the device turns OFF immediately. What happens next depends on the **type of fault**.

### Faults That Block the Entire Schedule

These faults are considered serious — they indicate a load or wiring issue that is unlikely to resolve on its own:

| Fault | What It Means |
|-------|---------------|
| **Dry Run** | Current is too low (e.g., pump running dry) |
| **Overcurrent** | Current exceeds the safe limit |

**Behavior:** The device stays OFF for the **remainder of the schedule window**. No further cycles will run.

**With Auto-Retry enabled:** If you have configured auto-retry for these faults, the device will attempt to restart after the configured retry delay. This can allow a later cycle to run if the retry delay has passed.

**Example — Dry Run, no auto-retry:**
```
Schedule: 09:00–12:00 | ON: 30 min, OFF: 30 min

  09:00 ── ON ──▶ 09:15  Dry Run detected → device OFF
                          ✗ Cycle 2 at 10:00 — BLOCKED
                          ✗ Cycle 3 at 11:00 — BLOCKED
                          ✓ Next day 09:00 — Starts normally
```

**Example — Dry Run, auto-retry after 5 minutes:**
```
  09:00 ── ON ──▶ 09:15  Dry Run detected → device OFF
                   09:20  Retry window opens
                          ✓ Cycle 2 at 10:00 — CAN START
```

### Faults That Clear Between Cycles

These faults are considered transient — the condition may resolve on its own:

| Fault | What It Means |
|-------|---------------|
| **Low Voltage** | Supply voltage is below the safe limit |
| **High Voltage** | Supply voltage is above the safe limit |
| **Phase Failure** | One or more phases are missing |
| **Phase Sequence Reverse** | Phase wiring is reversed |
| **Float Alert** | Float switch has triggered |

**Behavior:** The device turns OFF, but the fault **automatically clears at the next cycle boundary**. The next cycle can start normally — provided the fault condition has actually resolved (e.g., voltage has returned to normal). If the fault persists, the device will detect it again and turn OFF.

**Example — Low Voltage:**
```
Schedule: 09:00–12:00 | ON: 30 min, OFF: 30 min

  09:00 ── ON ──▶ 09:15  Low Voltage detected → device OFF
                   09:30  Cycle boundary — fault clears automatically
                          ✓ Cycle 2 at 10:00 — CAN START (if voltage is normal)
```

### Fault Detection Timing

| Fault | How It Is Detected |
|-------|-------------------|
| Dry Run | Current stays below limit for 4 seconds (checked after 7-second settle time) |
| Overcurrent | Current stays above limit for 4 seconds (checked after 7-second settle time) |
| Low Voltage | Voltage stays below limit for 7 seconds |
| High Voltage | Voltage stays above limit for 7 seconds |

The settle time ensures the device ignores brief transients (e.g., inrush current at startup).

---

## 4. MaxRuntime and BreakTime

These are **global safety settings** that apply to all schedule types, including cyclic.

### MaxRuntime
The maximum duration the device is allowed to stay ON continuously. If the device has been ON for this long, it is **forced OFF** regardless of the schedule.

### BreakTime
After a MaxRuntime stop, the device must remain OFF for this duration before it can turn ON again. This is a mandatory cool-down period.

### How They Differ From Cyclic ON/OFF Durations

| | Cyclic ON/OFF Duration | MaxRuntime / BreakTime |
|---|---|---|
| **What is it?** | Defines the repeating ON/OFF pattern within a schedule | A global safety limit on continuous operation |
| **Scope** | Applies to one specific schedule | Applies to the entire device, across all schedules |
| **Purpose** | Operational pattern (e.g., irrigation cycles) | Equipment protection (e.g., motor overheating) |
| **After turning OFF** | Next cycle starts after the configured OFF duration | Device must wait for BreakTime before any restart |

### When MaxRuntime Is Shorter Than Cyclic ON Duration

MaxRuntime will interrupt the cycle early and enforce a BreakTime pause.

**Example:**
```
MaxRuntime: 20 min | BreakTime: 10 min
Schedule: 09:00–12:00 | Cyclic ON: 30 min, OFF: 30 min

  09:00 ── ON ──▶ 09:20  MaxRuntime reached → forced OFF
                   09:20 ── BreakTime ──▶ 09:30 (cool-down complete)
                          ✓ Cycle 2 at 10:00 — CAN START
                   10:00 ── ON ──▶ 10:20  MaxRuntime reached again → forced OFF
                   ... pattern repeats
```

Each cycle effectively runs for only 20 minutes instead of the configured 30 minutes.

### When MaxRuntime Is Longer Than Cyclic ON Duration

MaxRuntime never triggers during normal operation because each cycle ends before the limit is reached.

**Example:**
```
MaxRuntime: 60 min | BreakTime: 15 min
Schedule: 09:00–12:00 | Cyclic ON: 10 min, OFF: 50 min

  09:00 ── ON ──▶ 09:10  Cyclic OFF (10 min < 60 min — MaxRuntime not reached)
  10:00 ── ON ──▶ 10:10  Cyclic OFF
  ... MaxRuntime never triggers
```

---

## 5. Summary Table

| Event | Stops Current Cycle? | Remaining Cycles? | Resumes Automatically? |
|-------|---------------------|-------------------|----------------------|
| **Manual Stop** | Yes | All cancelled | No — next schedule occurrence only |
| **Dry Run / Overcurrent** | Yes | All blocked | Only if auto-retry is enabled |
| **Low/High Voltage** | Yes | Temporarily paused | Yes — clears at next cycle boundary |
| **Phase Failure / Reverse** | Yes | Temporarily paused | Yes — clears at next cycle boundary |
| **Float Alert** | Yes | Temporarily paused | Yes — clears at next cycle boundary |
| **MaxRuntime Break** | Yes | Temporarily paused | Yes — after BreakTime elapses |
| **Cyclic OFF period** | Normal operation | Not affected | Yes — next cycle starts automatically |
| **Schedule window ends** | Yes | N/A | Next occurrence starts normally |

---

## 6. Key Takeaways

1. **Manual stop = full schedule cancel.** If you press stop during a cyclic schedule, all remaining cycles are skipped. The next day's schedule runs normally.

2. **Dry run and overcurrent are treated seriously.** These faults block the entire schedule because they indicate a problem that won't resolve on its own (e.g., a dry pump or a short circuit).

3. **Voltage faults are treated as transient.** They clear between cycles, allowing the device to retry automatically when power conditions normalize.

4. **MaxRuntime is a safety net, not a scheduler.** It protects equipment from running too long. If your cyclic ON duration is shorter than MaxRuntime, they won't interfere with each other.

5. **Auto-retry gives you control over fault recovery.** For each fault type, you can independently enable auto-retry and set the retry delay. This is especially useful for dry run/overcurrent faults in cyclic schedules, where you may want to allow the next cycle to attempt a start.
