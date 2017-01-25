#include <SimpleTimer.h>
#define BLYNK_PRINT Serial      // Comment this out to disable prints and save space
#include <BlynkSimpleEsp8266.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <TimeLib.h>            // Used by WidgetRTC.h
#include <WidgetRTC.h>          // Blynk's RTC

#include <ESP8266mDNS.h>        // Required for OTA
#include <WiFiUdp.h>            // Required for OTA
#include <ArduinoOTA.h>         // Required for OTA

#define ONE_WIRE_BUS 0          // WeMos pin D3 w/ pull-up, ESP-01 pin
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

const char auth[] = "fromBlynkApp";
char ssid[] = "ssid";
char pass[] = "pw";

SimpleTimer timer;

WidgetTerminal terminal(V26);
WidgetRTC rtc;

double outsideTemp;         // Current outdoor temp
int outsideTempInt;

int dailyHigh = 0;   // Today's high temp (resets at midnight)
int dailyLow = 200;  // Today's low temp (resets at midnight)
int today;

int last24high, last24low;    // Rolling high/low temps in last 24-hours.
int last24hoursTemps[288];    // Last 24-hours temps recorded every 5 minutes.
int arrayIndex = 0;


void setup()
{
  Serial.begin(9600);
  Blynk.begin(auth, ssid, pass);

  //WiFi.softAPdisconnect(true); // Per https://github.com/esp8266/Arduino/issues/676 this turns off AP

  while (Blynk.connect() == false) {
    // Wait until connected
  }

  // START OTA ROUTINE
  ArduinoOTA.setHostname("Node05BY-ESP01");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  // END OTA ROUTINE

  rtc.begin();

  sensors.begin();
  sensors.setResolution(10);

  timer.setInterval(2000L, sendTemps);           // Temperature sensor polling interval
  timer.setInterval(300000L, recordHighLowTemps);
  timer.setTimeout(5000, setupArray);             // Sets entire array to temp at startup for a "baseline"

  //timer.setInterval(1000L, uptimeReport);
}

void loop()
{
  Blynk.run();
  timer.run();
  ArduinoOTA.handle();

  if (hour() == 00 && minute() == 00)
  {
    timer.setTimeout(61000L, resetHiLoTemps);
  }
}

BLYNK_WRITE(V27) // App button to report uptime
{
  int pinData = param.asInt();

  if (pinData == 0)
  {
    timer.setTimeout(16000L, uptimeSend);
  }
}

void uptimeSend()
{
  long minDur = millis() / 60000L;
  long hourDur = millis() / 3600000L;
  
  if (minDur < 121)
  {
    terminal.print(String("Node05 (BY): ") + minDur + " mins @ ");
    terminal.println(WiFi.localIP());
  }
  else if (minDur > 120)
  {
    terminal.print(String("Node05 (BY): ") + hourDur + " hrs @ ");
    terminal.println(WiFi.localIP());
  }
  
  terminal.flush();
}

void uptimeReport() {
  if (second() > 2 && second() < 7)
  {
    Blynk.virtualWrite(102, minute());
  }
}

void sendTemps()
{
  sensors.requestTemperatures(); // Polls the sensors

  outsideTemp = sensors.getTempFByIndex(0); // Gets first probe on wire in lieu of by address

  // Conversion of outsideTemp to outsideTempInt
  int xoutsideTempInt = (int) outsideTemp;
  double xOutsideTemp10ths = (outsideTemp - xoutsideTempInt);
  if (xOutsideTemp10ths >= .50)
  {
    outsideTempInt = (xoutsideTempInt + 1);
  }
  else
  {
    outsideTemp = xoutsideTempInt;
  }

  // Send temperature to the app display
  if (outsideTemp > 10)
  {
    Blynk.virtualWrite(12, outsideTemp);
  }
  else
  {
    Blynk.virtualWrite(12, "ERR");
  }

  if (outsideTemp < 80)
  {
    Blynk.setProperty(V12, "color", "#04C0F8"); // Blue
  }
  else if (outsideTemp >= 81 && outsideTemp <= 100)
  {
    Blynk.setProperty(V12, "color", "#ED9D00"); // Yellow
  }
  else if (outsideTemp > 100)
  {
    Blynk.setProperty(V12, "color", "#D3435C"); // Red
  }
}

void setupArray()
{

  for (int i = 0; i < 288; i++)
  {
    last24hoursTemps[i] = outsideTempInt;
  }

  last24high = outsideTempInt;
  last24low = outsideTempInt;
  dailyHigh = outsideTempInt;
  dailyLow = outsideTempInt;

  Blynk.setProperty(V12, "label", "EXT");

}

void recordHighLowTemps()
{

  if (arrayIndex < 288)
  {
    last24hoursTemps[arrayIndex] = outsideTempInt;
    ++arrayIndex;
  }
  else
  {
    arrayIndex = 0;
    last24hoursTemps[arrayIndex] = outsideTempInt;
    ++arrayIndex;
  }

  last24high = -200;
  last24low = 200;

  for (int i = 0; i < 288; i++)
  {
    if (last24hoursTemps[i] > last24high)
    {
      last24high = last24hoursTemps[i];
    }

    if (last24hoursTemps[i] < last24low)
    {
      last24low = last24hoursTemps[i];
    }
  }

  if (outsideTemp > dailyHigh)
  {
    dailyHigh = outsideTemp;
    Blynk.virtualWrite(5, dailyHigh);
  }

  if (outsideTemp < dailyLow && outsideTemp > 0)
  {
    dailyLow = outsideTemp;
    Blynk.virtualWrite(13, dailyLow);
  }

  Blynk.setProperty(V12, "label", String("EXT ") + last24high + "/" + last24low);  // Sets label with high/low temps.
}

void resetHiLoTemps()
{
    dailyHigh = 0;     // Resets daily high temp
    dailyLow = 200;    // Resets daily low temp
}
