// Copyright Rob Latour, 2023
// License MIT

#include "user_settings_general.h"
#include "user_settings_secret.h"

#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

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

#include "mqtt_client.h"

#include "esp_http_client.h"

#include <cJSON.h>

#include <sys/param.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"

#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include <esp_http_client.h>

// wifi
#define DEFAULT_SSID USER_SETTINGS_SECRET_SSID
#define DEFAULT_PWD USER_SETTINGS_SECRET_PASSWORD

// mqtt
#define MQTT_USER_ID USER_SETTINGS_SECRET_MQTT_USER_ID
#define MQTT_USER_PASS USER_SETTINGS_SECRET_MQTT_USER_PASS
#define MQTT_BROKER_URL USER_SETTINGS_GENERAL_MQTT_BROKER_URL
#define MQTT_BROKER_PORT USER_SETTINGS_GENERAL_MQTT_BROKER_PORT
#define MQTT_QOS USER_SETTINGS_GENERAL_MQTT_QOS
#define MQTT_RETAIN USER_SETTINGS_GENERAL_MQTT_RETAIN
#define MQTT_TOPIC USER_SETTINGS_GENERAL_MQTT_TOPIC
#define DEFAULT_LISTEN_INTERVAL 100
#define DEFAULT_BEACON_TIMEOUT 15000

// PWSWeather.com
#define PWS_STATION_ID USER_SETTINGS_SECRET_PWS_STATION_ID
#define SECRET_PSW_API_KEY USER_SETTINGS_SECRET_PWS_API_KEY

// GPIO PIN attached to an external physical switch used to toggle on / off reporting to PWSWeather.com
#define EXTERNAL_SWITCH_GPIO_PIN USER_SETTINGS_GENERAL_EXTERNAL_SWITCH_GPIO_PIN

// reporting frequency
#define MINUTES_BETWEEN_READINGS USER_SETTINGS_GENERAL_MINUTES_BETWEEN_READINGS

// time out period (in seconds) to get all the publishing done
#define PUBLISHING_TIMEOUT_PERIOD USER_SETTINGS_GENERAL_PUBLISHING_TIMEOUT_PERIOD

// debugging
static const char *TAG = USER_SETTINGS_GENERAL_TAG;

// BME680 sensor
#define BME680_I2C_ADDR USER_SETTINGS_GENERAL_BME680_I2C_ADDR
#define PORT USER_SETTINGS_GENERAL_PORT
#define I2C__SDA USER_SETTINGS_GENERAL_I2C_SDA
#define I2C__SCL USER_SETTINGS_GENERAL_I2C_SCL

#define HTTP_RESPONSE_BUFFER_SIZE 1024
#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048

// Used to power up and down the BME680 sensor
#define POWER_SENSOR_CONTROLLER_PIN USER_SETTINGS_GENERAL_POWER_SENSOR_CONTROLLER_PIN
#define POWER_ON 1
#define POWER_OFF 0

// Misc
#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#if CONFIG_EXAMPLE_POWER_SAVE_MIN_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MIN_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_MAX_MODEM
#define DEFAULT_PS_MODE WIFI_PS_MAX_MODEM
#elif CONFIG_EXAMPLE_POWER_SAVE_NONE
#define DEFAULT_PS_MODE WIFI_PS_NONE
#else
#define DEFAULT_PS_MODE WIFI_PS_NONE
#endif

// Global variables

volatile bool BME680_readings_are_valid;
volatile float temperature;
volatile float humidity;
volatile float pressure;

volatile bool WiFiReady;

volatile bool MQTT_Publishing_Done;
volatile bool PWSWeather_Publishing_Done;

/*
void confirm_protocol_in_use()
{

    wifi_phy_mode_t WiFiMode;
    if (esp_wifi_sta_get_negotiated_phymode(&WiFiMode) == ESP_OK)
    {
        if (WiFiMode == WIFI_PHY_MODE_11B)
            ESP_LOGI(TAG, "11b");

        if (WiFiMode  == WIFI_PHY_MODE_11G)
            ESP_LOGI(TAG, "11g");

        if (WiFiMode == WIFI_PHY_MODE_LR)
            ESP_LOGI(TAG, "Low rate");

        if (WiFiMode == WIFI_PHY_MODE_HT20)
            ESP_LOGI(TAG, "HT20");

        if (WiFiMode == WIFI_PHY_MODE_HT40)
            ESP_LOGI(TAG, "HT40");

        if (WiFiMode == WIFI_PHY_MODE_HE20)
            ESP_LOGI(TAG, "HE20");  // this is WiFi 6
    }
}

*/

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{

    static int confirmed_published_readings = 0;

    esp_mqtt_client_handle_t client = event->client;

    switch (event->event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGV(TAG, "MQTT_EVENT_BEFORE_CONNECT");
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGV(TAG, "MQTT_EVENT_CONNECTED");

        // used to confirm when mqtt messages have all been published
        // this is done by having this program subscribe to the topics which it will itself be publishing
        // in this way this program can confirm the published topics have actually being processed by the MQTT Server
        // also, this program will count the number of publications received, and when it receives three
        // (one for temp, one for pressure, and one for humidity) then it will know all publishing is complete for the current cycle
        // and it can advance towards deep sleep
        char TopicAndAllSubTopics[100] = MQTT_TOPIC;
        strcat(TopicAndAllSubTopics, "/#");
        esp_mqtt_client_subscribe(client, TopicAndAllSubTopics, 2);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGV(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGV(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGV(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGV(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        // ESP_LOGV(TAG, "MQTT_EVENT_DATA");
        // ESP_LOGI(TAG, "TOPIC = %.*s", event->topic_len, event->topic);
        // ESP_LOGI(TAG, "DATA = %.*s", event->data_len, event->data);
        confirmed_published_readings++;
        // once we have three readings (temp, pressure and humidity) set the MQTT_Publishing_Done flag to confirmed our job here is done
        if (confirmed_published_readings >= 3)
            MQTT_Publishing_Done = true;
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        break;

    default:
        ESP_LOGW(TAG, "Other mqtt event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "mqtt_event_handler event: base=%s, event_id=%ld", base, event_id);
    mqtt_event_handler_cb(event_data);
}

esp_mqtt_client_handle_t client;

static void mqtt_app_start(void)
{

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URL,
        .broker.address.port = MQTT_BROKER_PORT,
        .credentials.username = MQTT_USER_ID,
        .credentials.authentication.password = MQTT_USER_PASS,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}

void get_bme680_readings(void *pvParameters)
{
    // Gets temperature, pressure and humidity from the BME680.
    // Do not get gas_resistance as gas_resistance readings.
    // If successful return true; otherwise return false;

    vTaskDelay(100 / portTICK_PERIOD_MS); // short pause so this starts at the optional time
    ESP_LOGV(TAG, "Get readings");

    temperature = 0;
    humidity = 0;
    pressure = 0;

    bme680_t sensor;

    // power up the BME680 sensor
    ESP_LOGI(TAG, "Power on the BME680");
    gpio_reset_pin(POWER_SENSOR_CONTROLLER_PIN);
    gpio_set_direction(POWER_SENSOR_CONTROLLER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(POWER_SENSOR_CONTROLLER_PIN, POWER_ON);

    // initialize the sensor
    ESP_ERROR_CHECK(i2cdev_init());
    memset(&sensor, 0, sizeof(bme680_t));
    ESP_ERROR_CHECK(bme680_init_desc(&sensor, BME680_I2C_ADDR, PORT, I2C__SDA, I2C__SCL));
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

    // get the time (duration) needed for a valid set of readings
    uint32_t duration;
    bme680_get_measurement_duration(&sensor, &duration);

    bme680_values_float_t values;

    int tries = 0;
    int max_tries = 5;

    while (!BME680_readings_are_valid && (tries < max_tries))
    {
        ESP_LOGI(TAG, "Take the readings now");
        if (bme680_force_measurement(&sensor) == ESP_OK)
        {
            vTaskDelay(duration); // wait until measurement results are available

            if (bme680_get_results_float(&sensor, &values) == ESP_OK)
                BME680_readings_are_valid = (values.pressure > 0);

            if (BME680_readings_are_valid)
            {
                temperature = values.temperature;
                humidity = values.humidity;
                pressure = values.pressure;
            }
        }
        tries++;
    };

    // power down the BME680 sensor
    ESP_LOGI(TAG, "Power off the BME680");
    gpio_set_level(POWER_SENSOR_CONTROLLER_PIN, POWER_OFF);

    if (BME680_readings_are_valid)
        ESP_LOGI(TAG, "\n\n*** Readings:\n***  Temperature:  %.2f °C\n***  Humidity:     %.2f %%\n***  Pressure:    %.2f hPa\n", temperature, humidity, pressure);
    else
        ESP_LOGE(TAG, "Could not get valid readings from the BME680");

    vTaskDelete(NULL);
}

void publish_a_reading(const char *subtopic, float value)
{

    char topic[100] = MQTT_TOPIC;
    strcat(topic, "/");
    strcat(topic, subtopic);

    char payload[13];
    sprintf(payload, "%g", value);

    // ESP_LOGI(TAG, "Publish");
    // ESP_LOGI(TAG, "   Topic:    %s", topic);
    // ESP_LOGI(TAG, "   Payload:  %s", payload);
    esp_mqtt_client_publish(client, topic, payload, 0, MQTT_QOS, MQTT_RETAIN);
}

// Callback function for HTTP events
esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG, "HTTP_EVENT_ERROR\n");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED\n");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT\n");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER\n");
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA\n");
        ESP_LOGI(TAG, "%.*s", evt->data_len, (char *)evt->data);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH\n");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED\n");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_REDIRECT\n");
        esp_http_client_set_header(evt->client, "From", "user@example.com");
        esp_http_client_set_header(evt->client, "Accept", "text/html");
        esp_http_client_set_redirection(evt->client);
    }
    return ESP_OK;
}

// Function to upload weather data to PWSweather
void publish_readings_to_PWSWeather(void *pvParameters)
{

    // Create the HTTP client
    esp_http_client_config_t config = {
        .url = "https://pwsupdate.pwsweather.com/api/v1/submitwx?",
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // Set the URL line
    char url_line[250];
    sprintf(url_line, "ID=%s&PASSWORD=%s&dateutc=now&tempf=%.1f&humidity=%.1f&baromin=%.2f&softwaretype=ESP32DIY&action=updateraw",
            PWS_STATION_ID, SECRET_PSW_API_KEY, temperature * 1.8 + 32.0, (float)humidity, pressure * 0.02952998751);

    ESP_LOGI(TAG, "%s", url_line);

    // Set the HTTP headers
    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_header(client, "Content-Length", "0");

    // Set the POST data length
    esp_http_client_set_post_field(client, url_line, strlen(url_line));

    // Perform the HTTP request
    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK)
    {
        printf("Error: esp_http_client_perform failed with error code %d\n", err);
    }

    // Cleanup
    esp_http_client_cleanup(client);

    PWSWeather_Publishing_Done = true;
    vTaskDelete(NULL);
}

void publish_readings_via_MQTT(void *pvParameters)
{

    ESP_LOGV(TAG, "Start MQTT");
    mqtt_app_start();

    ESP_LOGV(TAG, "Publish readings");

    publish_a_reading("temperature", temperature);
    publish_a_reading("humidity", humidity);
    publish_a_reading("pressure", pressure);

    vTaskDelete(NULL);
}

// Wifi logic stuff

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{

    if (event_base == WIFI_EVENT)

        switch (event_id)
        {

        case WIFI_EVENT_STA_START:
            ESP_LOGV(TAG, "WiFi start");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected");
            // confirm_protocol_in_use(); // may be used in testing to confirm a wifi 6 connection
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "WiFi disconnected");
            ESP_LOGI(TAG, "WiFi attempt to reconnect");
            esp_wifi_connect();
            break;

        default:
            ESP_LOGW(TAG, "unhandled WiFi event id: %ld \n", event_id);
            break;
        }

    else if (event_base == IP_EVENT)

        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
            WiFiReady = true;
            break;

        default:
            ESP_LOGW(TAG, "unhandled IP event id: %ld \n", event_id);
            break;
        }

    else
        ESP_LOGW(TAG, "unhandled event base and event id: %s %ld \n", event_base, event_id);
}

// init wifi as sta and set power save mode
static void start_wifi(void)
{

    // This subroutine starts the WiFi.
    // Once the WiFi event handler see that the WiFi has been connected and an IP address has been assigned
    // it will call the subroutine to publish the readings

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = DEFAULT_SSID,
            .password = DEFAULT_PWD,
            .listen_interval = DEFAULT_LISTEN_INTERVAL,
            .pmf_cfg = {
                .capable = true,
                .required = false},
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AX));

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_STA, DEFAULT_BEACON_TIMEOUT));
    esp_wifi_set_ps(DEFAULT_PS_MODE); // set power saving mode
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

void configure_power_management()
{

#if CONFIG_PM_ENABLE
    // Configure dynamic frequency scaling:
    // maximum and minimum frequencies are set in sdkconfig,
    // automatic light sleep is enabled if tickless idle support is enabled.
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_EXAMPLE_MAX_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_EXAMPLE_MIN_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#endif
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
#endif // CONFIG_PM_ENABLE
};

void initialize_the_external_switch()
{
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << EXTERNAL_SWITCH_GPIO_PIN;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);
}

void app_main(void)
{

    ESP_LOGI(TAG, "********************");
    ESP_LOGI(TAG, "Weather Station v1.0");

    // Initialize NVS
    initalize_non_volatile_storage();

    // configure power management
    configure_power_management();

    // Initialize the external switch used to determine if the readings taken should be reported to PWSWeather.com
    initialize_the_external_switch();

    // Get the BME680 readings; this is done as a task so that it can be run in parallel with the starting of the Wifi
    BME680_readings_are_valid = false;
    xTaskCreate(get_bme680_readings, "GetReadings", 1024 * 2, NULL, 20, NULL);

    // start the WiFi and wait for it to connect; max wait time is one half the PUBLISHING_TIMEOUT_PERIOD
    int64_t publishing_start_time = esp_timer_get_time();
    int64_t timeout = +publishing_start_time + (PUBLISHING_TIMEOUT_PERIOD / 2) * 1000000;
    WiFiReady = false;
    start_wifi();

    // wait for the readings to be taken and the wifi to connect
    while (!BME680_readings_are_valid && (esp_timer_get_time() < timeout))
        vTaskDelay(100 / portTICK_PERIOD_MS);

    // check if external switch is toggled on to report results to PWSWeather.com
    bool publish_to_pwsweather = (gpio_get_level(EXTERNAL_SWITCH_GPIO_PIN) == 0);

    while (!WiFiReady && (esp_timer_get_time() < timeout))
        vTaskDelay(100 / portTICK_PERIOD_MS);

    // if the readings are valid and the WiFi is connected then
    // publish the results via MQTT, and
    // publish the results to PWSWeather.com (as required)

    if (BME680_readings_are_valid)
    {
        if (WiFiReady)
        {
            // publishing to PWSWeather usually takes longer than publishing to the MQTT Server
            //
            // the goal of the code below is to get the publishing jobs done as quickly as possible while at the same time smoothing
            // out the power consumption (i.e. reducing the peak current draw level).
            //
            // so with this in mind:
            //
            // first, if required:
            //
            //    start a task to publish to PWSWeather
            //    and then insert a short delay so the esp32 can concentrate on starting up the publishing to PWSWeather
            //
            // second,
            //
            //    start a parallel task to publish to MQTT (which  will likely complete well before the PWSWeather task)
            //
            // As the MQTT publishing can then be completed (in most cases) before the PWSWeather returns its results
            // we are effectively using the wait time for PWSWeather to do all of the MQTT reporting.
            //
            // This has two advantages as noted above:
            //  First, as the two publishing tasks run in parallel (as opposed to sequentially) the program can get to deep sleep sooner.
            //  Second, as MQTT publishing task starts a couple seconds after the publishing to PWSWeather task, this program is effectively
            //  flattening the power consumption curve by staggering their startup times (as both tasks are briefly power hungry when first started)
            //  in my very limited testing, the two second delay dropped the peak power consumption point as much as a third

            ESP_LOGI(TAG, "Publish via PWSWeather is switched %s", publish_to_pwsweather ? "on" : "off");

            if (publish_to_pwsweather)
            {
                PWSWeather_Publishing_Done = false;
                xTaskCreate(publish_readings_to_PWSWeather, "PWSWeather", 1024 * 8, NULL, 10, NULL);
                //
                // Add a short delay before starting a parallel task to publish via MQTT.
                // As noted above, this will give the task publishing to PWSWeather.com time to get underway before the MQTT publishing starts.
                //
                vTaskDelay(100 / portTICK_PERIOD_MS);
            };

            MQTT_Publishing_Done = false;
            ESP_LOGI(TAG, "Publish via MQTT");
            xTaskCreate(publish_readings_via_MQTT, "MQTT", 1024 * 2, NULL, 5, NULL);

            // if they exist, wait for the two tasks above to complete.
            // if they don't complete within the timeout period continue on towards deep sleep

            // set a timeout period to get all the above publishing done
            timeout = publishing_start_time + PUBLISHING_TIMEOUT_PERIOD * 1000000;

            // wait until MQTT and PWSWeather are both done reporting or the time out period has elapsed (whichever comes first)
            // note: MQTT is not done reporting when its started tasks ends, it is done when its event handler confirms its publishing done

            ESP_LOGI(TAG, "waiting on MQTT");
            while (!MQTT_Publishing_Done && (esp_timer_get_time() < timeout))
                vTaskDelay(200 / portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "MQTT done");

            if (publish_to_pwsweather)
            {
                ESP_LOGI(TAG, "waiting on PWSWeather");
                while (!PWSWeather_Publishing_Done && (esp_timer_get_time() < timeout))
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                ESP_LOGI(TAG, "PWSWeather done");
            };
        }
        else
        {
            ESP_LOGI(TAG, "Couldn't connect to WiFi");
        }
    }
    else
    {
        ESP_LOGI(TAG, "Couldn't get valid BME680 readings");
    };

    // Report run time
    float runtime_in_seconds = (float)esp_timer_get_time() / (float)1000000;
    ESP_LOGI(TAG, "run time: %f seconds", runtime_in_seconds);

    // Put the program into deep sleep
    ESP_LOGI(TAG, "Go to sleep");
    esp_sleep_enable_timer_wakeup(MINUTES_BETWEEN_READINGS * 60 * 1000000 - esp_timer_get_time());
    esp_deep_sleep_start();
}