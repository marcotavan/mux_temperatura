#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "onewire_bus.h"
#include "ds18b20.h"

#define MAX_SENSORS 8
#define MQTT_UP BIT0
static const char *TAG = "centrale";
static EventGroupHandle_t events;
static esp_mqtt_client_handle_t mqtt;
static char node[32], availability[96];
typedef struct {
    ds18b20_device_handle_t handle;
    uint64_t rom;
} sensor_t;
static sensor_t sensors[MAX_SENSORS];
static int sensor_count;

static void publish(const char *topic, const char *payload, bool retain)
{
    if (esp_mqtt_client_enqueue(mqtt, topic, payload, 0, 1, retain, true) < 0) {
        ESP_LOGW(TAG, "Coda MQTT piena: %s", topic);
    }
}

static void discovery(const sensor_t *s)
{
    char id[64], topic[128], state[128], health[128], name[48];
    snprintf(id, sizeof(id), "%s_%016" PRIx64, node, s->rom);
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/config", id);
    snprintf(state, sizeof(state), "centrale/%s/%016" PRIx64 "/temperature", node, s->rom);
    snprintf(health, sizeof(health), "centrale/%s/%016" PRIx64 "/availability", node, s->rom);
    snprintf(name, sizeof(name), "Sonda %016" PRIx64, s->rom);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "unique_id", id);
    cJSON_AddStringToObject(root, "state_topic", state);
    cJSON_AddStringToObject(root, "device_class", "temperature");
    cJSON_AddStringToObject(root, "state_class", "measurement");
    cJSON_AddStringToObject(root, "unit_of_measurement", "°C");
    cJSON_AddNumberToObject(root, "expire_after", CONFIG_CT_PERIOD * 3);
    cJSON_AddStringToObject(root, "availability_mode", "all");
    cJSON *av = cJSON_AddArrayToObject(root, "availability");
    cJSON *a = cJSON_CreateObject();
    cJSON_AddStringToObject(a, "topic", availability);
    cJSON_AddItemToArray(av, a);
    a = cJSON_CreateObject();
    cJSON_AddStringToObject(a, "topic", health);
    cJSON_AddItemToArray(av, a);
    cJSON *device = cJSON_AddObjectToObject(root, "device");
    cJSON *ids = cJSON_AddArrayToObject(device, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString(node));
    cJSON_AddStringToObject(device, "name", node);
    cJSON_AddStringToObject(device, "manufacturer", "DIY");
    cJSON_AddStringToObject(device, "model", CONFIG_IDF_TARGET);
    cJSON_AddStringToObject(device, "sw_version", "0.1.0");
    char *json = cJSON_PrintUnformatted(root);
    if (json) { publish(topic, json, true); cJSON_free(json); }
    cJSON_Delete(root);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == MQTT_EVENT_CONNECTED) {
        xEventGroupSetBits(events, MQTT_UP);
        ESP_LOGI(TAG, "MQTT connesso");
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(events, MQTT_UP);
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, MQTT_UP);
        esp_wifi_connect();
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) ESP_LOGI(TAG, "Wi-Fi connesso");
}

static void scan(onewire_bus_handle_t bus)
{
    onewire_device_iter_handle_t it;
    if (onewire_new_device_iter(bus, &it) != ESP_OK) return;
    onewire_device_t dev;
    esp_err_t err;
    while ((err = onewire_device_iter_get_next(it, &dev)) == ESP_OK) {
        bool known = false;
        for (int i = 0; i < sensor_count; i++) if (sensors[i].rom == dev.address) known = true;
        if (known || (dev.address & 0xff) != 0x28) continue;
        if (sensor_count == MAX_SENSORS) { ESP_LOGW(TAG, "Limite di 8 sonde raggiunto"); break; }
        ds18b20_config_t cfg = {};
        ds18b20_device_handle_t handle;
        if (ds18b20_new_device_from_enumeration(&dev, &cfg, &handle) != ESP_OK) continue;
        sensors[sensor_count++] = (sensor_t){ .handle = handle, .rom = dev.address };
        ESP_LOGI(TAG, "Sonda trovata: %016" PRIx64, (uint64_t)dev.address);
    }
    if (err != ESP_OK && err != ESP_ERR_NOT_FOUND) ESP_LOGW(TAG, "Ricerca sonde: %s", esp_err_to_name(err));
    onewire_del_device_iter(it);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    events = xEventGroupCreate();
    assert(events);
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(node, sizeof(node), "centrale_%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(availability, sizeof(availability), "centrale/%s/availability", node);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t wifi = {};
    if (strlen(CONFIG_CT_WIFI_SSID) > sizeof(wifi.sta.ssid) ||
        strlen(CONFIG_CT_WIFI_PASSWORD) > sizeof(wifi.sta.password)) {
        ESP_LOGE(TAG, "Credenziali Wi-Fi troppo lunghe");
        return;
    }
    memcpy(wifi.sta.ssid, CONFIG_CT_WIFI_SSID, strlen(CONFIG_CT_WIFI_SSID));
    memcpy(wifi.sta.password, CONFIG_CT_WIFI_PASSWORD, strlen(CONFIG_CT_WIFI_PASSWORD));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    if (strlen(CONFIG_CT_WIFI_SSID)) ESP_ERROR_CHECK(esp_wifi_start());
    else ESP_LOGW(TAG, "SSID vuoto: configurare Centrale termica in menuconfig");

    esp_mqtt_client_config_t mc = {
        .broker.address.uri = CONFIG_CT_MQTT_URI,
        .credentials.username = CONFIG_CT_MQTT_USER,
        .credentials.authentication.password = CONFIG_CT_MQTT_PASSWORD,
        .credentials.client_id = node,
        .session.last_will = { .topic = availability, .msg = "offline", .qos = 1, .retain = true },
        .session.keepalive = 30,
        .buffer.size = 2048,
        .outbox.limit = 16384,
    };
    mqtt = esp_mqtt_client_init(&mc);
    assert(mqtt);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL));
    if (strlen(CONFIG_CT_WIFI_SSID)) ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt));

    onewire_bus_handle_t bus;
    onewire_bus_config_t bc = { .bus_gpio_num = CONFIG_CT_GPIO };
    onewire_bus_rmt_config_t rc = { .max_rx_bytes = 10 };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bc, &rc, &bus));
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        scan(bus);
        esp_err_t conversion = ds18b20_trigger_temperature_conversion_for_all(bus);
        // The driver waits 800 ms for all 12-bit conversions to finish.
        bool connected = (xEventGroupGetBits(events) & MQTT_UP) != 0;
        if (connected) publish(availability, "online", true);
        for (int i = 0; i < sensor_count; i++) {
            float temperature = NAN;
            err = conversion == ESP_OK ? ds18b20_get_temperature(sensors[i].handle, &temperature) : conversion;
            bool valid = err == ESP_OK && isfinite(temperature) && temperature >= -55 && temperature <= 125;
            if (valid) ESP_LOGI(TAG, "%016" PRIx64 ": %.2f C", sensors[i].rom, temperature);
            else ESP_LOGW(TAG, "%016" PRIx64 ": lettura non valida (%s)", sensors[i].rom, esp_err_to_name(err));
            if (!connected) continue;
            discovery(&sensors[i]);
            char topic[128], value[24];
            snprintf(topic, sizeof(topic), "centrale/%s/%016" PRIx64 "/availability", node, sensors[i].rom);
            publish(topic, valid ? "online" : "offline", false);
            if (valid) {
                snprintf(topic, sizeof(topic), "centrale/%s/%016" PRIx64 "/temperature", node, sensors[i].rom);
                snprintf(value, sizeof(value), "%.2f", temperature);
                publish(topic, value, false);
            }
        }
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(CONFIG_CT_PERIOD * 1000));
    }
}
