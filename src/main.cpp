/*
***** For Arduino, install ESP32 Board by copying lthe ink:
* https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
*
***** Make sure to select the "NodeMCU-32S" Board in "Tools > Board" before uploading!
*
***** Also make sure to click "ESP32 Sketch Data Upload" to put the HTML files into the SPIFFS!

Core 1 - Runs webserver async, handles OTA, fetches weather, manage DHT sensor, handles OLED
Core 0 - plays piezo sometimes, handles button stop alarm

TODO: data validation in html site, stresstest

User Manual: https://docs.google.com/document/d/13_B62WKgXSCFKncpiR5TjrH5HiC2lpAX2O4p0_C5vBc/view
Public Instructions: https://docs.google.com/document/d/1FX94p-VmW4RWo_-B2vAzvLYiPV2Ckn8iARUBrKDIruo/view

[For CEC Students Only]
Hardware Instructions: https://docs.google.com/document/d/1vp5-Bf6bVt2BgmSVsTq9r9tmfMt-iGXgfSj-xWOTx8A/edit
Code Instructions: https://docs.google.com/presentation/d/1Ys43l4QkvRx-jvUKnLD3N2onDkJiMPiqpl9rWZ5XfFM/edit
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// ------------------------------------------ SETUP DISPLAYS ------------------------------------------

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "bitmaps.h"
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_MOSI 16
#define OLED_CLK 22
#define OLED_DC 26
#define OLED_CS 21
#define OLED_RESET 14
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

#include <TM1637Display.h>
#define TM_CLK 32
#define TM_DIO 33
TM1637Display segdisplay = TM1637Display(TM_CLK, TM_DIO);
// segdisplay.showNumberDecEx(number, 0b01000000, leading_zeros, length, position)
int segBrightness = 0; // from 0 to 7

void drawInfoBar();
void drawAPIWeather();
void drawSensorData();
void drawFace();
void drawScreen();

// ------------------------------------------ SETUP WIFI ------------------------------------------

#include <WiFi.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
// #include <ESPAsyncWebSrv.h> // https://randomnerdtutorials.com/esp32-async-web-server-espasyncwebserver-library/ // arduino IDE version. idk why anyone would want to use arduino ide over platformio even as a beginner.
#include <ESPAsyncWebServer.h> // https://randomnerdtutorials.com/esp32-async-web-server-espasyncwebserver-library/
#include <Arduino_JSON.h>

AsyncWebServer server(80);
String main_processor(const String &var);
String alarm_processor(const String &var);
String settings_processor(const String &var);

String jsonBuffer;
String httpGETRequest(const char *serverName);

// ------------------------------------------ SETUP INPUTS/OUTPUTS ------------------------------------------

#include <DHT.h>
#define DHT_SENSOR_PIN 27 // ESP32 pin GPIO27 connected to DHT11 sensor
#define DHT_SENSOR_TYPE DHT22
DHT dht_sensor(DHT_SENSOR_PIN, DHT_SENSOR_TYPE);
float dht_temp;
float dht_hum;
float dht_hi;
void readDHT();
void readWeatherAPI();

float temperature, windspeed;
int pressure, humidity;
String weather_main, weather_desc;
int weather_icon;

#define ONBOARD_LED 2
#define BUTTON_PIN 5
#define PRESSED 0
#define NOT_PRESSED 1
bool buttonPressed = false;
void IRAM_ATTR isr();
// for debouncing
unsigned long button_time = 0;
unsigned long last_button_time = 0;
int debounce_time = 333;

#include "songs.h"
void playPiezo(void *pvParameters);
TaskHandle_t piezoTask;

// -------------------------------------------- SETUP TIMEKEEPING ---------------------------------------------

#include <ESP32Time.h>
#include "time.h"
#include "esp_sntp.h"
const char *ntp_server1 = "pool.ntp.org";
const char *ntp_server2 = "time.nist.gov";
int gmt_offset = 8 * 3600; // for GMT+8

ESP32Time rtc;

unsigned long prev_wifi_millis = 0;
unsigned long prev_temphum_millis = 0;
unsigned long prev_time_millis = 0;
unsigned long prev_display_millis = 0;

typedef struct
{
  int repeats;  // 0:daily; 1:weekly; 2:never
  tm alarmTime; // https://www.tutorialspoint.com/c_standard_library/time_h.htm
  int song;     // -1:random, [1,2,3]; s.t. if song == 0 means not initialized
  bool rang;    // resets to 0 every midnight
} alarminfo;

alarminfo alarmData[10]; // allow ten alarms
int numAlarms = 0;
int currSong = 0;
void printTM(tm t);
String getDay(int d);
String padZeros(int t);

// ------------------------------------------ SETUP MEMORY/VARIABLES ------------------------------------------

#include "SPIFFS.h"
#include <Preferences.h>
Preferences preferences;

char charbuf[1000];
String ssid, password;
String espmac;
String getESPMac();

String serverPath;
String city = "George Town", countryCode = "MY", openWeatherMapApiKey = "";

// ------------------------------------------ SETUP FUNCTION ------------------------------------------

void setup()
{
  Serial.begin(115200);
  espmac = getESPMac();

  pinMode(ONBOARD_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  digitalWrite(ONBOARD_LED, LOW);
  dht_sensor.begin();

  segdisplay.clear();
  segdisplay.setBrightness(segBrightness);

  const uint8_t hi[] = {
      SEG_B | SEG_C | SEG_E | SEG_F | SEG_G,
      SEG_E | SEG_F,
      SEG_D | SEG_G,
      SEG_A | SEG_B | SEG_C | SEG_D};
  segdisplay.setSegments(hi);

  sntp_set_time_sync_notification_cb([](struct timeval *t)
                                     { Serial.println("[TIME] Got time adjustment from NTP!"); });
  sntp_servermode_dhcp(1);

  configTime(gmt_offset, 0, ntp_server1, ntp_server2);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("[MODULE] SSD1306 allocation failed");
    delay(1000);
    ESP.restart();
  }
  Serial.println("[MODULE] Display intialized");
  display.setRotation(2);
  display.setTextSize(1);
  display.setTextColor(WHITE); // Draw white text

  // show CEC splash screen
  display.clearDisplay();
  display.drawBitmap(0, 0, bitmap_cec, 128, 64, 1);
  display.display();
  delay(5000);

  // Initialize SPIFFS
  if (!SPIFFS.begin(true))
  {
    display.clearDisplay();
    display.println("An Error has occurred while mounting SPIFFS");
    display.display();
    for (;;)
      ; // loop forever
  }

  preferences.begin("pref-mem", false);
  ssid = preferences.getString("ssid");
  password = preferences.getString("pwd");
  openWeatherMapApiKey = preferences.getString("apikey");
  city = preferences.getString("city");
  countryCode = preferences.getString("ccode");

  if (city.length() == 0) {
    city = "George Town";
    preferences.putString("city", city);
  }
  if (countryCode.length() == 0) {
    countryCode = "MY";
    preferences.putString("ccode", countryCode);
  }

  size_t alarmLen = preferences.getBytesLength("alarm");
  if (alarmLen == 0 || alarmLen % sizeof(alarminfo) || alarmLen > sizeof(alarmData))
  {
    Serial.print("[CODE] Invalid size of alarm array: ");
    Serial.println(alarmLen);
  }
  else
  {
    preferences.getBytes("alarm", alarmData, alarmLen);
    Serial.println("[CODE]: Read the following alarms: ");
    for (int i = 0; i < (alarmLen / sizeof(alarminfo)); i++)
    {
      if (alarmData[i].song != 0)
      {
        Serial.print("Time: ");
        printTM(alarmData[i].alarmTime);
        Serial.print(" | Repeats: ");
        Serial.print(alarmData[i].repeats);
        Serial.print(" | Song: ");
        Serial.println(alarmData[i].song);
        numAlarms += 1;
      }
    }
  }

  // try connecting first; if waited 60sec then open wifi connect
  if (ssid.length() != 0)
  {
    prev_wifi_millis = millis();
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Connecting");
    display.display();

    while ((millis() - prev_wifi_millis < 60000) && (WiFi.status() != WL_CONNECTED) && (digitalRead(BUTTON_PIN) == NOT_PRESSED))
    {
      delay(500);
      display.print(".");
      display.display();
    }
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.mode(WIFI_AP);

    IPAddress apIP = IPAddress(192, 168, 4, 1);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    // https://github.com/espressif/arduino-esp32/issues/1832

    WiFi.softAP(espmac);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("[WIFI] AP IP address: ");
    Serial.println(IP);
  }
  else
  {
    Serial.print("[WIFI] Connected to WiFi network with IP Address: ");
    Serial.println(WiFi.localIP());

    // update RTC
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
      rtc.setTimeStruct(timeinfo);
  }
  readDHT();
  readWeatherAPI();

  display.clearDisplay();
  drawInfoBar();
  drawAPIWeather();
  display.display();

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/main.html", String(), false, main_processor); });

  server.on("/alarm", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/alarm.html", String(), false, alarm_processor); });

  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/settings.html", String(), false, settings_processor); });

  server.on("/init", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("name") && request->hasParam("pwd")) {
      String inputName = request->getParam("name")->value();
      String inputPwd = request->getParam("pwd")->value();
      
      if (inputName.length() || inputPwd.length()) {
        sprintf(charbuf, "%s - %s", inputName, inputPwd);
        Serial.println(charbuf);

        if (ssid != inputName) preferences.putString("ssid", inputName);
        if (password != inputPwd) preferences.putString("pwd", inputPwd);
        Serial.println("[CODE] Updated SSID & pwd in preferences");

        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("Details received!");
        display.println("Restarting now...");
        display.display();

        delay(2000);
        ESP.restart();
      }
    }

    if (request->hasParam("apikey")) {
      String inputApiKey = request->getParam("apikey")->value();
      if (inputApiKey.length() && openWeatherMapApiKey != inputApiKey) {
        Serial.print("[CODE] Received API Key: "); 
        Serial.println(inputApiKey);

        openWeatherMapApiKey = inputApiKey;
        preferences.putString("apikey", inputApiKey);
      }
    }

    if (request->hasParam("city") && request->hasParam("ccode")) {
      String inputCity = request->getParam("city")->value();
      String inputCCode = request->getParam("ccode")->value();
      if (inputCity.length() && inputCCode.length()) {
        Serial.print("[CODE] Received Location: "); 
        Serial.print(inputCity);
        Serial.print(", ");
        Serial.println(inputCCode);

        city = inputCity;
        countryCode = inputCCode;
        preferences.putString("city", inputCity);
        preferences.putString("ccode", inputCCode);

        readWeatherAPI();
      }
    }

    if (request->hasParam("time")) {
      String inputTime = request->getParam("time")->value();
      if (inputTime.length() == 10) {
        Serial.print("[CODE] Received time: ");
        Serial.println(inputTime);
        
        // reset time
        rtc.setTime(inputTime.toInt());
      }
      else {
        Serial.println("[CODE] Epoch time is not 10 digits, weird...");
      }
    }

    if (request->hasParam("repeats") && request->hasParam("alarmtime") && request->hasParam("song")) {
      int inputRepeats = (request->getParam("repeats")->value()).toInt();
      String inputAlarm = request->getParam("alarmtime")->value();
      int inputSong = (request->getParam("song")->value()).toInt();

      if (inputAlarm.length()) {
        sprintf(charbuf, "[CODE] Received type %d alarm (%d): ", inputRepeats, inputSong);
        Serial.print(charbuf);
        Serial.println(inputAlarm);

        // append in array
        alarminfo newAlarm = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, false};
        newAlarm.repeats = inputRepeats;
        newAlarm.song = inputSong;

        
        if (inputRepeats == 2) { // example: 2017-06-01T08:30
          newAlarm.alarmTime.tm_min = inputAlarm.substring(14, 16).toInt();
          newAlarm.alarmTime.tm_hour = inputAlarm.substring(11, 13).toInt();
          newAlarm.alarmTime.tm_mday = inputAlarm.substring(8, 10).toInt();
          newAlarm.alarmTime.tm_mon = inputAlarm.substring(5, 7).toInt() - 1;
          newAlarm.alarmTime.tm_year = inputAlarm.substring(0, 4).toInt() - 1900;
        }
        else {
          newAlarm.alarmTime.tm_hour = inputAlarm.substring(0, 2).toInt();
          newAlarm.alarmTime.tm_min = inputAlarm.substring(3, 5).toInt();
          if (inputRepeats == 1) 
            newAlarm.alarmTime.tm_wday = inputAlarm.substring(6, 7).toInt();
        }
        
        // save in preferences
        alarmData[numAlarms] = newAlarm;
        numAlarms += 1;
        preferences.putBytes("alarm", alarmData, sizeof(alarmData));
      }
    }

    if (request->hasParam("alarmdel")) {
      int del = (request->getParam("alarmdel")->value()).toInt();
      
      for (int i=0; i<10; i++) {
        if (alarmData[i].song != 0) {
          if (del <= i) {
            if (i+1 < 10) alarmData[i] = alarmData[i+1];
            else alarmData[i] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
          }
        }
        else alarmData[i] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
      }
      numAlarms = max(numAlarms-1, 0);
      preferences.putBytes("alarm", alarmData, sizeof(alarmData));

      sprintf(charbuf, "[CODE] Deleted %d alarm, %d remaining", del, numAlarms);
      Serial.println(charbuf);
    }

    request->send(200, "text/plain", "OK"); });

  // Send a GET request to <ESP_IP>/gpio?output=<inputMessage1>&state=<inputMessage2>
  server.on("/gpio", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    // GET input1 value on <ESP_IP>/gpio?output=<inputMessage1>&state=<inputMessage2>
    if (request->hasParam("output") && request->hasParam("state")) {
      int inputPin = (request->getParam("output")->value()).toInt();
      int inputState = (request->getParam("state")->value()).toInt();
      digitalWrite(inputPin, inputState);

      sprintf(charbuf, "[GPIO] %d - Set to: %d - Running on Core %d", inputPin, inputState, xPortGetCoreID());
      Serial.println(charbuf);
    }

    if (request->hasParam("song")) {
      int inputSong = (request->getParam("song")->value()).toInt();
      currSong = 0;
      xTaskCreatePinnedToCore(
        playPiezo,    /* Task function. */
        "Play Piezo", /* name of task. */
        10000,         /* Stack size of task */
        (void*)inputSong,         /* parameter of the task */
        0,            /* priority of the task */
        &piezoTask,   /* Task handle to keep track of created task */
        0);           /* pin task to core 0 */
    }

    request->send(200, "text/plain", "OK"); });

  server.on("/slider", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    String inputMessage;
    // GET input1 value on <ESP_IP>/slider?value=<inputMessage>
    if (request->hasParam("value")) {
      int sliderval = (request->getParam("value")->value()).toInt();
      segBrightness = sliderval;
      segdisplay.setBrightness(sliderval);
      sprintf(charbuf, "[GPIO] Set 7seg brightness to %d", sliderval);
      Serial.println(charbuf);
    }
    request->send(200, "text/plain", "OK"); });

  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->redirect("/"); });

  server.begin();
  Serial.println("[WIFI] HTTP server started");

  attachInterrupt(BUTTON_PIN, isr, FALLING);
}

// ------------------------------------------ LOOP FUNCTION ------------------------------------------

// which interface should be displayed currently
int display_state = 0;

void loop()
{
  // update 7seg every second
  if (millis() - prev_time_millis > 1 * 1000)
  {
    segdisplay.showNumberDecEx(
        100 * rtc.getHour(1) + rtc.getMinute(),
        (rtc.getSecond() % 2 ? 0b01000000 : 0b00000000),
        true, 4, 0);
    Serial.print("[CODE] RTC Time: ");
    printTM(rtc.getTimeStruct());
    Serial.println();

    if (currSong == 0) // if nothing is playing now
    {
      for (int i = 0; i < numAlarms; i++)
      {
        alarminfo a = alarmData[i];
        if (a.song != 0 && !a.rang)
        {
          bool isTime = a.alarmTime.tm_hour == rtc.getHour(1) && a.alarmTime.tm_min == rtc.getMinute();

          if (a.repeats == 1)
            isTime = (isTime && a.alarmTime.tm_wday == rtc.getDayofWeek());
          if (a.repeats == 2)
            isTime = (isTime && a.alarmTime.tm_mday == rtc.getDay() && a.alarmTime.tm_mon == rtc.getMonth() && a.alarmTime.tm_year == (rtc.getYear() - 1900));

          if (isTime) // current time == alarm time
          {
            if (a.song == -1)
              currSong = (int)random(3);
            else
              currSong = a.song;
            rand();
            alarmData[i].rang = true;
            xTaskCreatePinnedToCore(
                playPiezo,    /* Task function. */
                "Play Piezo", /* name of task. */
                10000,        /* Stack size of task */
                NULL,         /* parameter of the task */
                0,            /* priority of the task */
                &piezoTask,   /* Task handle to keep track of created task */
                0);           /* pin task to core 0 */
          }
        }
      }
    }

    if (rtc.getHour(1) == 0 && rtc.getMinute() == 0)
      for (int i = 0; i < 10; i++)
        alarmData[i].rang = false;

    prev_time_millis = millis();
  }

  // Update temperature & humidity data, from local and from api every min
  if (millis() - prev_temphum_millis > 60 * 1000)
  {
    readDHT();
    readWeatherAPI();
    prev_temphum_millis = millis();
  }

  // update OLED every minute
  if (millis() - prev_display_millis > 60 * 1000 && currSong == 0)
  {
    display_state = (display_state + 1) % 3;

    display.clearDisplay();
    drawInfoBar();
    drawScreen();
    display.display();

    prev_display_millis = millis();
  }

  if (display_state != -1 && currSong != 0)
  {
    display_state = -1;

    display.clearDisplay();
    display.drawBitmap(64 - 25, 5, bitmap_alarm, 50, 50, WHITE);
    display.display();
  }

  if (buttonPressed)
  {
    Serial.println("[CODE] Button pressed, yay");
    buttonPressed = false;

    if (currSong != 0)
    {
      vTaskDelete(piezoTask);
      piezoTask = NULL;
      currSong = 0;
      Serial.println("[CODE] Stopped piezo");

      display_state = 0;
      display.clearDisplay();
      drawInfoBar();
      drawAPIWeather();
      display.display();
      prev_display_millis = millis();
    }
    else
    {
      display_state = (display_state + 1) % 3;

      display.clearDisplay();
      drawInfoBar();
      drawScreen();
      display.display();

      prev_display_millis = millis();
    }
  }
}

// ------------------------------------------ HELPER FUNCTIONS ------------------------------------------
String getESPMac()
{
  unsigned char mac_base[6] = {0};
  esp_efuse_mac_get_default(mac_base);
  esp_read_mac(mac_base, ESP_MAC_WIFI_STA);

  unsigned char mac_local_base[6] = {0};
  unsigned char mac_uni_base[6] = {0};
  esp_derive_local_mac(mac_local_base, mac_uni_base);

  char macstring[20];
  sprintf(macstring, "%02X:%02X:%02X:%02X:%02X:%02X", mac_base[0], mac_base[1], mac_base[2], mac_base[3], mac_base[4], mac_base[5]);

  sprintf(charbuf, "[CODE] ESP MAC Address: %s", macstring);
  Serial.println(charbuf);

  return String(macstring);
}

void printTM(tm t)
{
  sprintf(charbuf, "%d/%d/%d (%s), %s:%s:%s", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900, getDay(t.tm_wday), padZeros(t.tm_hour), padZeros(t.tm_min), padZeros(t.tm_sec));
  Serial.print(charbuf);
}

String padZeros(int t)
{
  if (t < 10)
    return ("0" + String(t));
  else
    return String(t);
}

String getDay(int d)
{
  switch (d)
  {
  case 0:
    return "Sunday";
  case 1:
    return "Monday";
  case 2:
    return "Tuesday";
  case 3:
    return "Wednesday";
  case 4:
    return "Thursday";
  case 5:
    return "Friday";
  case 6:
    return "Saturday";
  default:
    return "lol?";
  }
}

String httpGETRequest(const char *serverName)
{
  WiFiClient client;
  HTTPClient http;

  // Your Domain name with URL path or IP address with path
  http.begin(client, serverName);

  // Send HTTP POST request
  int httpResponseCode = http.GET();

  String payload = "{}";

  if (httpResponseCode > 0)
  {
    Serial.print("[WIFI] HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else
  {
    Serial.print("[WIFI] Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}

String main_processor(const String &var)
{
  if (var == "BUTTONPLACEHOLDER")
  {
    String buttons = "";
    buttons += "<h4>ESP32 Blue On Board LED</h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"2\" " + (digitalRead(ONBOARD_LED) ? String("checked") : String("")) + "><span class=\"switchslider\"></span></label>";
    return buttons;
  }

  if (var == "APIKEYPLACEHOLDER")
    if (openWeatherMapApiKey.length() == 32)
      return openWeatherMapApiKey;

  if (var == "SLIDERVALUE")
    return String(segBrightness);

  return String();
}

String alarm_processor(const String &var)
{
  if (var == "NUMALARMS")
  {
    return String(numAlarms);
  }
  if (var == "ALARMSPLACEHOLDER")
  {
    String currAlarms = "<div id=\"alarmlist\">";
    for (int i = 0; i < (sizeof(alarmData) / sizeof(alarminfo)); i++)
    {
      if (alarmData[i].song != 0)
      {
        currAlarms += "<div>";

        tm alarmtime = alarmData[i].alarmTime;
        int rep = alarmData[i].repeats; // 0:daily; 1:weekly; 2:never
        if (rep == 0)
          currAlarms += padZeros(alarmtime.tm_hour) + ":" + padZeros(alarmtime.tm_min) + " every day";
        if (rep == 1)
          currAlarms += padZeros(alarmtime.tm_hour) + ":" + padZeros(alarmtime.tm_min) + " every " + getDay(alarmtime.tm_wday);
        if (rep == 2)
          currAlarms += padZeros(alarmtime.tm_hour) + ":" + padZeros(alarmtime.tm_min) + " on " + padZeros(alarmtime.tm_mday) + "/" + padZeros(alarmtime.tm_mon + 1) + "/" + String(alarmtime.tm_year + 1900);

        currAlarms += "&nbsp;(Song: " + (alarmData[i].song == -1 ? "Random" : String(alarmData[i].song)) + ")&nbsp;";
        currAlarms += "<button type=\"submit\" onclick=\"deleteAlarm(" + String(i) + ")\">Delete</button>";
        currAlarms += "</div>";
      }
    }
    currAlarms += "</div>";

    return currAlarms;
  }

  return String();
}

String settings_processor(const String &var)
{
  if (var == "CURRWIFIPLACEHOLDER")
  {
    String info = "";
    if (ssid.length() != 0)
    {
      info += "<div id=\"currwifi\">" + ssid + " (" + password + "), ";
      if (WiFi.status() == WL_CONNECTED)
        info += "RSSI: " + String(WiFi.RSSI()) + "</div>";
      else
        info += "Not Connected</div>";
    }
    else
      info += "<div id=\"currwifi\">Not Found</div>";
    return info;
  }

  if (var == "CURRAPIPLACEHOLDER")
  {
    String info = "";
    if (openWeatherMapApiKey.length() == 32)
      info += openWeatherMapApiKey;
    else
      info += "Not found";
    return info;
  }

  if (var == "CURRLOCATIONPLACEHOLDER")
  {
    String info = String(city) + ", " + String(countryCode);
    return info;
  }

  // // this crashes ESP32 when visiting /settings ☠️☠️
  // if (var == "INPUTPLACEHOLDER")
  // {
  //   int n = WiFi.scanNetworks();
  //   Serial.println("[WIFI] WiFi scan done");
  //   String nearbyWifis = "<div><h3>" + String(n) + " networks found</h3>";
  //   if (n == 0)
  //     Serial.println("[WIFI] No networks found");
  //   else
  //   {
  //     nearbyWifis += "<select name=\"wifis\" id=\"wifis\">";
  //     sprintf(charbuf, "[WIFI] %d networks found:", n);
  //     Serial.println(charbuf);
  //     for (int i = 0; i < n; i++)
  //     {
  //       // Print SSID and RSSI for each network found
  //       String info = String(i + 1) + ": " + String(WiFi.SSID(i)) + " (" + String(WiFi.RSSI(i)) + ")" + ((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
  //       Serial.println(info);
  //       nearbyWifis += "<option value=\"" + String(WiFi.SSID(i)) + "\">" + info + "</option>";
  //     }
  //   }
  //   nearbyWifis += "</select></div><br>";
  //   return nearbyWifis;
  // }

  return String();
}

void readDHT()
{
  dht_temp = dht_sensor.readTemperature();
  dht_hum = dht_sensor.readHumidity();
  dht_hi = dht_sensor.computeHeatIndex(dht_temp, dht_hum, false);

  if (isnan(dht_temp) || isnan(dht_hum))
    Serial.println("[MODULE] Failed to read from DHT sensor!");
  else
  {
    sprintf(charbuf, "[MODULE] DHT READ: %.2fC, %.2f%, %.2fC", dht_temp, dht_hum, dht_hi);
    Serial.println(charbuf);
  }

  return;
}

void readWeatherAPI()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[CODE] Not connected, skip reading API");
    return;
  }
  if (openWeatherMapApiKey.length() != 32)
  {
    Serial.println("[CODE] Invalid API Key, skip reading API");
    return;
  }

  // TODO: put get request on the other core
  if (openWeatherMapApiKey.length() == 32)
    serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&APPID=" + openWeatherMapApiKey;

  jsonBuffer = httpGETRequest(serverPath.c_str());
  JSONVar jsObj = JSON.parse(jsonBuffer);

  if (JSON.typeof(jsObj) == "undefined")
  {
    Serial.println("[CODE] Parsing input failed!");
    return;
  }

  temperature = double(jsObj["main"]["temp"]);
  pressure = int(jsObj["main"]["pressure"]);
  humidity = int(jsObj["main"]["humidity"]);
  windspeed = double(jsObj["wind"]["speed"]);
  weather_main = String((const char *)jsObj["weather"][0]["main"]);
  weather_desc = String((const char *)jsObj["weather"][0]["description"]);
  weather_icon = String((const char *)jsObj["weather"][0]["icon"]).toInt();

  sprintf(charbuf, "[CODE] Temperature: %.2f  Pressure: %u  Humidity: %u%%  Wind Speed: %.2f", temperature, pressure, humidity, windspeed);
  Serial.println(charbuf);
  sprintf(charbuf, "[CODE] Weather: %s (%s, %u)", weather_main, weather_desc, weather_icon);
  Serial.println(charbuf);

  return;
}

void drawScreen()
{
  switch (display_state)
  {
  case 0:
    drawAPIWeather();
    break;
  case 1:
    drawSensorData();
    break;
  case 2:
    drawFace();
    break;
  default:
    break;
  }
}

void drawInfoBar()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    display.setCursor(0, 64 - 8);

    if (display_state % 2)
    {
      display.print("ID:");
      display.println(espmac);
    }
    else
    {
      display.print("Web:");
      display.println(WiFi.softAPIP());
    }
  }
  else
  { // 128x64
    int rssi = WiFi.RSSI();
    if (rssi < -90)
      display.drawLine(0, 64 - 2, 0, 64 - 2, WHITE);
    if (rssi >= -90)
      display.drawLine(2, 64 - 2, 2, 64 - 3, WHITE);
    if (rssi >= -80)
      display.drawLine(4, 64 - 2, 4, 64 - 4, WHITE);
    if (rssi >= -70)
      display.drawLine(6, 64 - 2, 6, 64 - 5, WHITE);
    if (rssi >= -60)
      display.drawLine(8, 64 - 2, 8, 64 - 6, WHITE);

    display.setCursor(12, 64 - 8);

    if (display_state % 2)
    {
      display.print("WiFi:");
      display.println(WiFi.SSID());
    }
    else
    {
      display.print("Web:");
      display.println(WiFi.localIP());
    }
  }
}

void drawSensorData()
{
  display.drawBitmap(0, 0, bitmap_temp, 25, 25, WHITE);
  display.drawBitmap(64, 0, bitmap_hum, 25, 25, WHITE);

  display.setCursor(25 - 2, 8); // 2px padding
  display.print(dht_temp);
  display.print((char)247);
  display.println("C");
  display.setCursor(64 + 25 + 2, 8); // 2px padding
  display.print(dht_hum);
  display.println("%");

  display.setCursor(0, 28);
  display.println("Feels like:");
  display.setCursor(0, 38);
  display.setTextSize(2);
  display.print(dht_hi);
  display.print((char)247);
  display.println("C");
  display.setTextSize(1);

  return;
}

void drawAPIWeather()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    display.drawBitmap(0, 0, bitmap_nowifi, 16, 16, WHITE);
    display.setCursor(24, 4);
    display.println("Not connected :(");
    display.setCursor(0, 16);
    display.println("Connect to:");
    display.setCursor(10, 26);
    display.println(espmac);
    display.setCursor(0, 36);
    display.println("and go to:");
    display.setCursor(10, 46);
    display.println(WiFi.softAPIP());

    return;
  }

  if (openWeatherMapApiKey.length() != 32)
  {
    display.drawBitmap(0, 0, bitmap_key, 20, 20, WHITE);
    display.setCursor(25, 8);
    display.println("No API key found!");
    display.setCursor(0, 30);
    display.println("To get the weather,");
    display.setCursor(0, 40);
    display.print("go to ");
    display.print(WiFi.localIP());
    display.println("!");
    return;
  }

  switch (weather_icon)
  {
  case 1:
    display.drawBitmap(0, 0, bitmap_01, 50, 50, WHITE);
    break;
  case 2:
    display.drawBitmap(0, 0, bitmap_02, 50, 50, WHITE);
    break;
  case 3:
    display.drawBitmap(0, 0, bitmap_03, 50, 50, WHITE);
    break;
  case 4:
    display.drawBitmap(0, 0, bitmap_04, 50, 50, WHITE);
    break;
  case 9:
    display.drawBitmap(0, 0, bitmap_09, 50, 50, WHITE);
    break;
  case 10:
    display.drawBitmap(0, 0, bitmap_10, 50, 50, WHITE);
    break;
  case 11:
    display.drawBitmap(0, 0, bitmap_11, 50, 50, WHITE);
    break;
  case 13:
    display.drawBitmap(0, 0, bitmap_13, 50, 50, WHITE);
    break;
  case 50:
    display.drawBitmap(0, 0, bitmap_50, 50, 50, WHITE);
    break;
  default:
    break;
  }

  display.setCursor(50, 16);
  display.println(weather_main);
  display.setCursor(50, 24);
  display.println(weather_desc);
  display.setCursor(50, 36);
  display.print(city);
  display.print(", ");
  display.println(countryCode);

  return;
}

void drawFace()
{
  int randnum = random(3);
  display.drawBitmap(32, 0, smileys[randnum], 64, 56, WHITE);

  return;
}

void IRAM_ATTR isr()
{
  // debounce input
  button_time = millis();
  if (button_time - last_button_time > debounce_time)
  {
    buttonPressed = true;
    last_button_time = button_time;
  }
}

void playPiezo(void *param)
{
  // to copy paste: https://randomnerdtutorials.com/esp32-dual-core-arduino-ide/
  // more in depth: https://www.circuitstate.com/tutorials/how-to-write-parallel-multitasking-applications-for-esp32-using-freertos-arduino/
  Serial.print("[CODE] Piezo running on core ");
  Serial.println(xPortGetCoreID());

  if (currSong == 0 && param != NULL)
  {
    int testalarm = (int)param;
    Serial.print("[CODE] Playing alarm: ");
    Serial.println(testalarm);
    switch (testalarm)
    {
    case 1:
      song1();
      break;
    case 2:
      song2();
      break;
    case 3:
      song3();
      break;
    default:
      break;
    }
    vTaskDelete(NULL);
  }
  else
  {
    Serial.print("[CODE] Playing alarm: ");
    Serial.println(currSong);
    while (1) // infinite loop
    {
      switch (currSong)
      {
      case 1:
        song1();
        break;
      case 2:
        song2();
        break;
      case 3:
        song3();
        break;
      default:
        vTaskDelete(NULL);
        break;
      }
      vTaskDelay(5000);
    }
  }
}
