
// Reporting frequency - how often reading are taken and published
#define GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES 15

// Sleep approach for reducing power consumption
#define GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH 0 // set to 0 to use deep sleep
                                                             // set to 1 to use automatic light sleep; also EDF-ISP: SDK Configuration -> Component config -> FreeRTOS -> Tickless Idle (must be checked)
                                                             // set to 2 to use manual light sleep; also EDF-ISP: SDK Configuration -> Component config -> FreeRTOS -> Tickless Idle (must be unchecked)
                                                             // for more information, please see: https://github.com/roblatour/WeatherStation

// MQTT
#define GENERAL_USER_SETTINGS_MQTT_BROKER_URL "mqtt://192.168.1.173"
#define GENERAL_USER_SETTINGS_MQTT_BROKER_PORT 1883
#define GENERAL_USER_SETTINGS_MQTT_QOS 2
#define GENERAL_USER_SETTINGS_MQTT_RETAIN 1
#define GENERAL_USER_SETTINGS_MQTT_TOPIC "WeatherStation"

// Wi-Fi
// Your country code, for more information please see https://wiki.recalbox.com/en/tutorials/network/wifi/wifi-country-code 
#define GENERAL_USER_SETTINGS_WIFI_COUNTRY_CODE "CA"

// Target Wake Time mantissa and exponent, for more information please see https://github.com/roblatour/TWTWakeDurationCalculator 
#define GENERAL_USER_SETTINGS_ITWT_WAKE_INVL_MANT 6250
#define GENERAL_USER_SETTINGS_ITWT_WAKE_INVL_EXPN 4

// GPIO PIN attached to an external physical switch used to toggle on / off reporting to PWSWeather.com
#define GENERAL_USER_SETTINGS_EXTERNAL_SWITCH_GPIO_PIN GPIO_NUM_12

// time out period (in seconds) to connect to Wi-Fi
#define GENERAL_USER_SETTINGS_WIFI_CONNECT_TIMEOUT_PERIOD 30 

// time out period (in seconds) to complete the MQTT publishing
#define GENERAL_USER_SETTINGS_MQTT_PUBLISHING_TIMEOUT_PERIOD 30 

// time out period (in seconds) to get the PWSWeather publishing done
#define GENERAL_USER_SETTINGS_PWSWEATHER_PUBLISHING_TIMEOUT_PERIOD_IN_SECONDS 30

// BME680 sensor
#define GENERAL_USER_SETTINGS_BME680_I2C_ADDR 0x77
#define GENERAL_USER_SETTINGS_PORT 0
#define GENERAL_USER_SETTINGS_I2C_SDA 21
#define GENERAL_USER_SETTINGS_I2C_SCL 22

// Pin used to power up and down the BME680 sensor
#define GENERAL_USER_SETTINGS_POWER_SENSOR_CONTROLLER_PIN GPIO_NUM_20

// Debugging
#define GENERAL_USER_SETTINGS_TAG "Weather Station"

// The following are used for a static ip addressing
// The IP address below is for the ESP32
// (only enable this feature if not publishing to PWSWeather.com, and 
//  if you are publishing to a MQTT server with a static numeric IPv4 address)
#define GENERAL_USER_SETTINGS_ENABLE_STATIC_IP 0  // 0 = FALSE, 1 = TRUE
#define GENERAL_USER_SETTINGS_STATIC_IP_ADDR "192.168.200.248"
#define GENERAL_USER_SETTINGS_STATIC_IP_NETMASK_ADDR "255, 255, 255, 0"
#define GENERAL_USER_SETTINGS_STATIC_IP_GW_ADDR "192.168.200.1"
