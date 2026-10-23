#include "power_sensor_bl0939.h"

#include <Arduino.h>

#include "logdebug.h"

bool volatile SENSOR_RESET_IN_PROGRESS = 0;

uint8_t rx_cache[35];
uint8_t serial_in_byte = 0;
uint8_t buffer_size = 35;
uint8_t byte_counter = 0;
uint8_t checksum = 0;
volatile uint32_t requestTime;
bool found_header = false;
bool received = false;
bool crc_checked = false;
bool decoded = false;

const uint8_t bl0939_init[6][4] = {{REG_SOFT_RESET, 0x5A, 0x5A, 0x5A},
                                   {REG_USR_WRPROT, 0x55, 0x00, 0x00},
                                   {REG_MODE, 0x40, 0x10, 0x00}, // LFSEL = 1 (Bit 6), AC_FREQ_SEL 50Hz, RMS_UPDATE_SEL 800mS
                                   {REG_TPS_CTRL, 0xFF, 0x47, 0x00},
                                   {REG_IA_FAST_RMS_CTRL, 0xD9, 0x04, 0x00},
                                   // Leakage offset: Ix_FAST_RMS_CTRL=1870*0.95=1776. 30mA detection with 5% jitter
                                   // For 20mA detection: 1241 = 0x04D9
                                   {REG_IB_FAST_RMS_CTRL, 0xD9, 0x04, 0x00}};

// Reads register 0x25 (CF frequency-division status). Fills response[0..3]
// with the raw 4-byte reply (3 data bytes + CRC) and returns the number of
// bytes received (4 on success, less on timeout). The caller can both inspect
// the raw bytes (for logging) and judge correctness: byte0 == 0x03 means the
// CF divider is correctly trimmed; 0x00 means it was lost and the energy
// counter is running 256x too fast.
uint8_t BL0939_readCFDivider(uint8_t response[4]) {
    while (Serial2.available()) { Serial2.read(); }

    Serial2.write(READ_COMMAND);
    Serial2.write((uint8_t)0x25);

    uint8_t count = 0;
    unsigned long start = millis();
    while (count < 4 && (millis() - start) < 100) {
        if (Serial2.available()) {
            response[count++] = Serial2.read();
        }
        yield();
    }
    return count;
}

// Returns true only if the read succeeded, CRC matched, and the divider value
// (response[0]) is 0x03 as required by Belling.
bool BL0939_isCFDividerOK(const uint8_t response[4], uint8_t count) {
    if (count < 4) return false;
    uint8_t expected = 0xFF ^ (READ_COMMAND + 0x25 + response[0] + response[1] + response[2]);
    if (expected != response[3]) return false;
    return (response[0] == 0x03);
}

// Reconfigures the CF frequency-division registers using the exact sequence
// supplied by Belling support. Use when the divider read above is not OK.
void BL0939_reconfigureCFDivider() {
    static const uint8_t cf_init[6][6] = {
        {0xA5, 0x39, 0x42, 0x00, 0x00, 0xDF},
        {0xA5, 0x38, 0x3C, 0x00, 0x00, 0xE6},
        {0xA5, 0x39, 0x42, 0x00, 0x00, 0xDF},
        {0xA5, 0x2B, 0x10, 0x00, 0x00, 0x1F},
        {0xA5, 0x2E, 0x38, 0x00, 0x00, 0xF4},
        {0xA5, 0x25, 0x03, 0x00, 0x00, 0x32},
    };
    for (uint32_t i = 0; i < 6; i++) {
        Serial2.write(cf_init[i], 6);
        Serial2.flush();
        delay(2);
    }
}

void BL0939_setup() {
    SENSOR_RESET_IN_PROGRESS = true;
    for (uint32_t i = 0; i < 6; i++) {
        uint8_t crc, byte;
        crc = byte = WRITE_COMMAND;
        Serial2.write(byte);
        for (uint32_t j = 0; j < 4; j++) {
            crc += byte = bl0939_init[i][j];
            Serial2.write(byte);
        }
        Serial2.write(0xFF ^ crc);
        delay(1);
    }
    Serial2.flush();
    delay(1000);
    // The soft reset above zeroes CF_CNT — sync the baseline so the Decode()
    // plausibility filter treats the first post-reset frame as legitimate.
    energy = 0;

    uint8_t cfResp[4] = {0, 0, 0, 0};
    uint8_t cfCount = BL0939_readCFDivider(cfResp);
    // {
    //     char buf[96];
    //     snprintf(buf, sizeof(buf),
    //              "BL0939 CF divider read (reg 0x25): bytes=%u resp=%02X %02X %02X %02X",
    //              cfCount, cfResp[0], cfResp[1], cfResp[2], cfResp[3]);
    //     logDebugAction(DebugAction::SYSTEM_FAILURE, buf);
    // }

    if (!BL0939_isCFDividerOK(cfResp, cfCount)) {
        char msg[] = "BL0939 CF divider trim lost - reconfiguring";
        logDebugAction(DebugAction::SYSTEM_FAILURE, msg);
        BL0939_reconfigureCFDivider();
        delay(100);

        uint8_t cfResp2[4] = {0, 0, 0, 0};
        uint8_t cfCount2 = BL0939_readCFDivider(cfResp2);
        char buf2[96];
        snprintf(buf2, sizeof(buf2),
                 "BL0939 CF divider after reconfigure: bytes=%u resp=%02X %02X %02X %02X",
                 cfCount2, cfResp2[0], cfResp2[1], cfResp2[2], cfResp2[3]);
        logDebugAction(DebugAction::SYSTEM_FAILURE, buf2);

        if (!BL0939_isCFDividerOK(cfResp2, cfCount2)) {
            char failMsg[] = "BL0939 CF divider reconfigure failed";
            logDebugAction(DebugAction::SYSTEM_FAILURE, failMsg);
        }
    }

    if (!received) {
        Request_Data();
        delay(1000);
        Read_Data();
    }
    SENSOR_RESET_IN_PROGRESS = false;
}

void Request_Data() {
    // flush(false) drains TX *and* clears the RX buffer. The default flush()
    // maps to uartFlush() -> uartFlushTxOnly(uart, true), which waits for TX
    // only and leaves RX untouched - so bytes left over from an abandoned reply
    // would be read as the head of this request's frame and fail CRC every time.
    Serial2.flush(false);

    // A new request means a fresh frame; drop any partial one still being
    // assembled, otherwise this reply is appended behind those stale bytes.
    found_header = false;
    received = false;
    crc_checked = false;
    byte_counter = 0;

    Serial2.write(READ_COMMAND);
    Serial2.write(REQUEST_FULL);
    requestTime=millis();
}

void Read_Data() {
    while (Serial2.available()) {
        yield();
        serial_in_byte = Serial2.read();

        if (serial_in_byte == PACKET_HEADER && !found_header) {
            byte_counter = 0;
            found_header = true;
        }
        if (found_header) {
            rx_cache[byte_counter++] = serial_in_byte;

            if (buffer_size == byte_counter) {
                received = true;
                // Stop here. Setting `received` used to leave the loop running,
                // so with more than one reply queued (the 500ms retry fires
                // while a long flash write delays this loop, leaving 70+ bytes
                // in the 256-byte UART buffer) byte_counter ran past
                // rx_cache[35] and overwrote the globals declared right after
                // it - serial_in_byte, buffer_size, byte_counter, checksum,
                // requestTime, found_header, received... i.e. it corrupted the
                // state machine's own control variables.
                //
                // One reply per request, so whatever is still queued is stale;
                // drop it rather than let it head the next frame.
                Serial2.flush(false);
                return;
            }
        }
    }
}

void CRC_Check() {
    if (rx_cache[0] == PACKET_HEADER) {
        checksum = READ_COMMAND;
        for (uint32_t i = 0; i < buffer_size - 1; i++) {
            checksum += rx_cache[i];
        }
        checksum ^= 0xFF;
        if (checksum == rx_cache[buffer_size - 1]) {
            crc_checked = true;
        } else {
            // Bad frame: discard it completely and let power_sensing_loop()
            // re-request.
            //
            // This used to byte-shift rx_cache looking for another PACKET_HEADER
            // to resync onto. That suits a continuous stream, but the BL0939 is
            // request/response - one 35-byte reply per Request_Data() - so there
            // is no later frame buffered to resync into, and the attempt did
            // harm three ways: the shifted partial frame could never complete
            // (no more bytes are coming); found_header stayed true, so the NEXT
            // reply was appended behind the stale bytes and was itself corrupted;
            // and 0x55 is an ordinary data byte, so the "resync" usually landed
            // mid-payload anyway. When the shift happened to land on a 0x55 it
            // also left received=true with crc_checked=false, which wedged the
            // state machine permanently - readings froze until reboot, taking
            // the low-voltage / dry-run / overload cutouts with them.
            while (Serial2.available()) {
                Serial2.read();
            }
            found_header = false;
            received = false;
            crc_checked = false;
            byte_counter = 0;
        }
    } else {
        // received was set without rx_cache[0] holding a header - unreachable
        // by the paths above, but if it ever happens the frame can never
        // validate, and leaving `received` set would stall the loop forever.
        found_header = false;
        received = false;
        crc_checked = false;
        byte_counter = 0;
    }
}

void Decode() {
    //float energyATemp, energyBTemp, currentATemp, currentBTemp;
    uint32_t voltageTemp, currentATemp, currentBTemp;
    int32_t  powerATemp=0, powerBTemp=0, energyATemp=0, energyBTem=0;
    float newEnergy = energy;

    voltageTemp    = rx_cache[12] << 16 | rx_cache[11] << 8 | rx_cache[10];     // V_RMS       unsigned
    voltage = ((float)voltageTemp / V_Conversion) / V_correction_factor;

    // if (strcmp((const char *) "S", (char *)READING_FROM_SHUNT_OR_CT)==0) {
    if (strcmp((const char *) "S", shuntOrCT.c_str())==0) {
    //   Serial.println("inside shunt");
        currentATemp  = rx_cache[6]  << 16 | rx_cache[5]  << 8 | rx_cache[4];      // I_RMS[A]    unsigned
        powerATemp    = rx_cache[18] << 16 | rx_cache[17] << 8 | rx_cache[16];     // WATT[A]      signed
        energyATemp   = rx_cache[24] << 16 | rx_cache[23] << 8 | rx_cache[22];     // CF_CNT[A]   unsigned

        current = ((float) currentATemp / I_Conversion_Shunt) / I_correction_factor_SHUNT / I_correction_factor2_SHUNT;
        newEnergy = (float)energyATemp * CF_Conversion_Shunt / E_correction_factor_SHUNT;
        if (bitRead(powerATemp, 23)) {
          powerATemp |= 0xFF000000;  // Extend sign bit
        }
        double  powerTemp = (((double)powerATemp / (double)W_Conversion_Shunt)) / W_correction_factor_SHUNT;
        if (isThreePhaseDevice) {
          // power    =  ((float)powerATemp / W_Conversion_Shunt) * sqrt(3)/ W_correction_factor_SHUNT;
          power    =  3 * powerTemp;
        } else {
          power    =  powerTemp;
        }

    } else {
    //    Serial.println("inside CT");

      currentATemp  = rx_cache[9]  << 16 | rx_cache[8]  << 8 | rx_cache[7];      // I_RMS[B]    unsigned
      powerATemp    = rx_cache[21] << 16 | rx_cache[20] << 8 | rx_cache[19];     // WATT[B]      signed
      energyATemp   = rx_cache[27] << 16 | rx_cache[26] << 8 | rx_cache[25];     // CF_CNT[B]   unsigned

      current = ((float) currentATemp / I_Conversion_CT)/ I_correction_factor_CT;
      newEnergy = ((float)energyATemp * CF_Conversion_CT)/ E_correction_factor_CT;
      
      if (bitRead(powerATemp, 23)) {
        powerATemp |= 0xFF000000;  // Extend sign bit
      }
      double  powerTemp = (((double)powerATemp / (double)W_Conversion_CT) ) / W_correction_factor_CT ; 
      if (isThreePhaseDevice) {
        // power = powerTemp * sqrt(3);
        power = 3 * powerTemp;
      } else {
        power = powerTemp;
      }

      unsigned long currentMeasureTime = millis();

    }
      // CF_CNT plausibility: between two decoded frames the counter can only
      // advance ~0.014 kWh/sec even at a 50kW load, so a bigger step is a
      // corrupted-but-CRC-passing frame — keep the previous energy so a bad
      // value never reaches energyOnStart or the session accounting. A step
      // persisting 3 consecutive frames is real (chip reset, 24-bit rollover
      // or a long loop stall) and is accepted; the counter is absolute, so
      // nothing is lost by the short delay.
      static uint8_t energyRejects = 0;
      if (fabsf(newEnergy - energy) > 1.0f && energyRejects < 5) {
          energyRejects++;
          if (energyRejects == 1) {
              char msg[DEBUG_MESSAGE_SIZE];
              snprintf(msg, sizeof(msg),
                       "Implausible CF_CNT step rejected: new=%.3f prev=%.3f",
                       newEnergy, energy);
              logDebugAction(DebugAction::SYSTEM_FAILURE, msg);
          }
      } else {
          energy = newEnergy;
          energyRejects = 0;
      }
      decoded = true;
}

void readPowerFactor() {
    byte response[4];
    Serial2.write(READ_COMMAND);
    Serial2.write(0x0C);

    unsigned long startTime = millis();
    while (Serial2.available() < 4 && millis() - startTime < 100) {
    }

    if (Serial2.available() >= 4) {
        Serial2.readBytes(response, 4);
        int phaseAngleRaw = (response[1] << 8) | response[0];
        float phaseAngleDegrees = phaseAngleRaw * 180.0 / 32768.0;
        pf = cos(phaseAngleDegrees * PI / 180.0);
    }
}

int16_t readRegister16(uint8_t regAddress) {
    uint8_t frame[] = {0x55, regAddress, 0x00, 0x00, 0x00};
    frame[4] = ~(frame[0] + frame[1] + frame[2] + frame[3]);
    Serial2.write(frame, 5);
    delay(10);

    if (Serial2.available() >= 5) {
        uint8_t response[5];
        for (int i = 0; i < 5; i++) {
            response[i] = Serial2.read();
        }
        if (response[0] == 0xAA && ~(response[1] + response[2] + response[3]) == response[4]) {
            return (int16_t)((response[2] << 8) | response[3]);
        }
    }
    return 0;
}

void power_sensing_loop() {
    if (!received) {
        if (millis() - requestTime >= 500) {
            Request_Data();
        } else if (Serial2.available() >= 35) {
            Read_Data();
        }
    }

    if (received) {
        CRC_Check();
    }

    if (crc_checked) {
        Decode();
    }

    if (decoded) {
        found_header = false;
        received = false;
        crc_checked = false;
        decoded = false;
    } else if (received && (millis() - requestTime >= FRAME_STALL_TIMEOUT_MS)) {
        // Backstop: a frame was captured but never validated or decoded. While
        // `received` is set nothing re-requests and nothing reads more bytes, so
        // without this the state machine would sit on those bytes forever and
        // power readings would freeze until reboot (taking the low-voltage /
        // dry-run / overload cutouts with them). requestTime only advances in
        // Request_Data(), so this fires on a genuinely stuck frame.
        found_header = false;
        received = false;
        crc_checked = false;
        byte_counter = 0;
    }
}