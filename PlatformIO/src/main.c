/**
 * PlatformIO based firmware for LilyGO TTGO T-Higrow ESP32 plant sensor.
 * Measures soil moisture and battery level, and published measurements
 * over MQTT.
 *
 * Optimized for low power consumption, so you can run your plant sensor
 * for months on a small LiPo battery.
 *
 * TODO:
 * - average over 10 samples
 * - simpler event handling (https://esp32tutorials.com/esp32-mqtt-client-publish-subscribe-esp-idf/)
*/

#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_adc_cal.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

// Copy secrets-example.h to secrets.h and configure your wifi and MQTT settings
#include "secrets.h"

const char* TAG = "plantsensor";

// Event group to signal connection to wifi and MQTT
static EventGroupHandle_t event_group;

#define WIFI_CONNECTED BIT0
#define MQTT_CONNECTED BIT1

void enable_sensors()
{
    // Pull pin 4 high to enable sensor power
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_4, 1);
}

float clamp_percentage(float value)
{
    return value < 0.0f ? 0.0f : (value > 100.0f ? 100.0f : value);
}

/*
    Air:            1.85V @ 6db*   2.77V @ 11db
    Very dry soil:  1.85V @ 6db*   2.76V @ 11db
    Wet soil:       1.78V @ 6db    1.53V @ 11db
    Very wet soil:  1.35V @ 6db    1.35V @ 11db
    Water:          1.37V @ 6db    1.39V @ 11db
    *Out of range
*/
float measure_soil_moisture()
{
    // ADC1_CHANNEL_4 == GPIO32
    esp_adc_cal_characteristics_t calibration;
    ESP_ERROR_CHECK(esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &calibration));
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_4, ADC_ATTEN_DB_11));

    const int raw = adc1_get_raw(ADC1_CHANNEL_4);
    const uint32_t mV = esp_adc_cal_raw_to_voltage(raw, &calibration);
    const float min = 1350.0;
    const float max = 2770.0;
    const float percentage = clamp_percentage(100.0 * (max - mV) / (max - min));

    ESP_LOGI(TAG, "Measurement soil: %d mV (raw=%d) = %.0f%%", mV, raw, percentage);

    return percentage;
}

/*
  Battery  ADC (atten 11db)  Sample
  4.2 V    1900 mV           100%
  4.0 V    1820 mV            84%
  3.8 V    1700 mV            60%
  3.6 V    1590 mV            38%
  3.4 V    1500 mV            20%
  3.2 V    1410 mV             2%
*/
float measure_battery_percentage()
{
    // ADC1_CHANNEL_5 == GPIO33
    esp_adc_cal_characteristics_t calibration;
    ESP_ERROR_CHECK(esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_DEFAULT, 0, &calibration));
    ESP_ERROR_CHECK(adc1_config_channel_atten(ADC1_CHANNEL_5, ADC_ATTEN_DB_11));

    const int raw = adc1_get_raw(ADC1_CHANNEL_5);
    const uint32_t mV = esp_adc_cal_raw_to_voltage(raw, &calibration);
    const float percentage = clamp_percentage((mV - 1400) / 5.0);

    ESP_LOGI(TAG, "Measurement battery: %u mV (raw=%d) = %.0f%%", mV, raw, percentage);

    return percentage;
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(event_group, WIFI_CONNECTED);
    }
}

void connect_to_wifi()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));

    esp_event_handler_instance_t wifi_start;
    esp_event_handler_instance_t got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        WIFI_EVENT_STA_START,
                                                        &event_handler,
                                                        NULL,
                                                        &wifi_start));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &got_ip));

    wifi_config_t wifi_config = {
        .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS }
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait until connected
    xEventGroupWaitBits(event_group, WIFI_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_start));
}

static void mqtt_publish_discovery(esp_mqtt_client_handle_t client, const char* id, const char* name, const char* class, const char* unit)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/plantsensor/%s/config", id);

    char contents[512];
    snprintf(contents, sizeof(contents),
        "{\"dev_cla\": \"%s\","
        "\"unit_of_meas\": \"%s\","
        "\"stat_cla\": \"measurement\","
        "\"name\": \"%s\","
        "\"stat_t\": \"plantsensor/sensor/%s/state\","
        "\"uniq_id\": \"ESPsensor%s\","
        "\"dev\": {"
        "  \"ids\": \"1\","
        "  \"name\": \"plantsensor\","
        "  \"sw\": \"0.1\","
        "  \"model\": \"T-Higrow\","
        "  \"mf\": \"LilyGO\"}}", class, unit, name, id, id);

    if (esp_mqtt_client_publish(client, topic, contents, 0, 0, 0) == -1) {
        ESP_LOGE(TAG, "Failed to publish MQTT %s discovery data", id);
    }
}

static void mqtt_publish_measurements(esp_mqtt_client_handle_t client, float battery, float soil)
{
    char buffer[32];

    snprintf(buffer, sizeof(buffer), "%.0f", soil);
    if (esp_mqtt_client_publish(client, "plantsensor/sensor/soil/state", buffer, 0, 0, 0) == -1) {
        ESP_LOGE(TAG, "Failed to publish MQTT state");
    }

    snprintf(buffer, sizeof(buffer), "%.0f", battery);
    if (esp_mqtt_client_publish(client, "plantsensor/sensor/battery/state", buffer, 0, 0, 0) == -1) {
        ESP_LOGE(TAG, "Failed to publish MQTT state");
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connected to MQTT server");
        xEventGroupSetBits(event_group, MQTT_CONNECTED);
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event_id);
        break;
    }
}

static esp_mqtt_client_handle_t mqtt_app_start(void)
{
    ESP_LOGI(TAG, "Starting MQTT");
    esp_mqtt_client_config_t mqttConfig = {
        .uri = MQTT_URI,
        .client_id = "plantsensor",
        .username = MQTT_USER,
        .password = MQTT_PASS
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqttConfig);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));

    return client;
}

void init_non_volatile_storage()
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
}

struct measurements {
    float battery;
    float soil;
};

RTC_DATA_ATTR struct measurements last_published = { 0.0, 0.0 };
RTC_DATA_ATTR uint32_t wakeup_count = 0;

void app_main()
{
    wakeup_count++;

    enable_sensors();

    // Measuring too quickly after enabling the sensors gives weird readings
    vTaskDelay(500 / portTICK_PERIOD_MS);

    const float battery = measure_battery_percentage();
    const float soil = measure_soil_moisture();

    ESP_LOGI(TAG, "Battery: %.1f%% (change: %.1f%%)  Soil: %.1f%% (change: %.1f%%)",
        battery,
        battery - last_published.battery,
        soil,
        soil - last_published.soil);

    // Only publish measurements if they were different from previously published measurements
    if (wakeup_count == 1 || abs(battery - last_published.battery) >= 1 || abs(soil - last_published.soil) >= 1) {
        event_group = xEventGroupCreate();

        init_non_volatile_storage();
        connect_to_wifi();
        esp_mqtt_client_handle_t client = mqtt_app_start();

        xEventGroupWaitBits(event_group, MQTT_CONNECTED, pdFALSE, pdFALSE, portMAX_DELAY);

        // Only publish discovery data when restarted (not when waking up)
        if (wakeup_count == 1) {
            mqtt_publish_discovery(client, "battery", "Battery", "battery", "%");
            mqtt_publish_discovery(client, "soil", "Soil", "moisture", "%");
        }

        mqtt_publish_measurements(client, battery, soil);
        ESP_ERROR_CHECK(esp_mqtt_client_stop(client));

        last_published.battery = battery;
        last_published.soil = soil;
    }

    ESP_LOGI(TAG, "================================ Going to sleep ================================");
    esp_deep_sleep(60 * 1000 * 1000);
}
