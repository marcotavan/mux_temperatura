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
#include "setup.h"
#include "reconnect.h"
#include "esp_ota_ops.h"
#include "plant.h"

#define MAX_SENSORS 8
#define MQTT_UP BIT0
#define WIFI_UP BIT1
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
static ct_settings_t settings;

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
    cJSON_AddStringToObject(device, "sw_version", esp_app_get_description()->version);
    char *json = cJSON_PrintUnformatted(root);
    if (json) { publish(topic, json, true); cJSON_free(json); }
    cJSON_Delete(root);
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == MQTT_EVENT_CONNECTED) {
        ct_plant_mqtt_connected(true);
        ct_setup_mqtt_connected(true);
        xEventGroupSetBits(events, MQTT_UP);
        ESP_LOGI(TAG, "MQTT connesso");
    } else if (id == MQTT_EVENT_DISCONNECTED) {
        ct_plant_mqtt_connected(false);
        ct_setup_mqtt_connected(false);
        xEventGroupClearBits(events, MQTT_UP);
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ct_reconnect_event(base, id);
    if ((base == WIFI_EVENT && (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP)) ||
        (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)) {
        ct_setup_wifi_connected(false);
        ct_plant_mqtt_connected(false);
        ct_setup_mqtt_connected(false);
        xEventGroupClearBits(events, MQTT_UP | WIFI_UP);
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ct_setup_wifi_connected(true);
        xEventGroupSetBits(events, WIFI_UP);
        ESP_LOGI(TAG, "Wi-Fi connesso");
    }
}

static esp_err_t scan(onewire_bus_handle_t bus)
{
    onewire_device_iter_handle_t it;
    esp_err_t create_err = onewire_new_device_iter(bus, &it);
    if (create_err != ESP_OK) return create_err;
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
    return err == ESP_ERR_NOT_FOUND ? ESP_OK : err;
}

static onewire_bus_handle_t create_bus(void)
{
    onewire_bus_handle_t bus;
    // Keeps an unwired input high during bench tests; external 4.7k is still required.
    onewire_bus_config_t bc = { .bus_gpio_num = CONFIG_CT_GPIO, .flags.en_pull_up = true };
    onewire_bus_rmt_config_t rc = { .max_rx_bytes = 10 };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bc, &rc, &bus));
    return bus;
}

static void recover_bus(onewire_bus_handle_t *bus)
{
    // A timed-out RMT reception may leave the driver's RX channel busy.
    // Release sensor handles before replacing their underlying bus, retaining ROM identities.
    for (int i = 0; i < sensor_count; i++) ESP_ERROR_CHECK(ds18b20_del_device(sensors[i].handle));
    ESP_ERROR_CHECK(onewire_bus_del(*bus));
    *bus = create_bus();
    for (int i = 0; i < sensor_count; i++) {
        onewire_device_t dev = { .bus = *bus, .address = sensors[i].rom };
        ds18b20_config_t cfg = {};
        ESP_ERROR_CHECK(ds18b20_new_device_from_enumeration(&dev, &cfg, &sensors[i].handle));
    }
    ESP_LOGW(TAG, "Bus 1-Wire reinizializzato; nuovo tentativo al prossimo ciclo");
}

void app_main(void)
{
    ct_plant_gpio_init();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ct_settings_load(&settings);
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
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ct_reconnect_init(settings.ssid[0] != 0);
    wifi_config_t wifi = {};
    if (strlen(settings.ssid) > sizeof(wifi.sta.ssid) ||
        strlen(settings.wifi_password) > sizeof(wifi.sta.password)) {
        ESP_LOGE(TAG, "Credenziali Wi-Fi troppo lunghe");
        return;
    }
    memcpy(wifi.sta.ssid, settings.ssid, strlen(settings.ssid));
    memcpy(wifi.sta.password, settings.wifi_password, strlen(settings.wifi_password));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    ct_setup_init(settings.ssid[0] != 0);

    esp_mqtt_client_config_t mc = {
        .broker.address.uri = settings.mqtt_uri,
        .credentials.username = settings.mqtt_user,
        .credentials.authentication.password = settings.mqtt_password,
        .credentials.client_id = node,
        .session.last_will = { .topic = availability, .msg = "offline", .qos = 1, .retain = true },
        .session.keepalive = 30,
        .buffer.size = 2048,
        .outbox.limit = 16384,
    };
    mqtt = esp_mqtt_client_init(&mc);
    assert(mqtt);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL));
    ct_plant_start(mqtt, node, availability);
    bool mqtt_started = false;
    onewire_bus_handle_t bus = create_bus();
    bool boot_checked = false;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        if (!mqtt_started && (xEventGroupGetBits(events) & WIFI_UP)) {
            ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt));
            mqtt_started = true;
        }
        esp_err_t scan_result = scan(bus);
        esp_err_t conversion = scan_result;
        if (scan_result == ESP_OK) {
            if (sensor_count) conversion = ds18b20_trigger_temperature_conversion_for_all(bus);
            else ESP_LOGI(TAG, "Nessuna sonda collegata; in attesa (GPIO %d)", CONFIG_CT_GPIO);
        }
        bool recover = conversion == ESP_ERR_TIMEOUT || conversion == ESP_ERR_INVALID_STATE;
        // The driver waits 800 ms for all 12-bit conversions to finish.
        bool connected = (xEventGroupGetBits(events) & MQTT_UP) != 0;
        if (connected) publish(availability, "online", true);
        for (int i = 0; i < sensor_count; i++) {
            float temperature = NAN;
            err = conversion == ESP_OK ? ds18b20_get_temperature(sensors[i].handle, &temperature) : conversion;
            if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE) {
                recover = true;
                conversion = err; // Do not issue further reads on a potentially stuck channel.
            }
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
        if (recover) recover_bus(&bus);
        if (!boot_checked) {
            esp_ota_img_states_t state;
            const esp_partition_t *running = esp_ota_get_running_partition();
            if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
                ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
                ESP_LOGI(TAG, "Avvio nuovo firmware OTA confermato");
            }
            boot_checked = true;
        }
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(CONFIG_CT_PERIOD * 1000));
    }
}
