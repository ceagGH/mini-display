/*
Core 1 - Runs webserver async, handles OTA, fetches weather, manage DHT sensor, handles OLED
Core 0 - plays piezo sometimes, handles button stop alarm

pages to have
- configure weather (location + apikey)
- see current settings
- choose alarm tone
- choose alarm time, and reminders; connect to gcal?

TODO: set time for alarm to go off, choose buzzer song to play
TODO: write code for buzzer
TODO: buzzer + button reminder
TODO: wifi list // custom wifi name, wifimulti
TODO: data validation in html site
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
#define BUZZER_PIN 4
#define BUTTON_PIN 5
#define PRESSED 0
#define NOT_PRESSED 1

#include "pitches.h"
#define TONE_CHANNEL 15
TaskHandle_t piezoTask;

void tone(uint8_t pin, unsigned int frequency, unsigned long duration, uint8_t channel);
void noTone(uint8_t pin, uint8_t channel);
void playPiezo(void *pvParameters);

// -------------------------------------------- SETUP TIMEKEEPING ---------------------------------------------

#include <ESP32Time.h>
#include "time.h"
#include "esp_sntp.h"
const char *ntp_server1 = "pool.ntp.org";
const char *ntp_server2 = "time.nist.gov";
int gmt_offset = 4 * 3600; // idk why we need to put 4h for GMT+8

ESP32Time rtc;

unsigned long prev_wifi_millis = 0;
unsigned long prev_temphum_millis = 0;
unsigned long prev_time_millis = 0;
unsigned long prev_display_millis = 0;

typedef struct
{
  int sec;
  int min;
  int hr;
  int day;
  int month;
  int year;
  int repeats;
  int song;
} alarminfo;
// ss, mm, hh, DD, MM, YYYY, repeats?, buzzersong

alarminfo alarmData[10];

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

  rtc.offset = gmt_offset; // GMT+8
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

  size_t alarmLen = preferences.getBytesLength("alarm");
  if (alarmLen == 0 || alarmLen % sizeof(alarminfo) || alarmLen > sizeof(alarmData))
  {
    Serial.print("[CODE] Invalid size of schedule array: ");
    Serial.println(alarmLen);
  }
  else
  {
    preferences.getBytes("alarm", alarmData, alarmLen);
    for (int i = 0; i < (alarmLen / sizeof(alarminfo)); i++)
    {
      Serial.println(alarmData[i].sec);
      Serial.println(alarmData[i].min);
      Serial.println(alarmData[i].hr);
      Serial.println(alarmData[i].day);
      Serial.println(alarmData[i].month);
      Serial.println(alarmData[i].year);
      Serial.println(alarmData[i].repeats);
      Serial.println(alarmData[i].song);
    }
  }

  espmac = getESPMac();
  // try connecting first; if waited 60sec then open wifi connect
  if (ssid.length() != 0)
  {
    if (openWeatherMapApiKey.length() == 32)
      serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&APPID=" + openWeatherMapApiKey;

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

  display.clearDisplay();
  drawInfoBar();
  drawAPIWeather();
  readDHT();
  readWeatherAPI();
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
      
      if (
        (inputName.length() || inputPwd.length())
        && 
        (ssid != inputName || password != inputPwd)
      ) {
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
        Serial.println("[CODE] Received API Key: "); 
        Serial.println(inputApiKey);
        preferences.putString("apikey", inputApiKey);
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

    if (request->hasParam("alarm")) {
      String inputAlarm = request->getParam("alarm")->value();
      if (inputAlarm.length()) {
        Serial.print("[CODE] Received alarm: ");
        Serial.println(inputAlarm);
      }
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

  server.begin();
  Serial.println("[WIFI] HTTP server started");
}

// ------------------------------------------ LOOP FUNCTION ------------------------------------------

// which interface should be displayed currently
int display_state = 0;

void loop()
{
  // update OLED every minute
  if (millis() - prev_display_millis > 60 * 1000)
  {
    display_state = (display_state + 1) % 3;

    display.clearDisplay();
    drawInfoBar();

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
    display.display();

    prev_display_millis = millis();
  }

  // update 7seg every second
  if (millis() - prev_time_millis > 1 * 1000)
  {
    segdisplay.showNumberDecEx(
        100 * rtc.getHour(1) + rtc.getMinute(),
        (rtc.getSecond() % 2 ? 0b01000000 : 0b00000000),
        true, 4, 0);

    prev_time_millis = millis();
  }

  // Update temperature & humidity data, from local and from api every min
  if (millis() - prev_temphum_millis > 60 * 1000)
  {
    readDHT();
    readWeatherAPI();

    xTaskCreatePinnedToCore(
        playPiezo,    /* Task function. */
        "play_piezo", /* name of task. */
        10000,        /* Stack size of task */
        NULL,         /* parameter of the task */
        1,            /* priority of the task */
        &piezoTask,   /* Task handle to keep track of created task */
        0);           /* pin task to core 0 */

    prev_temphum_millis = millis();
  }
}

// ------------------------------------------ HELPER FUNCTIONS ------------------------------------------
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
    buttons += "<h4>Output - ONBOARD_LED</h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"2\" " + (digitalRead(ONBOARD_LED) ? String("checked") : String("")) + "><span class=\"switchslider\"></span></label>";
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
  if (var == "ALARMPLACEHOLDER")
  {
    String currAlarms = "<div>";
    for (int i = 0; i < (sizeof(alarmData) / sizeof(alarminfo)); i++)
    {
      currAlarms += String(alarmData[i].sec) + " ";
      currAlarms += String(alarmData[i].min) + " ";
      currAlarms += String(alarmData[i].hr) + " ";
      currAlarms += String(alarmData[i].day) + " ";
      currAlarms += String(alarmData[i].month) + " ";
      currAlarms += String(alarmData[i].year) + " ";
      currAlarms += String(alarmData[i].repeats) + " ";
      currAlarms += String(alarmData[i].song) + " ";
    }
    currAlarms += "</div>";
    return currAlarms;
  }

  return String();
}

String settings_processor(const String &var)
{
  if (var == "CURRSETTINGSPLACEHOLDER")
  {
    String info = "";
    if (ssid.length() != 0)
      info += "<div id=\"currwifi\">" + ssid + " (" + password + ")</div>";
    if (openWeatherMapApiKey.length() == 32)
      info += "<div id=\"currapikey\">" + openWeatherMapApiKey + "</div>";

    return info;
  }

  if (var == "INPUTPLACEHOLDER")
  {
    int n = WiFi.scanNetworks();
    Serial.println("[WIFI] WiFi scan done");

    String nearbyWifis = "<div><h3>" + String(n) + " networks found</h3>";

    if (n == 0)
      Serial.println("[WIFI] No networks found");
    else
    {
      nearbyWifis += "<select name=\"wifis\" id=\"wifis\">";
      sprintf(charbuf, "[WIFI] %d networks found:", n);
      Serial.println(charbuf);
      for (int i = 0; i < n; i++)
      {
        // Print SSID and RSSI for each network found
        String info = String(i + 1) + ": " + String(WiFi.SSID(i)) + " (" + String(WiFi.RSSI(i)) + ")" + ((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
        Serial.println(info);
        nearbyWifis += "<option value=\"" + String(WiFi.SSID(i)) + "\">" + info + "</option>";
      }
    }
    nearbyWifis += "</select></div><br>";
    return nearbyWifis;
  }

  return String();
}

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
    Serial.println("[CODE] Invalid apikey, skip reading API");
    return;
  }

  // TODO: put get request on the other core
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

void tone(uint8_t pin, unsigned int frequency, unsigned long duration, uint8_t channel)
{
  if (ledcRead(channel))
  {
    log_e("Tone channel %d is already in use", channel);
    return;
  }
  ledcAttachPin(pin, channel);
  ledcWriteTone(channel, frequency);
  if (duration)
  {
    delay(duration);
    noTone(pin, channel);
  }
}

void noTone(uint8_t pin, uint8_t channel)
{
  ledcDetachPin(pin);
  ledcWrite(channel, 0);
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

  return;
}

void drawFace()
{
  int randnum = random(3);
  display.drawBitmap(32, 0, smileys[randnum], 64, 56, WHITE);

  return;
}

void playPiezo(void *pvParameters)
{
  // Serial.print("[CODE] Task1 running on core ");
  // Serial.println(xPortGetCoreID());

  // tone(BUZZER_PIN, NOTE_C4, 500);
  // noTone(BUZZER_PIN);
  // tone(BUZZER_PIN, NOTE_D4, 500);
  // noTone(BUZZER_PIN);
  // tone(BUZZER_PIN, NOTE_E4, 500);
  // noTone(BUZZER_PIN);
  // tone(BUZZER_PIN, NOTE_F4, 500);
  // noTone(BUZZER_PIN);
  // tone(BUZZER_PIN, NOTE_G4, 500);
  // noTone(BUZZER_PIN);
  // tone(BUZZER_PIN, NOTE_A4, 500);
  // noTone(BUZZER_PIN);
  // tone(BUZZER_PIN, NOTE_B4, 500);
  // noTone(BUZZER_PIN);

  // https://esp32developer.com/programming-in-c-c/tasks/creating-tasks
  vTaskDelete(NULL);
}