void connections()
// Establishing and re-establishing WiFi and MQTT connections
{

  int status = WiFi.status();

  // Taking care of WiFi connection (trying to reconnect when lost)
  if (WiFi_on == 0 && status == WL_CONNECTED)
  {
    WiFi_on = 1;
    digitalWrite(LED_BUILTIN, LOW);
#ifdef DEBUG
    Serial.println("WiFi on");
#endif
  }

  if (WiFi_on == 1 && status != WL_CONNECTED)
    // If we just lost WiFi (will have to reconnect MQTT as well):
  {
    WiFi_on = 0;
    MQTT_on = 0;
    mqtt_init = 1;
    WiFi.begin(ssid, password);
    digitalWrite(LED_BUILTIN, HIGH);
#ifdef DEBUG
    Serial.println("WiFi off");
#endif
  }


  if (WiFi_on == 1 && !client.connected())
    // If we just lost MQTT connection (WiFi is still on), or not connected yet after WiFi reconnection:
  {
    MQTT_on = 0;
    mqtt_init = 1;
#ifdef DEBUG
    Serial.println("MQTT off");
#endif
  }

  // Taking care of MQTT connection (reconnecting when lost and only if WiFi is connected)
  if (WiFi_on == 1 && MQTT_on == 0)
  {
    if (client.connect(ROOT_ID))
    {
      // Subscribing (listening) to the following MQTT topics:
      client.subscribe(ROOT"/red_button");
      client.subscribe(ROOT"/green_button");
      client.subscribe("openhab/start");
      MQTT_on = 1;
      t_pulse = 0;
#ifdef DEBUG
      Serial.println("MQTT on");
#endif
    }
  }


  return;
}
