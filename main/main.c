// (Wi-Fi 6) Weather Station

// Copyright Rob Latour, 2023
// License MIT
// open source project: https://github.com/roblatour/WeatherStation

// Running on an esp32-c6 for Wifi 6 (802.11ax) using either:
//  - deep sleep, or
//  - automatic light sleep with WiFi 6
//  to reduce power consumption.
//
// For more information please see the general_user_settings.h file

#include "sdkconfig.h"
#include "general_user_settings.h"
#include "secret_user_settings.h"

#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include <esp_system.h>

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"

#include "driver/gpio.h"

#include "nvs_flash.h"

#include <bme680.h>

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/err.h"
#include "lwip/api.h"

#include "mqtt_client.h"

#include "esp_http_client.h"

#include <cJSON.h>

#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_netif.h"
#include "esp_tls.h"

#include "cmd_system.h"
#include "wifi_cmd.h"
#include "esp_wifi_he.h"

// debugging
static const char *TAG = GENERAL_USER_SETTINGS_TAG;

// BME680 sensor
#define HTTP_RESPONSE_BUFFER_SIZE 1024
#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

// Used to power up and down the BME680 sensor
#define POWER_ON 1
#define POWER_OFF 0

// WIFI
#define ITWT_TRIGGER_ENABLED 1 // 0 = FALSE, 1 = TRUE
#define ITWT_ANNOUNCED 1       // 0 = FALSE, 1 = TRUE
#define ITWT_MIN_WAKE_DURATION 255

#define WIFI_LISTEN_INTERVAL 100

// Global variables

volatile bool BME680_readings_are_reasonable;
volatile float temperature;
volatile float humidity;
volatile float pressure;

enum Wifi_status
{
    WIFI_CHECKING = 0,
    WIFI_UP = 1,
    WIFI_DOWN = 2
};

volatile enum Wifi_status WiFi_current_status;

volatile bool WiFi_is_connected = false;
volatile bool WiFi6_TWT_setup_successfully = false;
volatile bool MQTT_is_connected = false;
volatile bool light_sleep_enabled = false;

volatile int MQTT_published_messages;
volatile bool MQTT_publishing_in_progress;
volatile bool MQTT_unknown_error = false;

volatile bool PWSWeather_Publishing_Done;
volatile bool PWSWeather_unknown_error = false;

volatile bool going_to_sleep = false;

volatile esp_mqtt_client_handle_t MQTT_client;

const int CONNECTED_BIT = BIT0;
const int DISCONNECTED_BIT = BIT1;
esp_netif_t *netif_sta = NULL;
EventGroupHandle_t wifi_event_group;

volatile int64_t cycle_start_time = 0;

esp_pm_config_t power_management_disabled;
esp_pm_config_t power_management_enabled;

void initialize_power_management()
{

    // power management is only needed when the program will use automatic light sleep

    if (GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH == 1)
    {

        // get the current power management configuration and save it as a baseline for when power save mode is disabled
        esp_pm_get_configuration(&power_management_disabled);

        // set the power management configuration for when the power save mode is enabled
#if CONFIG_PM_ENABLE
        esp_pm_config_t power_management_when_enabled = {
            .max_freq_mhz = 160, // ref: the esp32-c6 datasheet https://www.espressif.com/sites/default/files/documentation/esp32-c6-wroom-1_wroom-1u_datasheet_en.pdf
            .min_freq_mhz = 10,  // ref: Espressive's itwt example: https://github.com/espressif/esp-idf/tree/903af13e847cd301e476d8b16b4ee1c21b30b5c6/examples/wifi/itwt
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
            .light_sleep_enable = true
#endif
        };
        power_management_enabled = power_management_when_enabled;
#endif
    }
}

void enable_power_save_mode(bool turn_on)
{
    if (turn_on)
        ESP_ERROR_CHECK(esp_pm_configure(&power_management_enabled));
    else
        ESP_ERROR_CHECK(esp_pm_configure(&power_management_disabled));
}

void MQTT_publish_a_reading(const char *subtopic, float value)
{

    static char topic[100];
    strcpy(topic, GENERAL_USER_SETTINGS_MQTT_TOPIC);
    strcat(topic, "/");
    strcat(topic, subtopic);

    static char payload[13];
    sprintf(payload, "%g", value);

    ESP_LOGI(TAG, "publish: %s %s", topic, payload);
    esp_mqtt_client_publish(MQTT_client, topic, payload, 0, GENERAL_USER_SETTINGS_MQTT_QOS, GENERAL_USER_SETTINGS_MQTT_RETAIN);
}

void MQTT_publish_all_readings()
{
    MQTT_published_messages = 0;
    MQTT_publish_a_reading("temperature", temperature);
    MQTT_publish_a_reading("humidity", humidity);
    MQTT_publish_a_reading("pressure", pressure);
};

esp_mqtt_event_handle_t event;
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{

    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        MQTT_is_connected = true;
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        MQTT_is_connected = false;
        break;

        // case MQTT_EVENT_SUBSCRIBED:
        //     ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        //     break;

        // case MQTT_EVENT_UNSUBSCRIBED:
        //     ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        //     break;

    case MQTT_EVENT_PUBLISHED:

        // ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);

        MQTT_published_messages++;
        if (MQTT_published_messages >= 3)
        {
            ESP_LOGI(TAG, "MQTT publishing complete");
            MQTT_publishing_in_progress = false;
        };
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Confirmed %.*s received", event->topic_len, event->topic);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        MQTT_unknown_error = true;
        break;

    default:
        ESP_LOGW(TAG, "Other mqtt event id:%d", event->event_id);
        break;
    }
}

void get_bme680_readings()
{

    // Gets temperature, pressure and humidity from the BME680.
    // Do not get gas_resistance.

    static bool doOnce = true;

    static bme680_t sensor;

    bme680_values_float_t values;

    temperature = 0;
    humidity = 0;
    pressure = 0;

    BME680_readings_are_reasonable = false;

    // power up the BME680 sensor
    // one time setup for the BME680 sensor power pin
    if (doOnce)
    {
        esp_rom_gpio_pad_select_gpio(GENERAL_USER_SETTINGS_POWER_SENSOR_CONTROLLER_PIN);
        gpio_reset_pin(GENERAL_USER_SETTINGS_POWER_SENSOR_CONTROLLER_PIN);
        gpio_set_direction(GENERAL_USER_SETTINGS_POWER_SENSOR_CONTROLLER_PIN, GPIO_MODE_OUTPUT);
        doOnce = false;
    };

    gpio_set_level(GENERAL_USER_SETTINGS_POWER_SENSOR_CONTROLLER_PIN, POWER_ON);
    ESP_LOGI(TAG, "BME680 powered on");

    // wait for the sensor to fully power up
    vTaskDelay(25 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "taking BME680 readings");

    // initialize the sensor
    ESP_ERROR_CHECK(i2cdev_init());

    memset(&sensor, 0, sizeof(bme680_t));
    ESP_ERROR_CHECK(bme680_init_desc(&sensor, GENERAL_USER_SETTINGS_BME680_I2C_ADDR, GENERAL_USER_SETTINGS_PORT, GENERAL_USER_SETTINGS_I2C_SDA, GENERAL_USER_SETTINGS_I2C_SCL));
    // wait for the sensor to power up

    ESP_ERROR_CHECK(bme680_init_sensor(&sensor));

    // turn off reporting for gas_resistance
    bme680_use_heater_profile(&sensor, BME680_HEATER_NOT_USED);

    // set the oversampling rate to 16x for temperature, pressure and humidity.
    bme680_set_oversampling_rates(&sensor, BME680_OSR_16X, BME680_OSR_16X, BME680_OSR_16X);

    // Set the IIR filter size
    // The purpose of the IIR filter is to remove noise and fluctuations from the sensor data, which can improve the accuracy and stability of the sensor readings.
    // The filter size determines how much filtering is applied to the sensor data, with larger filter sizes resulting in smoother but slower sensor readings.
    // The bme680_set_filter_size() function may be set to one of several predefined values, depending on the level of filtering required.
    // The available filter sizes range from 0 (no filtering) to 127 (maximum filtering).
    // By selecting an appropriate filter size, you can balance the trade-off between sensor response time and accuracy, depending on the specific needs of your application.
    bme680_set_filter_size(&sensor, BME680_IIR_SIZE_127);

    // Set ambient temperature n/a
    // bme680_set_ambient_temperature(&sensor, 20);

    // get the delay time (duration) required to get a set of readings
    uint32_t duration;
    bme680_get_measurement_duration(&sensor, &duration);

    // for some reason the first measurement is always wrong
    // so take a throw away measurement
    // this measurement will not counted in the attempts counter below
    if (bme680_force_measurement(&sensor) == ESP_OK)
    {
        vTaskDelay(duration);
        if (bme680_get_results_float(&sensor, &values) == ESP_OK)
        {
            // ESP_LOGI(TAG, "throw away measurement taken");
        }
    };

    int attempts = 0;
    int max_attempts = 10;
    // take up to max_attempts measurements until the readings are valid

    while (!BME680_readings_are_reasonable && (attempts++ < max_attempts))
    {

        if (bme680_force_measurement(&sensor) == ESP_OK)
        {
            vTaskDelay(duration); // wait until measurement results are available

            temperature = values.temperature;
            humidity = values.humidity;
            pressure = values.pressure;

            if (bme680_get_results_float(&sensor, &values) == ESP_OK)
            {
                // apply a reasonability check against the readings
                BME680_readings_are_reasonable = ((humidity <= 100.0f) && (temperature >= -60.0f) && (temperature <= 140.0f) && (pressure >= 870.0f) && (pressure <= 1090.0f));

                if (!BME680_readings_are_reasonable)
                {
                    ESP_LOGE(TAG, "readings: Temperature: %.2f °C   Humidity: %.2f %%   Pressure: %.2f hPa", temperature, humidity, pressure);
                    ESP_LOGE(TAG, "the above readings are unreasonable; will try again ( %d of %d )", attempts, max_attempts);
                }
            }
            else
            {
                ESP_LOGE(TAG, "could not get BME680 readings; will try again ( %d of %d )", attempts, max_attempts);
            }
        }
    };

    // power down the BME680 sensor
    gpio_set_level(GENERAL_USER_SETTINGS_POWER_SENSOR_CONTROLLER_PIN, POWER_OFF);
    ESP_LOGI(TAG, "BME680 powered off");

    // release the I2C bus
    ESP_ERROR_CHECK(i2cdev_done());

    // The readings will be displayed later when published via MQTT, so unless you want to really want to see them here the following line can be commented out:
    // ESP_LOGI(TAG, "readings: Temperature: %.2f °C   Humidity: %.2f %%   Pressure: %.2f hPa", temperature, humidity, pressure);
}

void publish_readings_via_MQTT()
{

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = GENERAL_USER_SETTINGS_MQTT_BROKER_URL,
        .broker.address.port = GENERAL_USER_SETTINGS_MQTT_BROKER_PORT,
        .credentials.username = SECRET_USER_SETTINGS_MQTT_USER_ID,
        .credentials.authentication.password = SECRET_USER_SETTINGS_MQTT_USER_PASS,
        //.session.disable_keepalive = true,    // this fails on my network; it may work on yours?
        .session.keepalive = INT_MAX, // using this instead of the above
        .network.disable_auto_reconnect = true,
        .network.refresh_connection_after_ms = (GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES + 1) * 60 * 1000,
    };

    MQTT_is_connected = false;
    MQTT_unknown_error = false;
    MQTT_publishing_in_progress = true;

    int64_t timeout = esp_timer_get_time() + GENERAL_USER_SETTINGS_MQTT_PUBLISHING_TIMEOUT_PERIOD * 1000000;

    // multiple attempts to connect to MQTT will be made incase the network connection has failed within the reporting period and needs to be re-established

    int attempts = 0;
    const int max_attempts = 3;

    while (!MQTT_is_connected && (attempts++ < max_attempts) && MQTT_publishing_in_progress)
    {
        if (esp_timer_get_time() < timeout)
        {
            ESP_LOGI(TAG, "Attempting to connect to MQTT (attempt %d of %d)", attempts, max_attempts);

            MQTT_unknown_error = false;

            if (esp_timer_get_time() < timeout)
            {
                MQTT_client = esp_mqtt_client_init(&mqtt_cfg);
                esp_mqtt_client_register_event(MQTT_client, ESP_EVENT_ANY_ID, mqtt_event_handler, MQTT_client);
                ESP_LOGI(TAG, "Starting MQTT client");
                esp_mqtt_client_start(MQTT_client);

                while ((!MQTT_is_connected) && (!MQTT_unknown_error) && (esp_timer_get_time() < timeout))
                    vTaskDelay(20 / portTICK_PERIOD_MS);
            }

            // wait for WiFi to connect (in case it has dropped out)
            while ((!WiFi_is_connected) && (esp_timer_get_time() < timeout))
                vTaskDelay(20 / portTICK_PERIOD_MS);

            if (MQTT_is_connected)
            {

                if (MQTT_publishing_in_progress)
                    MQTT_publish_all_readings();

                while (MQTT_publishing_in_progress && (!MQTT_unknown_error) && (esp_timer_get_time() < timeout))
                    vTaskDelay(20 / portTICK_PERIOD_MS);

                esp_mqtt_client_destroy(MQTT_client);
                vTaskDelay(40 / portTICK_PERIOD_MS);
            }
            else
            {
                esp_mqtt_client_unregister_event(MQTT_client, ESP_EVENT_ANY_ID, mqtt_event_handler);
                ESP_LOGI(TAG, "MQTT failed to connected (attempt %d of %d)", attempts, max_attempts);
                if (attempts == max_attempts)
                    ESP_LOGE(TAG, "Reached the MQTT connection attempt threshold");
                vTaskDelay(20 / portTICK_PERIOD_MS);
                MQTT_unknown_error = true;
            };

            if (esp_timer_get_time() >= timeout)
                ESP_LOGE(TAG, "Timed out trying to connect to MQTT");
        };
    };
}

// Callback function for HTTP events
esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER");
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA");
        ESP_LOGI(TAG, "%.*s", evt->data_len, (char *)evt->data);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_REDIRECT");
    }
    return ESP_OK;
}

// Function to upload weather data to PWSweather
void publish_readings_to_PWSWeather_now()
{

    PWSWeather_Publishing_Done = false;

    // Create the HTTP HTTP_client
    const esp_http_client_config_t config = {
        .url = "https://pwsupdate.pwsweather.com/api/v1/submitwx?",
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t HTTP_client = esp_http_client_init(&config);

    // Set the URL line
    char url_line[250];
    sprintf(url_line, "ID=%s&PASSWORD=%s&dateutc=now&tempf=%.1f&humidity=%.1f&baromin=%.2f&softwaretype=ESP32DIY&action=updateraw",
            SECRET_USER_SETTINGS_PWS_STATION_ID, SECRET_USER_SETTINGS_PWS_API_KEY, temperature * 1.8 + 32.0, (float)humidity, pressure * 0.02952998751);

    ESP_LOGI(TAG, "%s", url_line);

    // Set the HTTP headers
    esp_http_client_set_header(HTTP_client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_header(HTTP_client, "Content-Length", "0");

    // Set the POST data length
    esp_http_client_set_post_field(HTTP_client, url_line, strlen(url_line));

    // set the timeout
    esp_http_client_set_timeout_ms(HTTP_client, (int)(GENERAL_USER_SETTINGS_PWSWEATHER_PUBLISHING_TIMEOUT_PERIOD_IN_SECONDS * 1000));

    // Perform the HTTP request
    esp_err_t err = esp_http_client_perform(HTTP_client);
    if (err == ESP_OK)
    {
        PWSWeather_unknown_error = false;
        ESP_LOGI(TAG, "PWSWeather publishing complete");
    }
    else
    {
        ESP_LOGE(TAG, "Error: esp_http_client_perform failed with error code %d", err);
        PWSWeather_unknown_error = true;
    };

    esp_http_client_close(HTTP_client);   // close the connection
    esp_http_client_cleanup(HTTP_client); // clean up the client
    vTaskDelay(20 / portTICK_PERIOD_MS);
}

void publish_readings_to_PWSWeather()
{

    // if the external switch is in the on position, publish to PWSWeather
    bool publish_to_pwsweather = (gpio_get_level(GENERAL_USER_SETTINGS_EXTERNAL_SWITCH_GPIO_PIN) == 0);
    ESP_LOGI(TAG, "publish via PWSWeather is switched %s", publish_to_pwsweather ? "on" : "off");

    if (publish_to_pwsweather)
        publish_readings_to_PWSWeather_now();
}

static const char *itwt_probe_status_to_str(wifi_itwt_probe_status_t status)
{
    switch (status)
    {
    case ITWT_PROBE_FAIL:
        return "itwt probe fail";
    case ITWT_PROBE_SUCCESS:
        return "itwt probe success";
    case ITWT_PROBE_TIMEOUT:
        return "itwt probe timeout";
    case ITWT_PROBE_STA_DISCONNECTED:
        return "itwt probe sta disconnected";
    default:
        return "itwt probe unknown status";
    }
}

static void WiFi_start_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Wi-Fi started");
    ESP_LOGI(TAG, "Connecting to %s", SECRET_USER_SETTINGS_SSID);
    ESP_ERROR_CHECK(esp_wifi_connect());
}

static void WiFi_connected_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Wi-Fi connected");
}

static void WiFi_disconnect_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    WiFi_is_connected = false;

    if (going_to_sleep)
        ESP_LOGI(TAG, "Wi-Fi disconnected");
    else
    {
        ESP_LOGI(TAG, "Wi-Fi disconnected, reconnecting");
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

static void setup_WIFI6_targeted_wake_time()
{

    // if the Wi-Fi connection supports it, setup Wi-Fi 6 targeted wait time

    wifi_phy_mode_t WiFiMode;

    if (esp_wifi_sta_get_negotiated_phymode(&WiFiMode) == ESP_OK)
    {
        switch (WiFiMode)
        {
        case WIFI_PHY_MODE_HE20:

            // this is ideally what we want as it allows the Wi-Fi connection to be preserved into the next cycle

            ESP_LOGI(TAG, "802.11ax HE20");

            // setup a trigger-based announce individual TWT agreement
            wifi_config_t sta_cfg = {
                0,
            };

            esp_wifi_get_config(WIFI_IF_STA, &sta_cfg);

            esp_err_t err = ESP_OK;
        
            bool flow_type_announced = true;
            uint16_t twt_timeout = 5000;

            wifi_twt_setup_config_t setup_config = {
                .setup_cmd = TWT_REQUEST,
                .flow_id = 0, // announced individual TWT agreement
                .twt_id = 0,
                .flow_type = flow_type_announced ? 0 : 1,
                .min_wake_dura = ITWT_MIN_WAKE_DURATION,
                .wake_invl_expn = GENERAL_USER_SETTINGS_ITWT_WAKE_INVL_EXPN,
                .wake_invl_mant = GENERAL_USER_SETTINGS_ITWT_WAKE_INVL_MANT,
                .trigger = ITWT_TRIGGER_ENABLED,
                .timeout_time_ms = twt_timeout,
            };

            err = esp_wifi_sta_itwt_setup(&setup_config);

            if (err == ESP_OK)
                ESP_LOGI(TAG, "Wi-Fi 6 Targeted Wake Time setup succeeded!");
            else
                ESP_LOGE(TAG, "Wi-Fi 6 Targeted Wake Time setup failed, err:0x%x", err);

            WiFi6_TWT_setup_successfully = true;

            break;

        case WIFI_PHY_MODE_11B:
            ESP_LOGI(TAG, "802.11b");
            break;

        case WIFI_PHY_MODE_11G:
            ESP_LOGI(TAG, "802.11g");
            break;

        case WIFI_PHY_MODE_LR:
            ESP_LOGI(TAG, "Low rate");
            break;

        case WIFI_PHY_MODE_HT20:
            ESP_LOGI(TAG, "HT20");
            break;

        case WIFI_PHY_MODE_HT40:
            ESP_LOGI(TAG, "HT40");
            break;

        default:
            ESP_LOGE(TAG, "unknown Wi-Fi mode");
            break;
        }
    }
    else
        ESP_LOGE(TAG, "failed to get Wi-Fi mode");

    if (!WiFi6_TWT_setup_successfully)
        ESP_LOGW(TAG, "Wi-Fi 6 targeted wake time could not be set up");

    light_sleep_enabled = WiFi6_TWT_setup_successfully;
};

static void got_ip_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));

    xEventGroupClearBits(wifi_event_group, DISCONNECTED_BIT);
    xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);

    setup_WIFI6_targeted_wake_time();

    WiFi_is_connected = true;
}

static void WiFi_beacon_timeout_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGE(TAG, "Beacon timeout");
}

static void WiFi6_itwt_setup_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    wifi_event_sta_itwt_setup_t *setup = (wifi_event_sta_itwt_setup_t *)event_data;
    if (setup->config.setup_cmd == TWT_ACCEPT)
    {
        /* TWT Wake Interval = TWT Wake Interval Mantissa * (2 ^ TWT Wake Interval Exponent) */
        ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_SETUP>twt_id:%d, flow_id:%d, %s, %s, wake_dura:%d, wake_invl_e:%d, wake_invl_m:%d", setup->config.twt_id,
                 setup->config.flow_id, setup->config.trigger ? "trigger-enabled" : "non-trigger-enabled", setup->config.flow_type ? "unannounced" : "announced",
                 setup->config.min_wake_dura, setup->config.wake_invl_expn, setup->config.wake_invl_mant);
        ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_SETUP>wake duration:%d us, service period:%d us", setup->config.min_wake_dura << 8, setup->config.wake_invl_mant << setup->config.wake_invl_expn);
    }
    else
    {
        ESP_LOGE(TAG, "<WIFI_EVENT_ITWT_SETUP>twt_id:%d, unexpected setup command:%d", setup->config.twt_id, setup->config.setup_cmd);
    }
}

static void WiFi6_itwt_teardown_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    wifi_event_sta_itwt_teardown_t *teardown = (wifi_event_sta_itwt_teardown_t *)event_data;
    ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_TEARDOWN>flow_id %d%s", teardown->flow_id, (teardown->flow_id == 8) ? "(all twt)" : "");
}

static void WiFi6_itwt_suspend_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    wifi_event_sta_itwt_suspend_t *suspend = (wifi_event_sta_itwt_suspend_t *)event_data;
    ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_SUSPEND>status:%d, flow_id_bitmap:0x%x, actual_suspend_time_ms:[%lu %lu %lu %lu %lu %lu %lu %lu]",
             suspend->status, suspend->flow_id_bitmap,
             suspend->actual_suspend_time_ms[0], suspend->actual_suspend_time_ms[1], suspend->actual_suspend_time_ms[2], suspend->actual_suspend_time_ms[3],
             suspend->actual_suspend_time_ms[4], suspend->actual_suspend_time_ms[5], suspend->actual_suspend_time_ms[6], suspend->actual_suspend_time_ms[7]);
}

static void WiFi6_itwt_probe_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    wifi_event_sta_itwt_probe_t *probe = (wifi_event_sta_itwt_probe_t *)event_data;
    ESP_LOGI(TAG, "<WIFI_EVENT_ITWT_PROBE>status:%s, reason:0x%x", itwt_probe_status_to_str(probe->status), probe->reason);
}

/*

not currently in use

static void set_static_ip(esp_netif_t *netif)
{
#if GENERAL_USER_SETTINGS_ENABLE_STATIC_IP
    if (esp_netif_dhcpc_stop(netif) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to stop dhcp client");
        return;
    }
    esp_netif_ip_info_t ip;
    memset(&ip, 0, sizeof(esp_netif_ip_info_t));
    ip.ip.addr = ipaddr_addr(GENERAL_USER_SETTINGS_STATIC_IP_ADDR);
    ip.netmask.addr = ipaddr_addr(GENERAL_USER_SETTINGS_STATIC_IP_NETMASK_ADDR);
    ip.gw.addr = ipaddr_addr(GENERAL_USER_SETTINGS_STATIC_IP_GW_ADDR);
    if (esp_netif_set_ip_info(netif, &ip) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set ip info");
        return;
    }
    ESP_LOGI(TAG, "Success setting static ip: %s, netmask: %s, gw: %s",
             GENERAL_USER_SETTINGS_STATIC_IP_ADDR, GENERAL_USER_SETTINGS_STATIC_IP_NETMASK_ADDR, GENERAL_USER_SETTINGS_STATIC_IP_GW_ADDR);
#endif
}
*/

static void start_wifi()
{
    // This subroutine starts the Wi-Fi
    // It will make a Wi-Fi 6 connection if possible
    // However, once the Wi-Fi event handler has been connected and an IP address has been assigned
    // the program will determine if a Wi-Fi 6 connection was actually made

    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    netif_sta = esp_netif_create_default_wifi_sta();
    assert(netif_sta);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // WiFi events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_START, &WiFi_start_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &WiFi_disconnect_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &WiFi_connected_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_BEACON_TIMEOUT, &WiFi_beacon_timeout_handler, NULL, NULL));

    // WiFi 6 ITWT events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_ITWT_SETUP, &WiFi6_itwt_setup_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_ITWT_TEARDOWN, &WiFi6_itwt_teardown_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_ITWT_SUSPEND, &WiFi6_itwt_suspend_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_ITWT_PROBE, &WiFi6_itwt_probe_handler, NULL, NULL));

    // IP event
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &got_ip_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SECRET_USER_SETTINGS_SSID,
            .password = SECRET_USER_SETTINGS_PASSWORD,
            .listen_interval = WIFI_LISTEN_INTERVAL,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);

    // esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11AX);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX);

    esp_wifi_set_country_code(GENERAL_USER_SETTINGS_WIFI_COUNTRY_CODE, true);

    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    // esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    // esp_wifi_set_ps(WIFI_PS_NONE);

    // set_static_ip(netif_sta); // helpful if publishing to mqtt only; if publishing to pwsweather then comment this line // additional code for this is commented out above

    ESP_ERROR_CHECK(esp_wifi_start());

    uint16_t probe_timeout = 65535;
    ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_STA, probe_timeout));

#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_MU_STATS
    esp_wifi_enable_rx_statistics(true, true);
#else
    esp_wifi_enable_rx_statistics(true, false);
#endif
#endif
#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
    esp_wifi_enable_tx_statistics(ESP_WIFI_ACI_VO, true);
    esp_wifi_enable_tx_statistics(ESP_WIFI_ACI_BE, true);
#endif

    register_system();
    register_wifi_itwt();
    register_wifi_stats();
}

void turn_on_Wifi()
{

    ESP_LOGI(TAG, "Turn on Wi-Fi");

    start_wifi();

    // wait for wifi to connect
    int64_t timeout = esp_timer_get_time() + GENERAL_USER_SETTINGS_WIFI_CONNECT_TIMEOUT_PERIOD * 1000000;
    while (!WiFi_is_connected && (esp_timer_get_time() < timeout))
        vTaskDelay(20 / portTICK_PERIOD_MS);

    if (!WiFi_is_connected)
        ESP_LOGE(TAG, "Timed out trying to connect to Wi-Fi");
};

void goto_sleep()
{

    // Please see the 'Important Note' at top of this file for more information on the following work arounds

    static int cycle = 1;
    int64_t cycle_time;
    int64_t sleep_time;

    // if we have had a relatively serious problem force deep sleep rather than light sleep
    // this will effectively reset the esp32
    if (!WiFi_is_connected || !BME680_readings_are_reasonable || MQTT_unknown_error || PWSWeather_unknown_error)
        light_sleep_enabled = false;

    // report processing time for this cycle (processing time excludes sleep time)

    cycle_time = esp_timer_get_time() - cycle_start_time;

    if (cycle == 1)
        ESP_LOGW(TAG, "initial startup and cycle %d processing time: %f seconds", cycle++, (float)((float)cycle_time / (float)1000000));
    else
        ESP_LOGW(TAG, "cycle %d processing time: %f seconds", cycle++, (float)((float)cycle_time / (float)1000000));

    if (light_sleep_enabled && (GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH == 1))
    // automatic light sleep approach
    {

        if (cycle_time < ((GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60) * 1000000))
        {

            ESP_LOGI(TAG, "begin automatic light sleep for %d seconds\n", GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60);

            enable_power_save_mode(true);

            // with automatic light sleep we don't actually call light sleep
            // rather we delay for the required time
            // and power management seeing the delay kicks in automatic light sleep
            const uint64_t convert_from_microseconds_to_milliseconds_by_division = 1000;
            vTaskDelay(((GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60 * 1000000) - cycle_time) / convert_from_microseconds_to_milliseconds_by_division / portTICK_PERIOD_MS);

            enable_power_save_mode(false);
        }
        else
            ESP_LOGI(TAG, "skipping automatic light sleep (already running late for the next cycle)");
    }

    else if (light_sleep_enabled && (GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH == 2))
    // manual light sleep approach
    {

        if (cycle_time < ((GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60) * 1000000))
        {
            going_to_sleep = true;

            ESP_ERROR_CHECK(esp_wifi_stop()); // turn off wifi to save power

            while (WiFi_is_connected)
                vTaskDelay(20 / portTICK_PERIOD_MS);

            ESP_LOGI(TAG, "begin manual light sleep for %d seconds\n", GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60);

            vTaskDelay(20 / portTICK_PERIOD_MS); // provide some time to finalize writing to the log (this is not optional if you want to see the above log entry written)

            sleep_time = ((GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60) * 1000000) - cycle_time;
            esp_sleep_enable_timer_wakeup(sleep_time);

            esp_light_sleep_start();

            going_to_sleep = false;

            ESP_ERROR_CHECK(esp_wifi_start()); // turn wifi back on
        }
        else
            ESP_LOGI(TAG, "skipping manual light sleep (already running late for the next cycle)");
    }

    else
    // deep sleep approach
    {

        if (cycle_time < ((GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60) * 1000000))
        {

            if (GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH == 0)
                ESP_LOGI(TAG, "begin deep sleep for %d seconds\n", (GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60)); // show as info as using deep sleep was the user's choice
            else
                ESP_LOGW(TAG, "begin deep sleep for %d seconds\n", (GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60)); // show as warning as using deep sleep was not the user's choice

            vTaskDelay(20 / portTICK_PERIOD_MS); // provide some time to finalize writing to the log (this is not optional if you want to see the above log entry written)

            esp_sleep_enable_timer_wakeup(((GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60) * 1000000) - cycle_time);
            esp_deep_sleep_start();
        }
        else
        {
            ESP_LOGI(TAG, "skipping deep sleep (already running late for the next cycle); restarting now");
            vTaskDelay(20 / portTICK_PERIOD_MS); // provide some time to finalize writing to the log (this is not optional if you want to see the above log entry written)
            esp_restart();
        }
    }

    // reset the cycle start time
    cycle_start_time = esp_timer_get_time();

    ESP_LOGI(TAG, "awake from sleep");
}

void initalize_non_volatile_storage()
{

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void initialize_the_external_switch()
{
    // Initialize the external switch used to determine if the readings should be reported to PWSWeather.com
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << GENERAL_USER_SETTINGS_EXTERNAL_SWITCH_GPIO_PIN;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);
}

void restart_after_two_minutes()
{
    ESP_LOGE(TAG, "delaying for two minutes and then restarting");
    vTaskDelay(20 / portTICK_PERIOD_MS);
    esp_sleep_enable_timer_wakeup(2 * 60 * 1000000);
    esp_deep_sleep_start();
}

void startup_validations_and_displays()
{

    ESP_LOGI(TAG, "\n\n\n************************\n* Weather Station v1.1 *\n************************");

#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
    bool tickless_idle_enabled = true;
#else
    bool tickless_idle_enabled = false;
#endif

    if (GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES <= 0)
    {
        ESP_LOGE(TAG, "invalid reporting frequency");
        restart_after_two_minutes();
    };

    if ((GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH < 0) || (GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH > 2))
    {
        ESP_LOGE(TAG, "invalid sleep approach");
        restart_after_two_minutes();
    };

    if ((GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH == 1) && !tickless_idle_enabled)
    {
        ESP_LOGE(TAG, "automatic light sleep requires tickless idle to be enabled");
        restart_after_two_minutes();
    }

    if ((GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH == 2) && tickless_idle_enabled)
    {
        ESP_LOGE(TAG, "manual light sleep requires tickless idle to be disabled");
        restart_after_two_minutes();
    };

    if (GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH == 0)
        ESP_LOGI(TAG, "sleep approach: deep sleep");
    else if (GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH == 1)
        ESP_LOGI(TAG, "sleep approach: automatic light sleep");
    else if (GENERAL_USER_SETTINGS_USE_AUTOMATIC_SLEEP_APPROACH == 2)
        ESP_LOGI(TAG, "sleep approach: manual light sleep");

    ESP_LOGI(TAG, "sleep time between cycles: %d seconds", GENERAL_USER_SETTINGS_REPORTING_FREQUENCY_IN_MINUTES * 60);
}

void connect_to_WiFi()
{

    // Check if Wi-Fi is already connected

    wifi_ap_record_t ap_info;
    esp_wifi_sta_get_ap_info(&ap_info);
    if (ap_info.rssi != 0)
    {
        ESP_LOGI(TAG, "WIFI was previously connected, reconnecting (%d)", (int)ap_info.rssi);
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        ESP_ERROR_CHECK(esp_wifi_connect());
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
    else
    {
        ESP_LOGW(TAG, "WIFI was previously not connected, connecting");
        turn_on_Wifi();
    };

    if (!WiFi_is_connected)
    {
        ESP_LOGE(TAG, "WIFI is not connected");
        restart_after_two_minutes();
    }
}

void app_main(void)
{

    startup_validations_and_displays();

    initalize_non_volatile_storage();

    initialize_power_management();

    initialize_the_external_switch();

    connect_to_WiFi();

    while (true)
    {
        get_bme680_readings();

        if (BME680_readings_are_reasonable)
        {
            publish_readings_via_MQTT();
            publish_readings_to_PWSWeather();
        }
        else
        {
            ESP_LOGE(TAG, "couldn't get a valid reading from the BME680; please check the wiring;");
            restart_after_two_minutes();
        };

        goto_sleep();
    }
}