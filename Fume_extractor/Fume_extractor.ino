/*
    Fume Extractor, by Sergey Mashchenko (c) 2026

    Uses WiFi to connect to a MQTT server on the local network (which in turn can be integrated with OpenHAB web server,
    for full smart home integration).

    Specifically, add these lines to your openhab\conf\items\my_home.items file:
Switch FE_red_button "" <aaa> {mqtt=">[broker:fume_extractor/red_button:command:ON:1],>[broker:fume_extractor/red_button:command:OFF:0]"}
Switch FE_green_button "" <aaa> {mqtt=">[broker:fume_extractor/green_button:command:ON:1],>[broker:fume_extractor/green_button:command:OFF:0]"}
String FE_red_LED "" <red_LED> {mqtt="<[broker:fume_extractor/red_LED:state:MAP(onoff.map)]"}
String FE_green_LED "" <green_LED> {mqtt="<[broker:fume_extractor/green_LED:state:MAP(onoff.map)]"}
String FE_fan "" <fan> {mqtt="<[broker:fume_extractor/fan:state:MAP(onoff.map)]"}
String FE_time "Remaining time: [%s]" <empty> {mqtt="<[broker:fume_extractor/time:state:default]"}

  Also add these lines to openhab\conf\sitemaps\my_home.sitemap file:
Text item=FE_fan label="Fume extractor"
   {
     Frame 
       {
        Text item=FE_red_LED
        Switch item=FE_red_button mappings=[ON="Soldering"]
       }
     Frame 
       {
        Text item=FE_green_LED
        Switch item=FE_green_button mappings=[ON="3D printer"]
       }
     Frame
       {
        Text item=FE_fan
        Text item=FE_time
       }
   }

    Create file openhab\conf\transform\onoff.map with the following two lines:
0=OFF
1=ON

    Copy the *.png icons from this distro to openhab\conf\icons\classic

    I use Mosquitto (MQTT server for Windwos) and Apache web server. I use OpenHAB with mqtt plugin.

     All the configurable parameters are in two files - config.h, and private.h (the latter should be created by you; see config.h for
     instructions).

     To install the ESP8266 board, (using Arduino IDE):

  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"

  Libraries used:
  https://github.com/knolleary/pubsubclient
  https://github.com/ekstrand/ESP8266wifi
  https://www.airspayce.com/mikem/arduino/AccelStepper/index.html  Use the latest version 1.66 as zip file, don't use Library manager version
  https://github.com/n0mjs710/MCP23S17

*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <AccelStepper.h>

#define PIN_MCP_CS D3  // Defining a custom pin for controlling the MCP chip
#include  <MCP23S17.h>
#include "config.h"
MCP iochip(0, PIN_MCP_CS);

AccelStepper stepper(AccelStepper::DRIVER, PIN_STEP, PIN_DIR);

//---------------------------------------------------------------------------------------------------------------------

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  delay(100);
#endif

  WiFi_on = 0;
  MQTT_on = 0;

  iochip.begin(); // Initializing the port expander
  iochip.pinMode    (IO_MODE);
  iochip.pullupMode (IO_PULLUP);
  iochip.inputInvert(0B0000000000000000); // Likely not needed

  led1 = LOW;
  led2 = LOW;
  position = 0;
  limiter1_state = iochip.digitalRead(EPIN_LIMITER1); // reading the current state of the physical switch
  limiter1_state_old = limiter1_state;
  if (limiter1_state == 0)
  {
    position = 1;
    led1 = HIGH;
  }
  limiter2_state = iochip.digitalRead(EPIN_LIMITER2); // reading the current state of the physical switch
  limiter2_state_old = limiter2_state;
  if (limiter2_state == 0)
  {
    position = 2;
    led2 = HIGH;
  }

  btn1_state = iochip.digitalRead(EPIN_BTN1);
  btn1_state_old = btn1_state;
  btn2_state = iochip.digitalRead(EPIN_BTN2);
  btn2_state_old = btn2_state;

  iochip.digitalWrite(EPIN_SLEEP, LOW); //disable motor

  pinMode(LED_BUILTIN, OUTPUT);

  iochip.digitalWrite(EPIN_LED1, led1);
  iochip.digitalWrite(EPIN_LED2, led2);
  digitalWrite(LED_BUILTIN, HIGH);

#ifdef DEBUG
  delay(1000);
  Serial.println("Starting code");
#endif

#ifdef DEBUG
  Serial.println("Setup done");
#endif

  // Setting the requested microstepping mode:
  iochip.digitalWrite(EPIN_M0, MOTOR_M0);
  iochip.digitalWrite(EPIN_M1, MOTOR_M1);
  iochip.digitalWrite(EPIN_M2, MOTOR_M2);

  fan = LOW;
  iochip.digitalWrite(EPIN_FAN, fan); // Fan is off
    
  t0 = millis();
  t = t0;
  t_lim1 = 0;
  t_lim2 = 0;
  t_led1 = t0;
  t_led2 = t0;
  t_btn1 = t0;
  t_btn2 = t0;
  t_pulse = 0;
  remaining_time = 0;
  remaining_time_old = 0;
  sprintf(remaining_time_str, "");
  
  moving = false;
  dest = 0;
  dest1 = 0;
  fan_mode = 0;

  stepper.setMaxSpeed(MaxSpeed);

  // WiFi / MQTT initialization (non-blocking):
#ifdef DEBUG
  Serial.println("WiFi mode");
#endif
  WiFi.mode(WIFI_STA);
#ifdef DEBUG
  Serial.println("WiFi begin");
#endif
  WiFi.begin(ssid, password);
#ifdef DEBUG
  Serial.println("set server mqtt");
#endif
  client.setServer(mqtt_server, 1883);
#ifdef DEBUG
  Serial.println("set callback");
#endif
  client.setCallback(callback);
#ifdef DEBUG
  Serial.println("Setup done");
#endif

  t_mqtt = t0;
  mqtt_init = 1;
  mqtt_refresh = 0;
  btn1_mqtt = 0;
  btn2_mqtt = 0;  

}

//---------------------------------------------------------------------------------------------------------------------

void loop() 
{

  if (dest != 0 && moving == false)
  // Initiate motion
  {
    iochip.digitalWrite(EPIN_SLEEP, HIGH); //enable motor
    delay(1); // required after coming back from sleep
    stepper.setAccelerationRoot(AccelRoot);
    stepper.move(DIR*(2*dest-3)*SPAN);

    moving = true;
    dest1 = dest;
    dest = 0;
    if (fan_mode == 2)
    // If fan is still running when we initiate motion, immediately stop the fan, change its mode
    {
      fan_mode = 0;
      fan = LOW;
      iochip.digitalWrite(EPIN_FAN, fan); // Fan is off
      if (MQTT_on)
        client.publish(ROOT"/fan", "0");
      remaining_time = 0;
      sprintf(remaining_time_str, " - ");
    }
  }

  if (moving)
  {
    stepper.run();
    if (stepper.isRunning() == false)
    // Motor just stopped
    {
      iochip.digitalWrite(EPIN_SLEEP, LOW); //disable motor
      moving = false;
      if (position == 0)
      // A problem: we stopped before triggering a limiting switch!
      {      
      }
      else
      {
        fan_mode = 1; // Fan can be turned on now, if applicable
      }
    }
  }

  // Limiting switches should only be analyzed when moving:
  if (moving)
  {
    // Processing the two limiting switches:
    t = millis();
    limiter1_state = iochip.digitalRead(EPIN_LIMITER1);
    // Debouncing the state change of the switch:
    if (limiter1_state != limiter1_state_old && t-t_lim1>DT_DEBOUNCE)
    {
      t_lim1 = t;
      limiter1_state_old = limiter1_state;
      if (limiter1_state == 0 && position == 0)
      {
        stepper.setAccelerationRoot(DecelRoot1);
        stepper.stop();
        position = 1;
        led1 = 1;
        iochip.digitalWrite(EPIN_LED1, led1);
        if (MQTT_on)
          client.publish(ROOT"/red_LED", "1");
      }
      if (limiter1_state == 1 && position == 1)
      {
        position = 0;
        led1 = 0;
        iochip.digitalWrite(EPIN_LED1, led1);
        if (MQTT_on)
          client.publish(ROOT"/red_LED", "0");
      }
    }
  
    t = millis();
    limiter2_state = iochip.digitalRead(EPIN_LIMITER2);
    // Debouncing the state change of the switch:
    if (limiter2_state != limiter2_state_old && t-t_lim2>DT_DEBOUNCE)
    {
      t_lim2 = t;
      limiter2_state_old = limiter2_state;
      if (limiter2_state == 0 && position == 0)
      {
        stepper.setAccelerationRoot(DecelRoot2);
        stepper.stop();
        position = 2;
        led2 = 1;
        iochip.digitalWrite(EPIN_LED2, led2);
        if (MQTT_on)
          client.publish(ROOT"/green_LED", "1");
      }
      if (limiter2_state == 1 && position == 2)
      {
        position = 0;
        led2 = 0;
        iochip.digitalWrite(EPIN_LED2, led2);
        if (MQTT_on)
          client.publish(ROOT"/green_LED", "0");
      }
    }
  }

  if (moving)
    return;
    
  // We can only get here if the motor is not moving

  // Processing WiFi commands
  // Establishing and re-establishing WiFi and MQTT connections:
  connections();
  // Reading / sending MQTT message(s):
  mqtt();

  // Fan processing
  t = millis();
  if (fan_mode == 1 && position != 0)
  {
    t_fan = t; // Time when fan was turned on
    fan = HIGH;
    iochip.digitalWrite(EPIN_FAN, fan); // Fan is on
    if (MQTT_on)
      client.publish(ROOT"/fan", "1");
    if (position == 1)
      dt_fan = DT_FAN1;
      else
      dt_fan = DT_FAN2;
    fan_mode = 2; // This mode means the fan is running
  }

  if (fan_mode == 2 && position != 0 && t-t_fan > dt_fan)
  {
    fan = LOW;
    iochip.digitalWrite(EPIN_FAN, fan); // Fan is off
    if (MQTT_on)
    {
      client.publish(ROOT"/fan", "0");
      client.publish(ROOT"/time", " - ");
    }
    fan_mode = 0;
    remaining_time = 0;
    sprintf(remaining_time_str, "");
  }

  if (fan_mode == 2)
  {
    remaining_time = (dt_fan-t+t_fan) / 1000; // In seconds
    if (remaining_time != remaining_time_old)
    // Entering here once a second
    {
      remaining_time_old = remaining_time;
      int hrs = remaining_time / 3600;
      unsigned long int t2 = remaining_time % 3600;
      int mins = t2 / 60;
      int secs = t2 % 60;
      sprintf(remaining_time_str, "%dh %02dm %02ds", hrs, mins, secs); 
      if (MQTT_on == 1)
        client.publish(ROOT"/time", remaining_time_str);
    }
  }
  
  // Processing the two buttons (debounced):
  t = millis();
  btn1_state = iochip.digitalRead(EPIN_BTN1);
  if (btn1_mqtt == 1)
    btn1_state = 0; // Acting upon online command
  if (btn1_state != btn1_state_old && (t-t_btn1>DT_DEBOUNCE || btn1_mqtt==1))
  {
    t_btn1 = t;
    btn1_state_old = btn1_state;
    if (btn1_state==0 && dest==0 && position!=1)
    // Received the command to travel to the opposite side
    {
      dest = 1;
    }
    if (btn1_state==0 && position==1)
    {
      // Pressing the corresponding button when we are already at that position
      // will turn on the fan if it's not running, and will turn it off otherwise
      if (fan_mode == 0)
        fan_mode = 1; // A signal to turn on the fan immediately
      if (fan_mode == 2)
        dt_fan = 0; // A signal to turn off the fan immediately
    }
  }

  btn2_state = iochip.digitalRead(EPIN_BTN2);
  if (btn2_mqtt == 1)
    btn2_state = 0; // Acting upon online command
  if (btn2_state != btn2_state_old && (t-t_btn2>DT_DEBOUNCE || btn2_mqtt==1))
  {
    t_btn2 = t;
    btn2_state_old = btn2_state;
    if (btn2_state==0 && dest==0 && position!=2)
    // Received the command to travel to the opposite side
    {
      dest = 2;
    }
    if (btn2_state==0 && position==2)
    {
      // Pressing the corresponding button when we are already at that position
      // will turn on the fan if it's not running, and will turn it off otherwise
      if (fan_mode == 0)
        fan_mode = 1; // A signal to tun on the fan immediately
      if (fan_mode == 2)
        dt_fan = 0; // A signal to turn off the fan immediately
    }
  }



  btn1_mqtt = 0;
  btn2_mqtt = 0;  

}
