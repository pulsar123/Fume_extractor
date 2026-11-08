void mqtt()
// Reading / sending MQTT message(s)
{
  if (MQTT_on)
  {
    // Receiving; calls callback() function internally:
    client.loop();

    // Re-publishing to OpenHAB
    if (mqtt_init || mqtt_refresh)
    {
      if (led1 == 0)
        client.publish(ROOT"/red_LED", "0");
        else
        client.publish(ROOT"/red_LED", "1");
      if (led2 == 0)
        client.publish(ROOT"/green_LED", "0");
        else
        client.publish(ROOT"/green_LED", "1");
      if (fan == 0)
      {
        client.publish(ROOT"/fan", "0");
        client.publish(ROOT"/time", " - ");
      }
        else
        {
        client.publish(ROOT"/fan", "1");
        client.publish(ROOT"/time", remaining_time_str);
        }
      t_pulse = millis();
    }

    mqtt_init = 0;
    mqtt_refresh = 0;

/*
    // "Pulse" functionality: publishing this every DT_PULSE to let OpenHAB know that the sensor is alive
    if (t_pulse == 0 || millis() - t_pulse > DT_PULSE)
    {
      client.publish(ROOT"/pulse", "1");
      t_pulse = millis();
    }
    */

  }


  return;
}
