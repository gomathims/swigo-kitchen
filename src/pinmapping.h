#ifndef PINMAPPING_H       
#define PINMAPPING_H

#include "Arduino.h"

extern const uint8_t *active_pins;
// // Relay
// #define  RELAY_OUT 23

// // Switch
// #define  SWITCH_1 34
// #define  SWITCH_2 0
// #define  SWITCH_3 2
// #define  SWITCH_4 12
// #define  SWITCH_OFF 15

// // RTC
// #define SDA_PIN 4
// #define SCL_PIN 5

// // PZEM
// #define RX_PIN 16
// #define TX_PIN 17


 //*******For ESP32 S3 board *******

// Relay
// #define  RELAY_OUT 1

// // As of 23 July
// #define  SWITCH_1 11
// #define  SWITCH_2 12
// #define  SWITCH_3 13
// #define  SWITCH_4 14
// #define  SWITCH_OFF 20

// #define  FLOAT_SWITCH 45
// #define  FACTORY_RESET 21

// // LEDs
// #define  LED_1 6
// #define  LED_2 7
// #define  LED_3 8
// #define  LED_4 3
// #define  LED_SMART 46


// // RTC
// #define SDA_PIN 42
// #define SCL_PIN 41

// // Power sensor
// #define TXD_ENERGY 15
// #define RXD_ENERGY 16



//  //*******For ESP32 GEN2 board *******

const uint8_t PINS_LED_BLUE[] = {
    2,  // 0. RELAY_OUT 2
    15, // 1. RELAY_OUT_POWER_TAP 15
    99, // 2. dummy
    46, // 3. SWITCH_1 46
    10, // 4. SWITCH_2 10
    47, // 5. SWITCH_3 47
    45, // 6. SWITCH_4 45
    39, // 7. SWITCH_OFF 39
    18, // 8. FLOAT_SWITCH 18
    0,  // 9.  FACTORY_RESET 0

    3,  // 10. LED_1 3
    9,  // 11. LED_2 9
    21, // 12. LED_3 21
    48, // 13. LED_4 48
    1,  // 14. LED_SMART 1
    99, // 15. dummy
    99, // 16. dummy
    99, // 17. dummy

    6,  // 18. SDA_PIN 6
    7,  // 19. SCL_PIN 7

    5,  // 20. TXD_ENERGY 5
    4,  // 21. RXD_ENERGY 4
    99, // 22. dummy
    99, // 23. dummy
    99, // 24. dummy
    99, // 25. dummy
    99, // 26. dummy
    99, // 27. dummy
    99, // 28. dummy
    99, // 29. dummy
    99, // 30. dummy
    99, // 31. dummy
    99, // 32. dummy

    };
const uint8_t PINS_LED_RGB[] = {
    41,  // 0. RELAY_OUT 
    15, // 1. RELAY_OUT_POWER_TAP 
    99, // 2. dummy
    46, // 3. SWITCH_1 46
    10, // 4. SWITCH_2 10
    47, // 5. SWITCH_3 47
    45, // 6. SWITCH_4 45
    39, // 7. SWITCH_OFF 39
    18, // 8. FLOAT_SWITCH 18
    0,  // 9.  FACTORY_RESET 0

    3,  // 10. LED_1 3
    9,  // 11. LED_2 9
    21, // 12. LED_3 21
    48, // 13. LED_4 48
    99, // 14. dummy 
    42,  // 15. LED_RED 
    2,  // 16.  LED_GREEN
    1, // 17. LED_BLUE

    6,  // 18. SDA_PIN 6
    7,  // 19. SCL_PIN 7

    5,  // 20. TXD_ENERGY 5
    4,  // 21. RXD_ENERGY 4
    40, // 22. ZERO_CROSS 40
    99, // 23. dummy
    99, // 24. dummy
    99, // 25. dummy
    99, // 26. dummy
    99, // 27. dummy
    99, // 28. dummy
    99, // 29. dummy
    99, // 30. dummy
    99, // 31. dummy
    99, // 32. dummy
    };

const uint8_t PINS_LED_RGB_FLOATRELAY[] = {
    41,  // 0. RELAY_OUT 
    15, // 1. RELAY_OUT_POWER_TAP 15
    10, // 2. RELAY_OUT_FLOAT
    46, // 3. SWITCH_1 46
    14, // 4. SWITCH_2 14
    47, // 5. SWITCH_3 47
    45, // 6. SWITCH_4 45
    39, // 7. SWITCH_OFF 39
    18, // 8. FLOAT_SWITCH 18
    0,  // 9.  FACTORY_RESET 0

    3,  // 10. LED_1 3
    9,  // 11. LED_2 9
    21, // 12. LED_3 21
    48, // 13. LED_4 48
    99, // 14. dummy 
    42,  // 15. LED_RED
    2,  // 16. LED_GREEN
    1, // 17. LED_BLUE

    6,  // 18. SDA_PIN 6
    7,  // 19. SCL_PIN 7

    5,  // 20. TXD_ENERGY 5
    4,  // 21. RXD_ENERGY 4
    40, // 22. ZERO_CROSS 40

    13, // 23. W5500_CS  13  // Chip Select
    20, // 24. W5500_RST 20   // Reset
    8,  // 25. W5500_INT 8    // Interrupt (optional, can be used for advanced features)

    // Custom SPI pins
    35, // 26. SPI_MOSI 35   // Custom MOSI
    36, // 27. SPI_MISO 36   // Custom MISO
    37, // 28. SPI_SCLK 37   // SPI Clock (can also be customized)
    19, // 29. RELAY_OUT_POWER_TAP2 // Second power tap for star delta

    12, // 30. PHASE_FAILURE 12
    38, // 31. PHASE_SEQUENCE_REVERSE 38
    16, // 32. LEAK_DETECTION 16

    };
// Pointer to the active pin configuration


const uint8_t PINS_LED_RGB_FLOAT_WIFI_UPDATE[] = {
    41,  // 0. RELAY_OUT 
    15, // 1. RELAY_OUT_POWER_TAP 15
    10, // 2. RELAY_OUT_FLOAT
    46, // 3. SWITCH_1 46
    14, // 4. SWITCH_2 14
    47, // 5. SWITCH_3 47
    45, // 6. SWITCH_4 45
    39, // 7. SWITCH_OFF 39
    20, // 8. FLOAT_SWITCH 20
    0,  // 9.  FACTORY_RESET 0

    3,  // 10. LED_1 3
    9,  // 11. LED_2 9
    21, // 12. LED_3 21
    48, // 13. LED_4 48
    99, // 14. dummy 
    42,  // 15. LED_RED
    2,  // 16. LED_GREEN
    1, // 17. LED_BLUE

    6,  // 18. SDA_PIN 6
    7,  // 19. SCL_PIN 7

    5,  // 20. TXD_ENERGY 5
    4,  // 21. RXD_ENERGY 4
    40, // 22. ZERO_CROSS 40

    13, // 23. W5500_CS  13  // Chip Select
    99, // 24. W5500_RST 20   // Reset
    8,  // 25. W5500_INT 8    // Interrupt (optional, can be used for advanced features)

    // Custom SPI pins
    35, // 26. SPI_MOSI 35   // Custom MOSI
    36, // 27. SPI_MISO 36   // Custom MISO
    37, // 28. SPI_SCLK 37   // SPI Clock (can also be customized)
    17, // 29. RELAY_OUT_POWER_TAP2 // Second power tap for star delta

    12, // 30. PHASE_FAILURE 12
    38, // 31. PHASE_SEQUENCE_REVERSE 38
    16, // 32. LEAK_DETECTION 16

    };

// Define aliases for readability
#define RELAY_OUT active_pins[0]
#define RELAY_OUT_POWER_TAP active_pins[1]
#define RELAY_OUT_FLOAT active_pins[2]
#define SWITCH_1 active_pins[3]
#define SWITCH_2 active_pins[4]
#define SWITCH_3 active_pins[5]
#define SWITCH_4 active_pins[6]
#define SWITCH_OFF active_pins[7]
#define FLOAT_SWITCH active_pins[8]
#define FACTORY_RESET active_pins[9]

#define LED_1 active_pins[10]
#define LED_2 active_pins[11]   
#define LED_3 active_pins[12]
#define LED_4 active_pins[13]
#define LED_SMART active_pins[14]

#define LED_RED active_pins[15]
#define LED_GREEN active_pins[16]
#define LED_BLUE active_pins[17]

#define SDA_PIN active_pins[18]
#define SCL_PIN active_pins[19]

#define TXD_ENERGY active_pins[20]
#define RXD_ENERGY active_pins[21]
#define ZERO_CROSS active_pins[22]

#define W5500_CS active_pins[23]
#define W5500_RST active_pins[24]
#define W5500_INT active_pins[25]

#define SPI_MOSI active_pins[26]
#define SPI_MISO active_pins[27]
#define SPI_SCLK active_pins[28] 

#define RELAY_OUT_POWER_TAP2  active_pins[29]  

#define PHASE_FAILURE active_pins[30]
#define PHASE_SEQUENCE_REVERSE active_pins[31]
#define LEAK_DETECTION active_pins[32]



/* 
#ifdef LED_NORMAL
    // Relay
    #define  RELAY_OUT 2
    #define  RELAY_OUT_POWER_TAP 15

    // Switches
    #define  SWITCH_1 46
    #define  SWITCH_2 10
    #define  SWITCH_3 47
    #define  SWITCH_4 45
    #define  SWITCH_OFF 39
    #define  FLOAT_SWITCH 18
    #define  FACTORY_RESET 0

    // LEDs
    #define  LED_1 3
    #define  LED_2 9
    #define  LED_3 21
    #define  LED_4 48
    #define  LED_SMART 1

    // RTC
    #define SDA_PIN 6
    #define SCL_PIN 7

    // Power sensor
    #define TXD_ENERGY 5
    #define RXD_ENERGY 4

#elif defined(LED_RGB)
    // Relay
    #define  RELAY_OUT 41
    #define  RELAY_OUT_POWER_TAP 15

    // Switches
    #define  SWITCH_1 46
    #define  SWITCH_2 10
    #define  SWITCH_3 47
    #define  SWITCH_4 45
    #define  SWITCH_OFF 39
    #define  FLOAT_SWITCH 18
    // #define  FACTORY_RESET EN

    //LEDs
    #define  LED_1 3
    #define  LED_2 9
    #define  LED_3 21
    #define  LED_4 48

    #define  LED_RED 42
    #define  LED_GREEN 2
    #define  LED_BLUE 1

    // RTC
    #define SDA_PIN 6
    #define SCL_PIN 7

    // Power sensor
    #define TXD_ENERGY 5
    #define RXD_ENERGY 4
    #define ZERO_CROSS 40

#elif defined(LED_RGB_FLOATRELAY)
     // Relay
     #define  RELAY_OUT 41
     #define  RELAY_OUT_POWER_TAP 15
     #define  RELAY_OUT_FLOAT 10
 
     // Switches
     #define  SWITCH_1 46
     #define  SWITCH_2 14
     #define  SWITCH_3 47
     #define  SWITCH_4 45
     #define  SWITCH_OFF 39
     #define  FLOAT_SWITCH 18
     // #define  FACTORY_RESET EN
 
     //LEDs
     #define  LED_1 3
     #define  LED_2 9
     #define  LED_3 21
     #define  LED_4 48
 
     #define  LED_RED 42
     #define  LED_GREEN 2
     #define  LED_BLUE 1
 
     // RTC
     #define SDA_PIN 6
     #define SCL_PIN 7
 
     // Power sensor
     #define TXD_ENERGY 5
     #define RXD_ENERGY 4
     #define ZERO_CROSS 40
        

    // W5500 pins
    #define W5500_CS  13  // Chip Select
    #define W5500_RST 20   // Reset
    #define W5500_INT 8    // Interrupt (optional, can be used for advanced features)

    // Custom SPI pins
    #define SPI_MOSI 35   // Custom MOSI
    #define SPI_MISO 36   // Custom MISO
    #define SPI_SCLK 37   // SPI Clock (can also be customized)

 
#endif */

#endif