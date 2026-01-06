# ESP32 Display Project Box – Example Firmware

This repository contains example firmware for an ESP32 project using a
1.54" 240×240 SPI TFT display (ST7789) and an optional LDR for
automatic brightness control.

Originally designed for a nightstand clock, this code can be adapted
for other ESP32 + display projects.

## Features
- Large time and date display
- Weather display using OpenWeatherMap
- Automatic brightness adjustment via LDR
- Wi-Fi auto-reconnect
- Non-blocking UI updates (no flicker)

## Hardware Used
- ESP32 Dev Board (30-pin)
- 1.54" 240×240 IPS TFT (ST7789)
- Light Dependent Resistor (optional)
- 10kΩ resistor
- Jumper wires

## Setup
1. Install the ESP32 board package in Arduino IDE
2. Install required libraries:
   - TFT_eSPI
   - ArduinoJson
3. Configure `User_Setup.h` in TFT_eSPI for ST7789
4. Edit WiFi credentials and OpenWeatherMap API key
5. Upload to ESP32

## Notes
- This code is provided as an example
- Feel free to modify or remix for your own projects
