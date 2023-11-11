# mini-display

# File Structure
### `/src`
This folder contains all the source codes, to be uploaded to the ESP32

### `/data`
This folder contains all the HTML files to be served by the ESP32, and stored in its SPIFFS.

# Reading Serial logs
- `[CODE]` - related to ESP32 memory or internal code logging
- `[WIFI]` - related to WiFi library
- `[MODULE]` - related to display modules or DHT sensor
- `[GPIO]` - related to piezobuzzer or misc GPIOs

# Notes
- [BOM List + Hardware Packaging Tracker](https://docs.google.com/spreadsheets/d/1CK4JsRST5qAaVRgLo28v85z7D3d7RJSNQsXnCHRLqZE/view)
- SSD1309 can display 8 lines max
- [SSD1309 needs SSD1306 library to run...](https://github.com/sh1ura/display-and-USB-host-with-ESP32)
- button is pulled high, so digitalRead == 1 is not pressed
- casing = 3mm acrylic, ceclogo = 3d printed

# Todo list
- finish write code for display
- uploadable from arduino
- write documentation



Example API Response from OpenWeatherAPI
```json
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
```


