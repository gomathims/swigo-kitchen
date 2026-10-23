#ifndef POWERSENSORBL0939_H
#define POWERSENSORBL0939_H

#include <ArduinoJson.h>

#define PACKET_HEADER        0x55
// A captured frame that has neither validated nor decoded within this long is
// treated as stuck and discarded, so the read state machine re-requests instead
// of stalling. Comfortably above the 500ms Request_Data() retry interval.
#define FRAME_STALL_TIMEOUT_MS 1000

// sending commands
#define READ_COMMAND         0x55
#define REQUEST_FULL         0xAA
#define WRITE_COMMAND        0xA5
#define REG_IA_FAST_RMS_CTRL 0x10
#define REG_MODE             0x18
#define REG_SOFT_RESET       0x19
#define REG_USR_WRPROT       0x1A
#define REG_TPS_CTRL         0x1B
#define REG_IB_FAST_RMS_CTRL 0x1E
extern bool volatile SENSOR_RESET_IN_PROGRESS;
// Conversion Factors are calculated as per the suggested circuit of BL0939 
//#define V_Conversion             17159
#define V_Conversion             17383.1603

#define I_Conversion_Shunt              266013
#define W_Conversion_Shunt             713
#define CF_Conversion_Shunt             0.000163

// Calculated for 1:2500 transformer and 2.8 ohm resister - calculated as per formula by Sivaraj Mar 2025
#define I_Conversion_CT               297934.7126  
#define W_Conversion_CT               985.601
#define CF_Conversion_CT              0.000163

// Correction factor calibrated by Yogesh Feb2025
#define I_correction_factor_SHUNT       1.0794
#define W_correction_factor_SHUNT         1.09502
#define E_correction_factor_SHUNT        1

#define I_correction_factor2_SHUNT       1.0427844
// Correction factors are as per calibration reference meter
#define V_correction_factor       1
#define I_correction_factor_CT    1
#define W_correction_factor_CT    0.8045
#define E_correction_factor_CT    1

// Global variables
extern float voltage;
extern float current;
extern float power;
extern float pf;
extern float energy;
extern bool isThreePhaseDevice;
extern bool isSlaveDevice;
extern uint8_t isFloatEnabled;
extern String shuntOrCT;

// Function declarations
void BL0939_setup();
void Request_Data();
void Read_Data();
void CRC_Check();
void Decode();
void readPowerFactor();
int16_t readRegister16(uint8_t regAddress);
void power_sensing_loop();
uint8_t BL0939_readCFDivider(uint8_t response[4]);
bool BL0939_isCFDividerOK(const uint8_t response[4], uint8_t count);
void BL0939_reconfigureCFDivider();

#endif // POWERSENSORBL0939_H