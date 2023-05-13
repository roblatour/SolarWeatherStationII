# WeatherStation (using an ESP32-C6, WiFi 6, BME680 sensor, publishing to MQTT and optionally PWSWeather.com)

Open source Weather Station by Rob Latour, 2023, MIT license

# Featuring

- a power conscious hardware and software design for use within a solar powered project: 
  - 2.4GHz WiFi 6 connectivity using an espressif ESP32-C6 dev kit, and a
  - DFRobot Solar Power Manager 5V
  
- temperature, pressure and humidity using a BME680 sensor

- an external switch to toggle on or off reporting to PWSWeather
  which is handy if moving the weather station indoors for whatever reason
  (uses an internal pullup resistor (so a resistor external to the ESP32-C6 dev kit isn't required)

# Reporting to

1. an MQTT broker (such as the Mosquitto broker running on Home Assistant) for internal access to your data

2. (optionally) PWSWeather.com by Aerisweather, using a free account for external access to your data.
   PWSWeather.com provides a web based presentation of your current and historical weather station data and 
   facilitates sharing your readings globally

# Build environment

Built using release version 5.1 Espressif's ESP-IDF with the Espressif Visual Studio Code extention version 1.6.2

Note: at the time of ESP-IDF release 5.1 is not yet a final stable release, but it is far enough along to fully support this project

# Power consumption

Tested with an Nordic Power Profiler kit II, showing an average power consumption of 1.44mAh 

Of note: the number above is based on:
- reporting to both MQTT and PWSWeather.com every 15 minutes,
- the removal of the red power LED on the ESP32-C6 dev kit to reduced ongoing power consumption (by .3mAh), and
- the setting of the ESP-IDF menuconfig Log Output option to 'No output' for the final project to be deployed into use
	
# The code

The code includes two companion files in the main directory:
- user_settings_general.h  used to change various settings, such as the number of minutes between readings
- user_settings_secret.h   used to change 'secret' settings, such as your WiFi password
	
The code itself is also well commented, and includes commented out code (that you can uncomment if you like) to confirm a WiFi 6 connection

Uses a I2C and BME680 driver from this Espressif ESP-IDF component libarary https://github.com/UncleRus/esp-idf-lib
The BME680 driver was forked from the original driver by Gunar Schorcht https://github.com/gschorcht/bme680-esp-idf
