# Solar Weather Station II
Using an ESP32-C6, Wi-Fi 6 and a BME680 sensor
Publishing to MQTT and optionally PWSWeather.com

Open source Weather Station by Rob Latour, 2023, MIT license

Please see [Solar Weather Station III](https://github.com/roblatour/SolarWeatherStationIII) for the latest update!

# Featuring

  - a power conscious hardware and software design for use within a solar powered project 
  - 2.4GHz Wi-Fi 6 connectivity with Targeted Wake Time (TWT) using an Espressif ESP32-C6 Devkit C-1, 
  - DFRobot Solar Power Manager 5V
  - (optional) Sparkfun TPL5110 Nano Power Timer (see notes below)
  
- temperature, pressure and humidity using a BME680 sensor

- an external switch / jumper to toggle on or off reporting to PWSWeather
  which is handy if moving the weather station indoors for whatever reason
  (uses an internal pullup resistor so a resistor external to the ESP32-C6 isn't required)

# Reporting to

- an MQTT broker (such as the Mosquitto broker running on Home Assistant) for internal access to your data

- (optionally) PWSWeather.com by Aerisweather, using a free account for external access to your data.
  PWSWeather.com provides a web based presentation of your current and historical weather station data and 
  facilitates sharing your readings globally

# Build environment

Built using release version 5.1 Espressif's ESP-IDF with the Espressif Visual Studio Code extention version 1.83

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
    The disadvantages of this are that the Wi-Fi will need to be reconnected when it is time to publish the data, and the device will use more power between readings than with deep sleep.

  Automated light sleep:
    Automatic light sleep allows the Wi-Fi 6 connection to be preserve between readings.
    The advantage of preserving the Wi-Fi 6 connection is that the device can publish a new set of readings without needing to reboot or reconnect. 
    Also, automatic light sleep allows the use of power management, which reduces the power consumption durning sleep but not to the degree that it is reduced by manual light sleep as with automatic light sleep the esp32 continues to respond to Wi-Fi beacons. 
    The disadvantage of preserving the Wi-Fi 6 connection is that the device will use more power between readings than when in either manual light sleep or deep sleep.
    Accordingly, this is likely the best option for shorter reporting periods.
	
  If you would like to learn more about WiFi 6, TWT, and ESP32 sleep alternatives, here is a very informmative video from Espresif https://www.youtube.com/watch?v=FpTwQlGtV0k
	
  Additionally, as the Espressif ESP32-C6 Devkit C-1 is somewhat power hungry as an alternative to the various ESP32 sleep modes above a TLP5100 circuit may be used.
 
  TPL5100:
    I use this one from Sparkfun: https://www.sparkfun.com/products/15353     
    Using a TPL5100 provided for significant overall power consumption, even over deep sleep.  
    To explain: the TPL5100 is like an external deep sleep function for the ESP32 development board.  With a preset cycle period is hardwired, for example for 15 minutes.   When initially powered from your power supply, the TPL500 will start passing power through to the development board.  When the ESP32 completes its tasks for a cycle (say in 10 seconds) it them ends by triggering the TPL5100 to cut off power to the ESP32 development board.  The TPL5100 then cuts off the power until the start of the cycle after which time it resumes sending power to the development board for another cycle.  
    However, in my testing I could not get the TPL5100 board to reliably work; it would often run for several hours without an issue and then suddenly stall out - without bringing the board to life again for a couple of hours, if at all.  I tried a couple boards and got the same results.  Having that said, your millage may vary and you may have better luck.  Regardless, based on my own testing, I ultimately gave up on this solution.

Here are some average hourly power consumption results: 

| Reporting Period |Reports/hour|Deep Sleep|Automatic Light Sleep|Manual Light Sleep|TLP5100|
|------------------|:----------:|:--------:|:-------------------:|:----------------:|:----------------:|
|5 minutes |12|2.50 mA|2.22 mA|2.57 mA|0.577 mA|
|10 minutes|6|2.05 mA|2.18 mA|2.72 mA|0.428 mA|
|15 minutes|4|1.84 mA|2.09 mA|2.79 mA|0.469 mA|

The above findings are based on limited testing, and: 

  connecting to a Wi-Fi 6 access point

  only publishing to a MQTT server on an internal network  
  (as publishing to PWSWeather.com introduces variability related to the site's response time for each individual request)

  using esp_wifi_set_ps(WIFI_PS_MAX_MODEM)

  having removed the power LED on the ESP32-C6

  Finally, although the above numbers don't reflect it, further power savings can be realized by setting the ESP-IDF: SDK Config - ROM Bootlog Behavior - permanently change Boot ROM output to permanently disable logging (of note this is irreversible).  This change will have the greatest marked savings when deep sleep is used.
  
# The code

The code includes two companion files in the main directory:
- general_user_settings.h  used to change various settings, such as the number of minutes between readings
- secret_user_settings.h   used to change various secret settings, such as your WiFi password
	
Uses a I2C and BME680 driver from this Espressif ESP-IDF component library
https://github.com/UncleRus/esp-idf-lib  

The BME680 driver was forked from the original driver by Gunar Schorcht
https://github.com/gschorcht/bme680-esp-idf

# (Optionally) using Node-Red 

While the code above allows your ESP32 to publish weather readings directly to PWSWeather.com doing so requires more power.   Accordingly, in order to preserve power in a solar based solution, if you have a Node-Red running along side a MQTT server (as can be done in Home Assistant as an example) you may opt to have the ESP32  report its readings via MQTT only, and have Node-Red subscribe to and relay those readings to PWSWeather.com.

Here is a picture of the Node-Red flow that I've have built for this purpose:
![components](https://github.com/roblatour/WeatherStation/blob/main/Node-Red/Node-Red-Flow.jpg)

with the code for it also included within this repository.

# PCB

In my final solution I am using an ESP32-C6 DevkitC-1 v1.2 along with the TPL5100 board mentioned above.  In order to cut down on the wiring within my solar weather station enclosure I desiged the following open source PCB, which you are welcome to use if you like. 
Here is a link to the PCB: https://oshwlab.com/roblatour/weather-station-2023_copy (However, please see the comments above about the TPL5100 board above - ultimately something that didn't work out for me).


Donations welcome https://rlatour.com
