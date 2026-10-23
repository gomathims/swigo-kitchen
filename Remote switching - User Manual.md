# Swigo-Gen2 - Remote Switching User Manual

## Overview

Remote Switching allows you to control a Master SwiGO SuperSmart device from a Remote SwiGO SuperSmart device. This feature enables you to operate your pump or device from a different location using the remote device's buttons.

## Float Mode Settings

Before configuring Remote Switching, it's important to understand the Float Mode settings on your Remote device. The Float Mode determines how the Remote device interacts with the Master device and provides additional safety and automation features.

### Float Mode Options

The Remote device can be configured with one of three Float Mode settings:

**1. No Float Switch (Disabled)**
- Float switch functionality is disabled
- Remote switching works normally without any float-based restrictions or automation
- Use this option if you don't have a float switch connected

**2. Tank Full Indication**
- Designed for overflow protection when a float switch is connected
- The float switch detects when the tank is full (overflow condition)
- When overflow is detected, the Remote device will automatically stop the Master device
- Manual start commands from the Remote device will be blocked if overflow is already detected
- Provides visual LED indication when tank is full
- Ideal for preventing water overflow in storage tanks

**3. Flow Detection**
- Designed for automatic pump operation based on water flow detection
- Uses a flow paddle switch to detect when water flow starts or stops
- When flow is detected, the Remote device automatically starts the Master device
- When flow stops, the Remote device automatically stops the Master device
- Manual start commands will be blocked if no flow is detected
- Provides extended operation duration (23 hours 59 minutes) when flow is detected
- Ideal for applications where the pump should run only when water flow is present

### Important Safety Note

⚠️ **WARNING**: Float switch wires must output 5V DC only and should be connected directly to the float switch. Do NOT connect float switch wires to 220V AC, as this will void the warranty and may cause damage.

### How Float Mode Affects Remote Switching

The Float Mode setting on the Remote device directly impacts how Remote Switching operates:

- **With Tank Full Indication**: The Remote device monitors for overflow conditions and will automatically protect the Master device by stopping it when overflow is detected
- **With Flow Detection**: The Remote device can automatically start and stop the Master device based on flow conditions, providing hands-free operation
- **Without Float Mode**: Remote switching operates with manual control only, without automatic safety or flow-based features

## Configuration

### Setting Up Remote Switching

1. Open the **Settings** tab on both devices
2. Select the Remote Switching mode:
   - **None**: Remote switching is disabled
   - **Master**: This device can be controlled by remote devices, while still maintaining all its normal local functions
   - **Remote**: This device controls a master device. When set to Remote, the device's local relay switching is disabled

### Connecting Devices on the Same Network

When both devices are on the same WiFi network:

1. Configure the Remote device with the Master device's network address and credentials
2. The Remote device will be able to control the Master device directly

### Connecting Devices on Different Networks

When devices are not on the same WiFi network:

1. Ensure both devices are connected to IOT.EVIQTECH.COM
2. Get the MAC address of the Master device
3. In IOT.EVIQTECH.COM:
   - Open the Devices menu
   - Click on the Remote device (right panel will open)
   - Go to the Relations tab
   - Click on "Inbound" or "To" relations
   - Click the "+" button to add a new relation
   - Set the relation name to "Manages"
   - Set entity type to "Device"
   - Enter the Master device's MAC address or select it from the device list

## Using Remote Switching

### Basic Operation

**Starting the Master Device:**
- Press any numbered button (1, 2, 3, etc.) on the Remote device
- The Master device will turn on for the duration corresponding to that button number
- For example, pressing Button 1 on the Remote will start the Master for the duration set for Button 1 on the Master device

**Stopping the Master Device:**
- Press the Stop button on the Remote device
- The Master device will turn off immediately

### Status Indicators

**Blue LED Indication:**
- When the Master device is already ON, the Remote device will show a blinking blue LED
- This indicates that the Master device is currently running
- The blue LED will stop blinking when:
  - The Master device turns off
  - The connection is lost (after 3 failed attempts to check status)

**Status Checking:**
- When the blue LED is blinking, the Remote device automatically checks the Master device's status every 20 seconds
- This ensures you always know if the Master device is running

### Fault Conditions

If the Master device is experiencing a fault condition (such as high/low voltage, dry run, overload, or flow detection issues), the attempt to start it will be logged but the device will not start.

## Special Features

### Tank Full Indication Mode

When the Remote device's float mode is set to "Tank Full Indication":

**Overflow Protection:**
- If overflow is detected on the Remote device, it will automatically send a stop command to the Master device
- A Tank Full LED indicator will light up on the Remote device

**Preventing Accidental Starts:**
- If you try to press a start button when overflow is already detected, the start command will not be sent
- The attempt will be logged, and the Tank Full LED will indicate the condition

**Continuous Monitoring:**
- If overflow is detected and the Master device is started directly (not through the Remote), the Remote device will automatically send stop commands every 20 seconds until the overflow condition clears

### Flow Detection Mode

When the Remote device's float mode is set to "Flow Detection":

**Automatic Operation:**
- When flow is detected starting, the Remote device will automatically start the Master device for an extended duration (23 hours 59 minutes)
- When flow stops, the Remote device will automatically send a stop command to the Master device

**Manual Operation:**
- If you press a start button when no flow is detected, the start command will not be sent
- The attempt will be logged, and the Tank Full LED combination will indicate that flow is required

## Important Notes

- When a device is configured as "Remote", its local relay switching is disabled - it can only control the Master device
- When a device is configured as "Master", it responds to remote switching commands while maintaining all its normal local functions
- Status synchronization works automatically when both devices are connected via IOT.EVIQTECH.COM, so you'll always see the Master device's status on the Remote device
- All operations and fault conditions are logged for troubleshooting purposes

