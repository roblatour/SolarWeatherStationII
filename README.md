# WeatherStation 
Using an ESP32-C6, WiFi 6, and a BME680 sensor
Publishing to MQTT and optionally PWSWeather.com

Open source Weather Station by Rob Latour, 2023, MIT license

# Featuring

- a power conscious hardware and software design for use within a solar powered project 
  - 2.4GHz WiFi 6 connectivity with Targeted Wake Time (TWT) using an Espressif ESP32-C6 Devkit C-1, and a
  - DFRobot Solar Power Manager 5V
  
- temperature, pressure and humidity using a BME680 sensor

- an external switch to toggle on or off reporting to PWSWeather
  which is handy if moving the weather station indoors for whatever reason
  (uses an internal pullup resistor so a resistor external to the ESP32-C6 isn't required)

# Reporting to

- an MQTT broker (such as the Mosquitto broker running on Home Assistant) for internal access to your data

- (optionally) PWSWeather.com by Aerisweather, using a free account for external access to your data.
  PWSWeather.com provides a web based presentation of your current and historical weather station data and 
  facilitates sharing your readings globally

# Build environment

Built using release version 5.1 Espressif's ESP-IDF with the Espressif Visual Studio Code extention version 1.6.3

Note: at the time of ESP-IDF release 5.1 is not yet a final stable release, but it is far enough along to fully support this project

# Power consumption

Power consumption varies depending on the approach chosen for sleep between reporting periods:

  The first and most straight forward approach to save power is to have program go into deep sleep in between readings.
  The advantage of this is that the esp32 will use as little power between readings as is possible.
  The disadvantage of this is that it will need to reboot and reconnect at the start of every cycle.
  Accordingly, this is likely the best option for longer reporting periods.

  There are also two alternative approaches to deep sleep, these are manual and automatic light sleep. 

  Manual light sleep:
    With this option, the esp32 will not preserve the Wi-Fi 6 connection between readings, rather it will reconnect to Wi-Fi when it is time to publish the data.
    The advantage of manual light sleep is that the device will not need to reboot as is required for awaking from deep sleep.
    The disadvantages of this are that the Wi-Fi will need to be reconnected when it is time to publish the data, and the device will use more power between readings than deep sleep. ???

  Automated light sleep:
    Automatic light sleep allows the Wi-Fi 6 connection to be preserve between readings.
    The advantage of preserving the Wi-Fi 6 connection is that the device can publish a new set of readings without needing to reboot or reconnect. 
    Also, automatic light sleep allows the use of power management, which reduces the power consumption durning sleep but not to the degree that it is reduced by manual light sleep as with automatic light sleep the esp32 continues to respond to Wi-Fi beacons. 
    The disadvantage of preserving the Wi-Fi 6 connection is that the device will use more power between readings than when in either manual light sleep or deep sleep.
    Accordingly, this is likely the best option for shorter reporting periods.

Here are some average hourly power consumption results: 

| Reporting Period |Deep Sleep|Automatic Light Sleep|Manual Light Sleep|
|------------------|:--------:|:-------------------:|:----------------:|
|5 minutes |2.50 mA|2.22 mA|2.57 mA|
|10 minutes|2.05 mA|2.18 mA|2.72 mA|
|15 minutes|1.84 mA|2.09 mA|2.79 mA|

The above findings are based on limited testing, and in all cases:

  connecting to a Wi-Fi 6 access point

  only publishing to a MQTT server on an internal network  
  (as publishing to PWSWeather.com introduces variability related to the site's response time for each individual request)

  using esp_wifi_set_ps(WIFI_PS_MAX_MODEM)

  having removed the power LED on the ESP32-C6 (which saved about .3 mA)

# The code

The code includes two companion files in the main directory:
- general_user_settings.h  used to change various settings, such as the number of minutes between readings
- secret_user_settings.h   used to change various secret settings, such as your WiFi password
	
Uses a I2C and BME680 driver from this Espressif ESP-IDF component library
https://github.com/UncleRus/esp-idf-lib  

The BME680 driver was forked from the original driver by Gunar Schorcht
https://github.com/gschorcht/bme680-esp-idf


Donations welcome https://rlatour.com
