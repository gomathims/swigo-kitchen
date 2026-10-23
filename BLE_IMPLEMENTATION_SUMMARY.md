# BLE Provisioning Implementation Summary

## Quick Reference

### Activation
**Hold Button 1 + Button 2 together for 10 seconds**

### Timeout
**10 minutes** - BLE automatically stops after 10 minutes

### Authentication
**Device PIN (POP)** - Uses existing `devicePin` (default: "1234")

### Data Format
```json
{
  "ssid": "WiFiNetworkName",
  "password": "WiFiPassword",
  "pop": "1234"
}
```

---

## Implementation Details

### Files Modified/Created

#### New Files
1. **`src/ble_provisioning.h`** - BLE provisioning header
2. **`src/ble_provisioning.cpp`** - BLE provisioning implementation
3. **`BLE_PROVISIONING_GUIDE.md`** - Complete user & developer guide
4. **`CUSTOM_BLE_VS_WIFIPROV.md`** - Comparison with ESP32's WiFiProv.h

#### Modified Files
1. **`platformio.ini`** - Added NimBLE library
2. **`src/main.cpp`** - Added BLE initialization and timeout checking
3. **`src/power_monitoring.h`** - Added BLE button handling declarations
4. **`src/power_monitoring.cpp`** - Implemented handleBLE() function
5. **`src/globals.h`** - Added BLE_ENABLE_PRESS_DURATION constant
6. **`src/buttonpress.h`** - Added ignoreButton1FallOnce declaration

---

## Key Functions

### `void startBLEProvisioning()`
Starts BLE advertising and provisioning service.
- Initializes NimBLE
- Creates BLE service with two characteristics
- Starts advertising
- Sets 10-minute timer

### `void stopBLEProvisioning()`
Stops BLE provisioning.
- Stops advertising
- Disconnects clients
- Deinitializes NimBLE
- Resets state

### `void checkBLETimeout()`
Checks if 10-minute timeout has elapsed.
- Called in main loop
- Automatically stops BLE after timeout

### `void handleBLE()`
Monitors Button 1 + Button 2 for simultaneous 10-second press.
- Reads button states
- Tracks press duration
- Sets flag when both buttons held for 10 seconds

### `bool processBLEProvisioningData(const char* jsonData)`
Processes received WiFi credentials.
- Parses JSON
- Verifies POP against devicePin
- Saves WiFi credentials if POP matches
- Returns success/failure

---

## BLE Service Structure

### Service UUID
```
4fafc201-1fb5-459e-8fcc-c5c9c331914b
```

### Characteristics

#### WiFi Credentials Characteristic
- **UUID**: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- **Property**: Write
- **Purpose**: Receive WiFi credentials + POP as JSON

#### Status Characteristic
- **UUID**: `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`
- **Property**: Read, Notify
- **Purpose**: Report provisioning status
- **Values**:
  - `READY` - Waiting for credentials
  - `CONNECTED` - Client connected
  - `SUCCESS` - Provisioning successful
  - `FAILED` - Provisioning failed

---

## Security Features

### 1. POP Verification
```cpp
if (strcmp(pop, devicePin) != 0) {
    return false;  // Reject if POP doesn't match
}
```

### 2. 10-Minute Timeout
```cpp
if (elapsed >= BLE_PROVISIONING_TIMEOUT) {
    stopBLEProvisioning();  // Auto-stop after 10 minutes
}
```

### 3. Button Activation
```cpp
// Both buttons must be held simultaneously
if ((switch1State == HIGH) && (switch2State == HIGH) && 
    (switch3State == LOW) && (switch4State == LOW) && 
    (switchOffState == LOW)) {
    // Start BLE
}
```

---

## Flow Diagram

```
┌────────────────────────────────┐
│ User holds Button 1 + 2 (10s) │
└────────────┬───────────────────┘
             │
             ▼
┌────────────────────────────────┐
│ handleBLE() detects press      │
│ Sets buttonBLEEnablePressed    │
└────────────┬───────────────────┘
             │
             ▼
┌────────────────────────────────┐
│ main() calls startProvisioning()│
└────────────┬───────────────────┘
             │
             ▼
┌────────────────────────────────┐
│ BLE starts advertising         │
│ Timeout timer starts (10 min)  │
└────────────┬───────────────────┘
             │
             ▼
┌────────────────────────────────┐
│ Mobile app connects to BLE     │
└────────────┬───────────────────┘
             │
             ▼
┌────────────────────────────────┐
│ App writes JSON to WiFi char   │
│ {"ssid":"...","password":"...","pop":"1234"}│
└────────────┬───────────────────┘
             │
             ▼
┌────────────────────────────────┐
│ processBLEProvisioningData()   │
│ - Parse JSON                   │
│ - Verify POP                   │
│ - Save WiFi credentials        │
└────────────┬───────────────────┘
             │
      ┌──────┴──────┐
      │             │
      ▼             ▼
  POP Match    POP Mismatch
      │             │
      ▼             ▼
┌──────────┐  ┌──────────┐
│ SUCCESS  │  │ FAILED   │
└────┬─────┘  └──────────┘
     │
     ▼
┌────────────────────────────────┐
│ Status → "SUCCESS"             │
│ WiFi credentials saved         │
│ BLE stops automatically        │
└────────────┬───────────────────┘
             │
             ▼
┌────────────────────────────────┐
│ Device connects to WiFi        │
└────────────────────────────────┘
```

---

## Testing

### Using nRF Connect Mobile App

1. **Activate BLE**
   - Hold Button 1 + Button 2 for 10 seconds
   - Watch serial monitor for: "BLE Enable triggered by Button 1 + Button 2 (10 sec hold)"

2. **Scan & Connect**
   - Open nRF Connect app
   - Scan for devices
   - Find "SwiGO_XXXX" or your custom device name
   - Tap CONNECT

3. **Send Credentials**
   - Expand service `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
   - Find characteristic `beb5483e-36e1-4688-b7f5-ea07361b26a8`
   - Tap upload (↑) icon
   - Select "Text"
   - Enter: `{"ssid":"TestWiFi","password":"testpass","pop":"1234"}`
   - Send

4. **Check Status**
   - Read characteristic `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`
   - Should show "SUCCESS"

5. **Verify**
   - Check serial monitor for "WiFi credentials saved successfully"
   - Device should connect to WiFi

---

## Serial Monitor Output

### Successful Provisioning
```
BLE Enable triggered by Button 1 + Button 2 (10 sec hold)
=== Starting BLE Provisioning ===
BLE will be active for 10 minutes
Send WiFi credentials via BLE in JSON format:
{"ssid":"YourWiFiSSID","password":"YourPassword","pop":"1234"}
Initializing BLE...
BLE Service started successfully
BLE Provisioning started - Active for 10 minutes
BLE Device Name: SwiGO_A1B2

[... user connects and sends credentials ...]

BLE Client connected
Received BLE provisioning data
Data length: 67
POP verification successful
Saving WiFi credentials - SSID: MyHomeWiFi
WiFi credentials saved successfully
BLE Provisioning successful
Stopping BLE Provisioning...
BLE Provisioning stopped
```

### Failed POP Verification
```
BLE Client connected
Received BLE provisioning data
Data length: 67
POP verification failed - incorrect PIN
Expected: 1234
Received: 5678
BLE Provisioning failed
```

### Timeout
```
BLE Provisioning timeout reached (10 minutes)
Stopping BLE Provisioning...
BLE Provisioning stopped
```

---

## Advantages of This Implementation

### vs ESP32's WiFiProv.h

1. **Integrated Authentication** - Uses existing device PIN
2. **Simpler Protocol** - JSON instead of Protocol Buffers
3. **Lighter Memory** - NimBLE (~50KB) vs BlueDroid (~80-100KB)
4. **Auto Timeout** - 10-minute security timeout built-in
5. **Physical Activation** - Button 1 + 2 prevents accidental activation
6. **Easy Testing** - Works with standard BLE tools (nRF Connect)
7. **Simple Mobile Apps** - Native BLE APIs, no ESP SDK needed
8. **Full Control** - Custom error handling, extensibility

See `CUSTOM_BLE_VS_WIFIPROV.md` for detailed comparison.

---

## Troubleshooting

### BLE Doesn't Start
- **Check**: Are both Button 1 AND Button 2 held?
- **Check**: Are they held for full 10 seconds?
- **Check**: Are other buttons released?
- **Serial**: Look for "BLE Enable triggered..." message

### Can't Find Device in Scan
- **Check**: Is BLE still active? (10-minute timeout)
- **Check**: Is device within BLE range? (~10 meters)
- **Action**: Re-activate by holding buttons again

### Provisioning Fails
- **Check**: Is JSON format correct?
- **Check**: Is POP (device PIN) correct?
- **Check**: Are all required fields present? (ssid, password, pop)
- **Serial**: Check for error messages

### Device Doesn't Connect to WiFi
- **Check**: Are WiFi credentials correct?
- **Check**: Is WiFi network in range?
- **Check**: Is WiFi network 2.4GHz? (ESP32 doesn't support 5GHz)
- **Serial**: Check WiFi connection status

---

## Configuration

### Change Timeout Duration
Edit `src/ble_provisioning.h`:
```cpp
#define BLE_PROVISIONING_TIMEOUT (10 * 60 * 1000)  // 10 minutes
// Change to 5 minutes:
#define BLE_PROVISIONING_TIMEOUT (5 * 60 * 1000)
```

### Change Button Combination
Edit `src/power_monitoring.cpp` in `handleBLE()` function.

### Change Button Hold Duration
Edit `src/globals.h`:
```cpp
#define BLE_ENABLE_PRESS_DURATION 10000   // 10 seconds
// Change to 5 seconds:
#define BLE_ENABLE_PRESS_DURATION 5000
```

### Change BLE Device Name
BLE device name is automatically generated from:
1. Custom switch name (if set)
2. Or "SwiGO_" + last 4 hex digits of MAC address

To customize, edit `getBLEDeviceName()` in `src/ble_provisioning.cpp`.

---

## Future Enhancements

### Easy to Add:
1. **Additional Fields** - Add to JSON (timezone, device name, etc.)
2. **OTA Updates** - Send firmware via BLE
3. **Configuration** - Send device settings via BLE
4. **Status Reading** - Read device status via BLE
5. **Localization** - Multi-language error messages

### Example - Adding Timezone:
```cpp
// In processBLEProvisioningData()
if (doc.containsKey("timezone")) {
    const char* timezone = doc["timezone"];
    // Save timezone
}
```

---

## Performance

- **Memory Usage**: ~50KB RAM (NimBLE stack)
- **Connection Time**: ~2-3 seconds
- **Provisioning Time**: ~5 seconds (including WiFi connection)
- **Power Consumption**: ~100mA during BLE operation
- **Range**: ~10 meters (typical BLE range)

---

## Security Recommendations

1. **Change Default PIN**: Change default device PIN from "1234" to secure 4-digit PIN
2. **Keep Timeout**: Don't increase timeout beyond 10 minutes
3. **Physical Access**: BLE requires physical access to device (button press)
4. **Short Range**: BLE limited to ~10m reduces attack surface

---

## Support & Documentation

- **User Guide**: `BLE_PROVISIONING_GUIDE.md`
- **Comparison**: `CUSTOM_BLE_VS_WIFIPROV.md`
- **This Summary**: `BLE_IMPLEMENTATION_SUMMARY.md`

For questions or issues, check the serial monitor output for detailed debugging information.
