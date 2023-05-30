// Important note:
//
// There is a known problem related to going into light sleep mode and then waking up, in that the Wi-Fi connection may be dropped durning light sleep.
// 
// Having that said, the issue could be related to my Wi-Fi 6 access point, but I am not sure.
//
// Also, the problem appears to happen less often with shorter reporting frequencies (for example once every 3 minutes or less), but definitely happen with longer reporting frequencies (for example 15 minutes).
//
// While the program has error handling to reconnect to Wi-Fi, the time needed to do so mostly defeats the purpose of using light sleep to save power.
// In fact, the overall power consumption may even be higher than if the program was to just used deep sleep and reconnect.
//
// In any case, there are two optional work-arounds to this problem if you are using longer reporting frequencies:
//
//    1. to use deep sleep in place of light sleep (potentially better than no work-around if you are concerned about power consumption), or
//
//    2. to stay connected and use vTaskDelay() in place of light sleep (the best option if you are not concerned about power consumption)
//       Also, with the second work-around you will be able to see the impressive run time differences between the first time and subsequent cycles
//
// To select no work around or one of the two work arounds noted above, set the WORKAROUND value directly below:
//
#define WORKAROUND 0 // for no work-around set to zero (program will use light sleep and auto reconnected as needed); to use work-around 1 set to 1; to use work-around 2 set to 2
//
// There is an open github issue on this problem at xxxx
// 

// reporting frequency  
#define GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES 15 

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
#define GENERAL_USER_SETTINGS_WIFI_CONNECT_TIMEOUT_PERIOD 30 // 15

// time out period (in seconds) to complete the MQTT publishing
#define GENERAL_USER_SETTINGS_MQTT_PUBLISHING_TIMEOUT_PERIOD 30 // 10

// time out period (in seconds) to get the PWSWeather publishing done
#define GENERAL_USER_SETTINGS_PWSWEATHER_PUBLISHING_TIMEOUT_PERIOD_IN_SECONDS 30

// Debugging

// Note: In the SDK Configuration Editor you can set Default log verbosity to 'Info' to see the displays while debugging.
//       However, when releasing for 'production' it may be set to 'No Output' to reduce processing time.

#define GENERAL_USER_SETTINGS_SAFETY_SLEEP_IN_SECONDS 0  // used in testing to help debug panics; should be set to 0 for production 

#define GENERAL_USER_SETTINGS_TAG "Weather Station"

// BME680 sensor
#define GENERAL_USER_SETTINGS_BME680_I2C_ADDR 0x77
#define GENERAL_USER_SETTINGS_PORT 0
#define GENERAL_USER_SETTINGS_I2C_SDA 21
#define GENERAL_USER_SETTINGS_I2C_SCL 22

// Pin used to power up and down the BME680 sensor
#define GENERAL_USER_SETTINGS_POWER_SENSOR_CONTROLLER_PIN GPIO_NUM_20

// The following are used for a static ip addressing
// The IP address below is for the ESP32
// (only enable this feature if not publishing to PWSWeather.com, and 
//  if you are publishing to a MQTT server with a static numeric IPv4 address)
#define GENERAL_USER_SETTINGS_ENABLE_STATIC_IP 0  // 0 = FALSE, 1 = TRUE
#define GENERAL_USER_SETTINGS_STATIC_IP_ADDR "192.168.200.248"
#define GENERAL_USER_SETTINGS_STATIC_IP_NETMASK_ADDR "255, 255, 255, 0"
#define GENERAL_USER_SETTINGS_STATIC_IP_GW_ADDR "192.168.200.1"
