# BLE Provisioning Guide for SwiGO Super Smart

## Overview
The SwiGO Super Smart device supports BLE (Bluetooth Low Energy) provisioning for WiFi credentials. This allows users to configure WiFi settings without needing to connect to the device's Access Point.

## Features
- **10-minute timeout**: BLE remains active for 10 minutes after activation
- **Secure authentication**: Requires device PIN (POP - Proof of Possession) verification
- **Easy activation**: Hold Button 1 + Button 2 together for 10 seconds
- **Automatic WiFi connection**: Device connects to WiFi after successful provisioning

## How to Activate BLE Provisioning

### Button 1 + Button 2 Simultaneous Press
1. Press and hold **Button 1 AND Button 2** together for **10 seconds**
2. Release both buttons
3. BLE provisioning is now active for 10 minutes

**Important**: 
- Both Button 1 and Button 2 must be pressed **simultaneously**
- No other buttons should be pressed at the same time
- If any other button is pressed, or if only one of the two buttons is pressed, BLE provisioning will not activate

## BLE Service Details

### Service UUID
```
4fafc201-1fb5-459e-8fcc-c5c9c331914b
```

### Characteristics

#### 1. WiFi Credentials Characteristic (Write)
- **UUID**: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- **Property**: Write
- **Purpose**: Receive WiFi credentials and POP

#### 2. Status Characteristic (Read + Notify)
- **UUID**: `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`
- **Property**: Read, Notify
- **Purpose**: Get provisioning status
- **Values**: 
  - `READY` - BLE is ready to receive credentials
  - `CONNECTED` - Client connected to BLE
  - `SUCCESS` - Provisioning successful
  - `FAILED` - Provisioning failed

## Data Format

### JSON Structure
Send WiFi credentials as a JSON string to the WiFi Credentials Characteristic:

```json
{
  "ssid": "YourWiFiNetworkName",
  "password": "YourWiFiPassword",
  "pop": "1234"
}
```

### Field Descriptions
- **ssid** (required): WiFi network SSID (max 20 characters)
- **password** (required): WiFi password (max 20 characters, can be empty for open networks)
- **pop** (required): 4-digit device PIN for authentication (must match device PIN)

### Example
```json
{
  "ssid": "MyHomeWiFi",
  "password": "SecurePassword123",
  "pop": "1234"
}
```

## BLE Device Name
The device advertises itself with one of the following names:
- If device has a custom name set: `[CustomName]`
- Default: `SwiGO_XXXX` (where XXXX is the last 4 hex digits of the MAC address)

## Provisioning Flow

```
┌─────────────────────────────────────────┐
│  1. User holds Button 1 + 2 for 10s     │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│  2. BLE starts advertising              │
│     - Active for 10 minutes             │
│     - Device name visible               │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│  3. Mobile app connects to BLE          │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│  4. App reads Status characteristic     │
│     (should show "CONNECTED")           │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│  5. App writes WiFi credentials + POP   │
│     to WiFi Credentials characteristic  │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│  6. Device verifies POP                 │
│     - Compares with device PIN          │
└─────────────────┬───────────────────────┘
                  │
         ┌────────┴────────┐
         │                 │
         ▼                 ▼
   POP Match         POP Mismatch
         │                 │
         ▼                 ▼
┌──────────────┐  ┌──────────────────┐
│ Save WiFi    │  │ Reject & Notify  │
│ Credentials  │  │ Status: FAILED   │
└──────┬───────┘  └──────────────────┘
       │
       ▼
┌──────────────────────────────────────────┐
│  7. Notify Status: SUCCESS               │
└──────────────┬───────────────────────────┘
               │
               ▼
┌──────────────────────────────────────────┐
│  8. BLE stops automatically              │
└──────────────┬───────────────────────────┘
               │
               ▼
┌──────────────────────────────────────────┐
│  9. Device connects to WiFi              │
└──────────────────────────────────────────┘
```

## Error Handling

### POP Verification Failed
- **Status**: `FAILED`
- **Reason**: The POP (device PIN) provided doesn't match the device's stored PIN
- **Action**: Verify the correct device PIN and retry

### Invalid JSON Format
- **Status**: `FAILED`
- **Reason**: JSON data is malformed or missing required fields
- **Action**: Check JSON format and retry

### Timeout
- After 10 minutes of inactivity, BLE automatically stops
- **Action**: Re-activate BLE by holding Button 1 or 2 for 10 seconds

## Sample Mobile App Code

### Android (Kotlin) Example

```kotlin
import android.bluetooth.*
import android.bluetooth.le.*
import java.util.*

class BLEProvisioningManager(private val context: Context) {
    
    private val SERVICE_UUID = UUID.fromString("4fafc201-1fb5-459e-8fcc-c5c9c331914b")
    private val WIFI_CHAR_UUID = UUID.fromString("beb5483e-36e1-4688-b7f5-ea07361b26a8")
    private val STATUS_CHAR_UUID = UUID.fromString("1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e")
    
    private var bluetoothGatt: BluetoothGatt? = null
    
    fun scanForDevice() {
        val bluetoothManager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        val bluetoothAdapter = bluetoothManager.adapter
        val scanner = bluetoothAdapter.bluetoothLeScanner
        
        val scanCallback = object : ScanCallback() {
            override fun onScanResult(callbackType: Int, result: ScanResult) {
                val device = result.device
                if (device.name?.startsWith("SwiGO") == true) {
                    scanner.stopScan(this)
                    connectToDevice(device)
                }
            }
        }
        
        scanner.startScan(scanCallback)
    }
    
    private fun connectToDevice(device: BluetoothDevice) {
        bluetoothGatt = device.connectGatt(context, false, object : BluetoothGattCallback() {
            override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    gatt.discoverServices()
                }
            }
            
            override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    // Services discovered, ready to provision
                    sendWiFiCredentials("MyWiFi", "password123", "1234")
                }
            }
            
            override fun onCharacteristicWrite(
                gatt: BluetoothGatt,
                characteristic: BluetoothGattCharacteristic,
                status: Int
            ) {
                if (status == BluetoothGatt.GATT_SUCCESS) {
                    println("WiFi credentials sent successfully")
                }
            }
        })
    }
    
    fun sendWiFiCredentials(ssid: String, password: String, pop: String) {
        val service = bluetoothGatt?.getService(SERVICE_UUID)
        val characteristic = service?.getCharacteristic(WIFI_CHAR_UUID)
        
        val json = """{"ssid":"$ssid","password":"$password","pop":"$pop"}"""
        characteristic?.value = json.toByteArray()
        bluetoothGatt?.writeCharacteristic(characteristic)
    }
}
```

### iOS (Swift) Example

```swift
import CoreBluetooth

class BLEProvisioningManager: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    
    private let SERVICE_UUID = CBUUID(string: "4fafc201-1fb5-459e-8fcc-c5c9c331914b")
    private let WIFI_CHAR_UUID = CBUUID(string: "beb5483e-36e1-4688-b7f5-ea07361b26a8")
    private let STATUS_CHAR_UUID = CBUUID(string: "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e")
    
    private var centralManager: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var wifiCharacteristic: CBCharacteristic?
    
    override init() {
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: nil)
    }
    
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            centralManager.scanForPeripherals(withServices: [SERVICE_UUID], options: nil)
        }
    }
    
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                       advertisementData: [String : Any], rssi RSSI: NSNumber) {
        self.peripheral = peripheral
        centralManager.stopScan()
        centralManager.connect(peripheral, options: nil)
    }
    
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.delegate = self
        peripheral.discoverServices([SERVICE_UUID])
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let services = peripheral.services else { return }
        
        for service in services {
            if service.uuid == SERVICE_UUID {
                peripheral.discoverCharacteristics([WIFI_CHAR_UUID, STATUS_CHAR_UUID], for: service)
            }
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService,
                   error: Error?) {
        guard let characteristics = service.characteristics else { return }
        
        for characteristic in characteristics {
            if characteristic.uuid == WIFI_CHAR_UUID {
                wifiCharacteristic = characteristic
            }
        }
    }
    
    func sendWiFiCredentials(ssid: String, password: String, pop: String) {
        guard let peripheral = peripheral, let characteristic = wifiCharacteristic else {
            return
        }
        
        let json = """
        {"ssid":"\(ssid)","password":"\(password)","pop":"\(pop)"}
        """
        
        if let data = json.data(using: .utf8) {
            peripheral.writeValue(data, for: characteristic, type: .withResponse)
        }
    }
    
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic,
                   error: Error?) {
        if error == nil {
            print("WiFi credentials sent successfully")
        }
    }
}
```

## Testing with nRF Connect App

You can test BLE provisioning using the nRF Connect mobile app:

1. **Download nRF Connect**
   - Android: [Google Play Store](https://play.google.com/store/apps/details?id=no.nordicsemi.android.mcp)
   - iOS: [App Store](https://apps.apple.com/app/nrf-connect/id1054362403)

2. **Activate BLE on Device**
   - Hold Button 1 + Button 2 together for 10 seconds

3. **Scan and Connect**
   - Open nRF Connect app
   - Scan for devices
   - Find your device (named "SwiGO_XXXX" or custom name)
   - Tap "CONNECT"

4. **Send Credentials**
   - Find the service `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
   - Find the WiFi characteristic `beb5483e-36e1-4688-b7f5-ea07361b26a8`
   - Tap the upload icon (↑)
   - Select "Text" format
   - Enter: `{"ssid":"YourWiFi","password":"YourPass","pop":"1234"}`
   - Send

5. **Check Status**
   - Read the Status characteristic `1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e`
   - Should show "SUCCESS" if provisioning worked

## Troubleshooting

### BLE doesn't start
- Ensure you're holding **both Button 1 AND Button 2** together
- Make sure no other buttons are pressed
- Hold for the full 10 seconds
- Check serial output for "BLE Provisioning started" message

### Can't find device in BLE scan
- Make sure BLE was activated (check serial monitor)
- Check if 10 minutes have elapsed (BLE auto-stops after timeout)
- Try moving closer to the device

### Provisioning fails with "POP verification failed"
- Verify the device PIN is correct (default is "1234")
- Check that POP in JSON is exactly 4 digits
- Ensure JSON format is correct

### Device doesn't connect to WiFi after provisioning
- Check WiFi credentials are correct
- Ensure WiFi network is within range
- Check serial monitor for connection status

## Security Considerations

1. **POP (Proof of Possession)**: Always change the default device PIN (1234) to a secure 4-digit PIN
2. **BLE Range**: BLE has limited range (~10m), reducing risk of unauthorized access
3. **Timeout**: 10-minute timeout prevents prolonged BLE exposure
4. **One-time use**: BLE stops automatically after successful provisioning

## Serial Monitor Output

When BLE provisioning is active, you'll see:
```
=== Starting BLE Provisioning ===
BLE will be active for 10 minutes
Send WiFi credentials via BLE in JSON format:
{"ssid":"YourWiFiSSID","password":"YourPassword","pop":"1234"}
Initializing BLE...
BLE Service started successfully
BLE Provisioning started - Active for 10 minutes
BLE Device Name: SwiGO_A1B2
```

When credentials are received:
```
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

## API Reference

### Functions

#### `void startBLEProvisioning()`
Starts BLE advertising and provisioning service. Active for 10 minutes.

#### `void stopBLEProvisioning()`
Manually stops BLE provisioning (automatically called after success or timeout).

#### `void checkBLETimeout()`
Checks if 10-minute timeout has elapsed. Called in main loop.

#### `bool processBLEProvisioningData(const char* jsonData)`
Processes received WiFi credentials and POP. Returns true if successful.

### Global Variables

#### `bool bleProvisioningActive`
Indicates if BLE provisioning is currently active.

#### `BLEProvisioningState bleProvisioningState`
Current provisioning state (IDLE, ADVERTISING, CONNECTED, SUCCESS, FAILED, TIMEOUT).

---

**Note**: This implementation uses NimBLE library for better memory efficiency and performance on ESP32-S3 devices.
