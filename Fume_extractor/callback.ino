void callback(char* topic, byte* payload, unsigned int length)
// The function processing incoming MQTT commands.
// The command is in payload[0..length-1] char array.
{

/*
  // External command to be quiet, for DT_QUIET ms
  if (strcmp(topic, ROOT"/quiet") == 0)
    if (external_alarm == 1 || local_alarm == 1)
    {
      // Only local alarm can be silenced for some time:
      if (local_alarm)
      {
        quiet = 1;
        t_quiet = millis();
      }
      // External alarm is silenced, but would go off at any time later if/when triggered
      external_alarm = 0;
//??      local_alarm = 0;
#ifdef DEBUG
      Serial.println("External command to be quiet");
#endif
    }
*/

  if (strcmp(topic, ROOT"/red_button") == 0 && btn1_mqtt==0)
  {
    if ((char)payload[0] == '1')
    {
      btn1_mqtt = 1;
    }
  }

  if (strcmp(topic, ROOT"/green_button") == 0 && btn2_mqtt==0)
  {
    if ((char)payload[0] == '1')
    {
      btn2_mqtt = 1;
    }
  }

  if (strcmp(topic, "openhab/start") == 0 && (char)payload[0] == '1')
    // We received a signal that openhab server has just restarted, so will re-publish all mqtt states later, in mqtt()
  {
    mqtt_refresh = 1;
  }

  return;
}
