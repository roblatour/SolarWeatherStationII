# WeatherStation 
  (using an ESP32-C6, WiFi 6, BME680 sensor, publishing to MQTT and optionally PWSWeather.com)

Open source Weather Station by Rob Latour, 2023, MIT license

# Featuring

- a power conscious hardware and software design for use within a solar powered project: 
  - 2.4GHz WiFi 6 connectivity with Targeted Wake Time (TWT) using an espressif ESP32-C6 dev kit, and a
  - DFRobot Solar Power Manager 5V
  
- temperature, pressure and humidity using a BME680 sensor

- an external switch to toggle on or off reporting to PWSWeather
  which is handy if moving the weather station indoors for whatever reason
  (uses an internal pullup resistor (so a resistor external to the ESP32-C6 dev kit isn't required)

Note: at the time of release there is a known issue that a WiFi 6 does not always stay connected while in light sleep, especially for light sleep periods over three minutes
      this issue seems to be more pronounced the longer the program is in light sleep, and it has been raised here:
      the code includes two optional work-arounds.  
      For more information, please see the 'Power Consumption' section below

# Reporting to

- an MQTT broker (such as the Mosquitto broker running on Home Assistant) for internal access to your data

- (optionally) PWSWeather.com by Aerisweather, using a free account for external access to your data.
  PWSWeather.com provides a web based presentation of your current and historical weather station data and 
  facilitates sharing your readings globally

# Build environment

Built using release version 5.1 Espressif's ESP-IDF with the Espressif Visual Studio Code extention version 1.6.2

Note: at the time of ESP-IDF release 5.1 is not yet a final stable release, but it is far enough along to fully support this project

# Power consumption

The power consumption varies depending on the work-around option chosen:
0. with no work around, the program goes into light sleep between reporting cycles.  With this when it awakes it does not need to reboot, but it does need to reconnect to Wi-Fi
1. work-around 1 the program uses deep sleep in place of light sleep between reporting cycles.  While deep sleep uses less power than light sleep, the need to reboot and reconnect for each reporting cycle is less than ideal 
2. work-around 2 uses a delay in place of light sleep.  With this the Wi-Fi connection is preserved between reporting cycles, and the program does not need to reboot or reconnect after each reporting cycle.  However, it uses much more power in between cycles.
	
# The code

The code includes two companion files in the main directory:
- general_user_settings.h  used to change various settings, such as the number of minutes between readings
- secret_user_settings.h   used to change 'secret' settings, such as your WiFi password
	
Uses a I2C and BME680 driver from this Espressif ESP-IDF component library https://github.com/UncleRus/esp-idf-lib . 
The BME680 driver was forked from the original driver by Gunar Schorcht https://github.com/gschorcht/bme680-esp-idf
