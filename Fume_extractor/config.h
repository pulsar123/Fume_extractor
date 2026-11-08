
//#define DEBUG  // Debugging mode. Prints debugging info to the serial interface

/* Personal info (place the following lines in a separate file, private.h, uncomment all the lines, and replace xxxx with your personal details):
  const char* ssid = "xxx";
  const char* password = "xxxx";
  const char* mqtt_server = "xxxx";
*/
#include "private.h" //  Make sure you create this file first (see the lines above)

#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)

// The root names for MQTT topics
// To talk to other sensors:
#define ROOT "fume_extractor"
// To talk to OpenHAB:
#define ROOT_ID ROOT STRINGIZE(SENSOR_ID)
/*
  For the "openhab/start" thing to work, one needs to have the OpenHab to publish "1" (followed by "0") in this topic at startup.
  Under Windows this can be accomplished by adding this line before the last line of openhab's start.bat file:

  start /b C:\openHAB2\mqtt_start.bat >nul

  and creating a new file mqtt_start.bat with the following content:

  timeout /t 20 /nobreak
  C:\mosquitto\mosquitto_pub.exe -h 127.0.0.1 -t "openhab/start" -m "1"
  C:\mosquitto\mosquitto_pub.exe -h 127.0.0.1 -t "openhab/start" -m "0"
*/


// Port expander initial state:
// For MCP23S17 expander; ports numbering is 16, 15, ...,8, ..., 1 (B7, B6, ..., A7, ..., A0):
s16 IO_MODE =   0B0000000000011011;  // 1: input; 0: output
s16 IO_PULLUP = 0B0000000000011011;

/*
    Pins: in []: native MCP pins (1...16)
     - D3, 5, 6, 7: SPI (for MCP23S17)
     - D1, D2, B0, 1, 2, 3: stepper motor driver (STEP, DIR, SLP, EN, M0...M2) [D1, D2, 8, 9, 10, 11, 12]
     - b4, b5: two LEDs [13, 14]
     - k1, k2: two end switches [1, 2]
     - k3: MOSFET control [3]
     - k4, k5: two buttons [4, 5]
    */

const long DIR = -1;  // 1: direct movements; -1: inverse movements

// Stepper driver (DRV8825)
// Can only use native pins for stepping and direction, as the expander pins are much slower:
const byte PIN_STEP = D1;
const byte PIN_DIR = D2;
// LOW: enable motor; HIGH: disable motor (to save energy):
const byte EPIN_ENABLE = 9; // Expander B0
// HIGH: enable motor; LOW: disable motor; use delay(1) after HIGH
const byte EPIN_SLEEP = 8; // Expander A7
// Microstepping control:
const byte EPIN_M0 = 10; // Expander B1
const byte EPIN_M1 = 11; // Expander B2
const byte EPIN_M2 = 12; // Expander B3

// Pins to read digital input from the two limiting switches (normally HIGH; LOW when limiters are triggered)
// Make sure to solder a 3-10 nF capacitor between this pin and the ground pin, on the microcontroller, 
// to reduce reading noise from the cable
const byte EPIN_LIMITER1 = 2;
const byte EPIN_LIMITER2 = 1;

// Two push buttons
const byte EPIN_BTN1 = 5;
const byte EPIN_BTN2 = 4;

const byte EPIN_LED1 = 14;  // Green LED; expander B4
const byte EPIN_LED2 = 13;  // Red LED; expander B5

// MOSFET (fan) control:
const byte EPIN_FAN = 3;

// (https://wiki.wemos.cc/products:d1:d1_mini)

// Number of full steps per rotation for the stepper motor:
const s32 MOTOR_STEPS = 200;
// M0, M1, M2 values determine the number of microsteps per step
const byte MOTOR_M0 = HIGH; //LOW
const byte MOTOR_M1 = HIGH;
const byte MOTOR_M2 = HIGH; //LOW
// Number of microsteps in a step (corresponding to the above M0, M1, M2 values):
const s32 N_MICROSTEPS = 16;
const short STEP_LOW_DT = 2; // 2

// The ratio is 8.89 microsteps per motor degree, or 88.9 microsteps per air flap degree
const long SPAN = 10000/16*N_MICROSTEPS; // Number of microsteps to move from one limiting switch to another, with a bit of extra
const float MaxSpeed = 4000/16*N_MICROSTEPS; // Maximum allowed speed, microsteps peer second; 1000: 11 flap degrees per second
const float x_break = 200/16*N_MICROSTEPS; // Breaking distance, in microsteps
const float a_break = MaxSpeed*MaxSpeed/(2*x_break); // Breaking deceleration
const float AccelRoot = sqrt(a_break); // Acceleration when starting movements
const float DecelRoot1 = sqrt(a_break); // Deceleration near switch1
const float DecelRoot2 = sqrt(a_break); // Deceleration near switch2

// For how long the fan will run:
const unsigned long int DT_FAN1 = 60 * 60000; // Timer for position 1 (minutes -> milliseconds)
const unsigned long int DT_FAN2 = 10 * 60000; // Timer for position 2 

const unsigned long int DT_DEBOUNCE = 100; // Physical switch debounce time in ms

// Not used:
const unsigned long int DT_PULSE = 60000; // Interval between publishing /pulse commands to MQTT, ms

//+++++++++++++++++++++++++++++ Normally nothing should be changed below ++++++++++++++++++++++++++++++++++++++++++++++++++

WiFiClient espClient;
PubSubClient client(espClient);
static WiFiUDP udp;

#ifdef DEBUG
char tmp[256];
#endif
byte led1, led2, limiter1_state, limiter2_state, limiter1_state_old, limiter2_state_old, fan;
byte btn1_state, btn2_state, btn1_state_old, btn2_state_old;
byte moving, state, fan_mode;
byte position; // 0: no limiter switches triggered; 1: switch is triggered 1; 2: switch 2
int dest, dest1; // 0: nothing; 1: destination 1; 2: destination 2
unsigned long int t, t0, t_led1, t_led2, t_lim1, t_lim2, t_btn1, t_btn2, t_fan, dt_fan, t_pulse, t_mqtt;
byte WiFi_on, MQTT_on;
byte mqtt_init;
byte mqtt_refresh;
char remaining_time_str[11]; // Format: "0h 25m 32s"
unsigned long int remaining_time, remaining_time_old; // In seconds
byte btn1_mqtt, btn2_mqtt;
