/*
Core 1 - Runs webserver async, handles OTA, fetches weather, manage DHT sensor, handles OLED
Core 0 - plays piezo sometimes, handles button stop alarm

TODO: finalize commponents, send mrkang
esp32  -
dht    - gpio
oled   - spi
7seg   - gpio
piezo  - pwm D4
button - gpio


pages to have
- configure weather (location + apikey)
- see current settings

TODO: fix startup flow, get wifi deets and lat/lon before
TODO: fix espcrash onclick of website button
TODO: change EEPROM ssid, pwd, apikey sizes; switch to preferences?
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>

// ------------------------------------------ SETUP DISPLAYS ------------------------------------------

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_MOSI 16
#define OLED_CLK 22
#define OLED_DC 26
#define OLED_CS 21
#define OLED_RESET 14
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

#include <TM1637Display.h>
// Define the connections pins
#define TM_CLK 32
#define TM_DIO 33
TM1637Display segdisplay = TM1637Display(TM_CLK, TM_DIO);
// segdisplay.showNumberDecEx(number, 0b01000000, leading_zeros, length, position)

// ------------------------------------------ SETUP WIFI ------------------------------------------

#include <WiFi.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h> // https://randomnerdtutorials.com/esp32-async-web-server-espasyncwebserver-library/
// #include <AsyncElegantOTA.h>     // https://randomnerdtutorials.com/esp32-ota-over-the-air-arduino/
#include <Arduino_JSON.h>

// ------------------------------------------ SETUP INPUTS/OUTPUTS ------------------------------------------

#include <DHT.h>
#define DHT_SENSOR_PIN 27 // ESP32 pin GPIO21 connected to DHT11 sensor
#define DHT_SENSOR_TYPE DHT22
DHT dht_sensor(DHT_SENSOR_PIN, DHT_SENSOR_TYPE);
float dht_temp;
float dht_hum;

#define ONBOARD_LED 2
#define BUZZER_PIN 4

#include "pitches.h"
#define TONE_CHANNEL 15
TaskHandle_t piezoTask;

void tone(uint8_t pin, unsigned int frequency, unsigned long duration, uint8_t channel);
void noTone(uint8_t pin, uint8_t channel);
void playPiezo(void *pvParameters);

// ------------------------------------------ SETUP MEMORY/VARIABLES ------------------------------------------

#include "SPIFFS.h"
#include <EEPROM.h>
// #include "Preferences.h"

#define EEPROM_SIZE 512 // max is 512
unsigned long prevMillis = 0;
unsigned long timerDelay = 10000;

AsyncWebServer server(80);
String wifi_processor(const String &var);
String main_processor(const String &var);

String jsonBuffer;
String httpGETRequest(const char *serverName);

float temperature, windspeed;
int pressure, humidity;

char charbuf[69];
String ssid, password;
String getESPMac();

String serverPath;
String city = "George Town", countryCode = "MY", openWeatherMapApiKey = "";

// ------------------------------------------ SETUP FUNCTION ------------------------------------------

void setup()
{
  Serial.begin(115200);

  pinMode(ONBOARD_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(ONBOARD_LED, LOW);
  dht_sensor.begin();

  segdisplay.setBrightness(3);
  segdisplay.showNumberDecEx(1234, 0b01000000, false, 4, 0);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("SSD1306 allocation failed");
    delay(1000);
    ESP.restart();
    // for(;;); // Don't proceed, loop forever
  }
  display.setTextSize(1);
  display.setTextColor(WHITE); // Draw white text

  Serial.println("display has been init");
  display.display();
  delay(2000); // Pause for 2 seconds

  EEPROM.begin(EEPROM_SIZE);
  // Initialize SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS");
    delay(1000);
    ESP.restart();
  }

  String espmac = getESPMac();

  // get from EEPROM
  EEPROM.get(0, ssid);
  EEPROM.get(20, password);
  for (int i = 0; i < 32; i++)
  {
    int c = EEPROM.read(50 + i);
    openWeatherMapApiKey += char(c);
  }

  // sprintf(charbuf, "ID: %s, PW: %s", ssid, password);
  // Serial.println(charbuf);

  // try connecting first; if waited 60sec then open wifi connect
  // Serial.println(ssid.length());
  if (0 < ssid.length() && ssid.length() <= 20 && openWeatherMapApiKey.length() == 32)
  {
    serverPath = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + countryCode + "&APPID=" + openWeatherMapApiKey;

    prevMillis = millis();
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid, password);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Connecting");
    display.display();

    while ((millis() - prevMillis < 60000) && (WiFi.status() != WL_CONNECTED))
    {
      delay(500);
      display.print(".");
      display.display();
    }
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(espmac);
    // save wifi in EEPROM

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Please connect to this WiFi:");
    display.println(espmac);
    display.println("And complete the setup here:");
    display.println(IP);
    display.display();

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/wifi.html", String(), false, wifi_processor); });
  }
  else
  {
    Serial.println();
    Serial.print("Connected to WiFi network with IP Address: ");
    Serial.println(WiFi.localIP());

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.print("RSSI: ");
    display.println(WiFi.RSSI());
    display.display();

    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/main.html", String(), false, main_processor); });
  }

  server.on("/init", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("name") && request->hasParam("pwd") && request->hasParam("apikey")) {
      String inputName = request->getParam("name")->value();
      String inputPwd = request->getParam("pwd")->value();
      String inputApiKey = request->getParam("apikey")->value();

      char apikey[32];
      for (int c = 0; c < inputApiKey.length(); c++) apikey[c] = inputApiKey[c];
      Serial.println(apikey);

      sprintf(charbuf, "%s - %s - %s", inputName, inputPwd, inputApiKey);
      Serial.println(charbuf);

      EEPROM.put(0, inputName);
      EEPROM.put(20, inputPwd);
      for (int i=0; i<32; i++) {
        EEPROM.write(50+i, int(inputApiKey[i]));
        delay(5);
      }
      EEPROM.commit();
      Serial.println("updated EEPROM");

      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("Details received! Restarting now...");
      display.display();

      delay(2000);
      ESP.restart();
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

      sprintf(charbuf, "GPIO: %d - Set to: %d - Running on Core %d", inputPin, inputState, xPortGetCoreID());
      Serial.println(charbuf);
    }  

    request->send(200, "text/plain", "OK"); });

  // AsyncElegantOTA.begin(&server); // Start ElegantOTA
  server.begin();
  Serial.println("HTTP server started");
}

// ------------------------------------------ LOOP FUNCTION ------------------------------------------

void loop()
{
  // display OLED:
  // wifi, rssi, ip, alarm, weather, reminders(?)

  // Send an HTTP GET request
  if (millis() - prevMillis > timerDelay)
  {
    dht_temp = dht_sensor.readTemperature();
    dht_hum = dht_sensor.readHumidity();
    if (isnan(dht_temp) || isnan(dht_hum))
      Serial.println("Failed to read from DHT sensor!");
    else
    {
      sprintf(charbuf, "DHT READ: %.2fC, %.2f%", dht_temp, dht_hum);
      Serial.println(charbuf);
      Serial.println(dht_temp);
      Serial.println(dht_hum);
    }
    // Check WiFi connection status
    if (WiFi.status() != WL_CONNECTED)
    {
      // Serial.println("WiFi Disconnected");
      // TODO: add instructions
    }
    else
    {
      Serial.println(serverPath);

      jsonBuffer = httpGETRequest(serverPath.c_str());
      Serial.println(jsonBuffer);
      Serial.println();
      JSONVar jsObj = JSON.parse(jsonBuffer);

      // JSON.typeof(jsonVar) can be used to get the type of the var
      if (JSON.typeof(jsObj) == "undefined")
      {
        Serial.println("Parsing input failed!");
        return;
      }
      // Serial.print("JSON object = ");
      // Serial.println(jsObj);

      xTaskCreatePinnedToCore(
          playPiezo,    /* Task function. */
          "play_piezo", /* name of task. */
          10000,        /* Stack size of task */
          NULL,         /* parameter of the task */
          1,            /* priority of the task */
          &piezoTask,   /* Task handle to keep track of created task */
          0);           /* pin task to core 0 */

      temperature = double(jsObj["main"]["temp"]);
      pressure = int(jsObj["main"]["pressure"]);
      humidity = int(jsObj["main"]["humidity"]);
      windspeed = double(jsObj["wind"]["speed"]);
      sprintf(charbuf, "Temperature: %.2f\nPressure: %u\nHumidity: %u%\nWind Speed: %.2f",
              temperature, pressure, humidity, windspeed);
      Serial.println(charbuf);
    }

    prevMillis = millis();
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
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    payload = http.getString();
  }
  else
  {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();

  return payload;
}

String wifi_processor(const String &var)
{
  // Serial.println(var);
  if (var == "INPUTPLACEHOLDER")
  {
    int n = WiFi.scanNetworks();
    Serial.println("WiFi scan done");

    String nearbyWifis = "<div><h3>" + String(n) + " networks found</h3>";

    if (n == 0)
      Serial.println("No networks found");
    else
    {
      nearbyWifis += "<select name=\"wifis\" id=\"wifis\">";
      sprintf(charbuf, "%d networks found", n);
      Serial.println(charbuf);
      for (int i = 0; i < n; i++)
      {
        // Print SSID and RSSI for each network found
        sprintf(charbuf, "%d: %s (%d)%s", i + 1, WiFi.SSID(i), WiFi.RSSI(i), (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
        Serial.println(charbuf);
        nearbyWifis += "<option value=\"" + String(WiFi.SSID(i)) + "\">" + String(charbuf) + "</option>";
        delay(10);
      }
    }
    nearbyWifis += "</select></div><br>";
    return nearbyWifis;
  }
  return String();
}

String main_processor(const String &var)
{
  Serial.println(var);
  if (var == "BUTTONPLACEHOLDER")
  {
    String buttons = "";
    buttons += "<h4>Output - ONBOARD_LED</h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"2\" " + (digitalRead(ONBOARD_LED) ? String("checked") : String("")) + "><span class=\"slider\"></span></label>";
    return buttons;
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
  sprintf(charbuf, "ESP MAC Address: %s", macstring);
  Serial.println(charbuf);

  return String(macstring);
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

void playPiezo(void *pvParameters)
{
  // Serial.print("Task1 running on core ");
  // Serial.println(xPortGetCoreID());

  tone(BUZZER_PIN, NOTE_C4, 500);
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, NOTE_D4, 500);
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, NOTE_E4, 500);
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, NOTE_F4, 500);
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, NOTE_G4, 500);
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, NOTE_A4, 500);
  noTone(BUZZER_PIN);
  tone(BUZZER_PIN, NOTE_B4, 500);
  noTone(BUZZER_PIN);

  // https://esp32developer.com/programming-in-c-c/tasks/creating-tasks
  vTaskDelete(NULL);
}

/* example api response:
{
    "coord": {
        "lon": 100.3354,
        "lat": 5.4112
    },
    "weather": [
        {
            "id": 801,
            "main": "Clouds",
            "description": "few clouds",
            "icon": "02d"
        }
    ],
    "base": "stations",
    "main": {
        "temp": 305.12,
        "feels_like": 312.12,
        "temp_min": 303.68,
        "temp_max": 305.12,
        "pressure": 1007,
        "humidity": 67
    },
    "visibility": 8000,
    "wind": {
        "speed": 5.14,
        "deg": 210
    },
    "clouds": {
        "all": 20
    },
    "dt": 1694852183,
    "sys": {
        "type": 1,
        "id": 9429,
        "country": "MY",
        "sunrise": 1694819368,
        "sunset": 1694863099
    },
    "timezone": 28800,
    "id": 1735106,
    "name": "George Town",
    "cod": 200
}
*/