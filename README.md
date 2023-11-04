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
- SSD1309 can display 8 lines max
- [SSD1309 needs SSD1306 library to run...](https://github.com/sh1ura/display-and-USB-host-with-ESP32)
- Stress relief hole now 3mm, need at least 4mm
- button is pulled high, so digitalRead == 1 is not pressed
- casing = 3mm acrylic, joints = 3d printed

design casing

write code for display

write documentation

