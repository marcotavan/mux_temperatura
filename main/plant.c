#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "cJSON.h"
#include "plant.h"

static const char *TAG = "rele_mqtt";
#if CONFIG_CT_BOARD_4RELAY
static const int relay_gpios[] = {32, 33, 25, 26};
#define RELAY_COUNT (sizeof(relay_gpios) / sizeof(relay_gpios[0]))
static bool relay_states[RELAY_COUNT];
static esp_mqtt_client_handle_t client;
static char identity[32], availability_topic[96];

static void publish(const char *topic, const char *payload, bool retain)
{
    int id = esp_mqtt_client_enqueue(client, topic, payload, 0, 1, retain, true);
    if (id < 0) ESP_LOGW(TAG, "Pubblicazione MQTT non accodata: %s", topic);
}

static void relay_discovery(unsigned index)
{
    char object_id[64], discovery_topic[128], state_topic[128], command_topic[128];
    snprintf(object_id, sizeof(object_id), "%s_relay_%u", identity, index + 1);
    snprintf(discovery_topic, sizeof(discovery_topic), "homeassistant/switch/%s/config", object_id);
    snprintf(state_topic, sizeof(state_topic), "centrale/%s/relay/%u/state", identity, index + 1);
    snprintf(command_topic, sizeof(command_topic), "centrale/%s/relay/%u/set", identity, index + 1);

    cJSON *json = cJSON_CreateObject();
    if (!json) return;
    char name[16];
    snprintf(name, sizeof(name), "Rele %u", index + 1);
    cJSON_AddStringToObject(json, "name", name);
    cJSON_AddStringToObject(json, "unique_id", object_id);
    cJSON_AddStringToObject(json, "state_topic", state_topic);
    cJSON_AddStringToObject(json, "command_topic", command_topic);
    cJSON_AddStringToObject(json, "payload_on", "ON");
    cJSON_AddStringToObject(json, "payload_off", "OFF");
    cJSON_AddStringToObject(json, "availability_topic", availability_topic);
    cJSON_AddStringToObject(json, "payload_available", "online");
    cJSON_AddStringToObject(json, "payload_not_available", "offline");
    cJSON *device = cJSON_AddObjectToObject(json, "device");
    cJSON *ids = cJSON_AddArrayToObject(device, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString(identity));
    cJSON_AddStringToObject(device, "name", identity);
    cJSON_AddStringToObject(device, "manufacturer", "DIY");
    cJSON_AddStringToObject(device, "model", CONFIG_IDF_TARGET);
    char *payload = cJSON_PrintUnformatted(json);
    if (payload) {
        publish(discovery_topic, payload, true);
        cJSON_free(payload);
    }
    cJSON_Delete(json);
}

static void remove_old_diagnostics(void)
{
    static const char *const old_entities[] = {
        "binary_sensor/flow_closed", "binary_sensor/flow_at_boot",
        "binary_sensor/relay_on", "sensor/boot_count", "sensor/reset_reason",
        "sensor/uptime",
    };
    char topic[144];
    for (size_t i = 0; i < sizeof(old_entities) / sizeof(old_entities[0]); i++) {
        snprintf(topic, sizeof(topic), "homeassistant/%s/%s_%s/config",
                 old_entities[i][0] == 'b' ? "binary_sensor" : "sensor", identity,
                 strchr(old_entities[i], '/') + 1);
        publish(topic, "", true);
    }
}

static void publish_relay_state(unsigned index)
{
    char topic[128];
    snprintf(topic, sizeof(topic), "centrale/%s/relay/%u/state", identity, index + 1);
    publish(topic, relay_states[index] ? "ON" : "OFF", true);
}
#endif

void ct_plant_gpio_init(void)
{
#if CONFIG_CT_BOARD_4RELAY
    uint64_t mask = 0;
    for (size_t i = 0; i < RELAY_COUNT; i++) {
        // Drive each relay inactive before enabling its output pin.
        ESP_ERROR_CHECK(gpio_set_level(relay_gpios[i], !CONFIG_CT_RELAY_ACTIVE_LEVEL));
        mask |= 1ULL << relay_gpios[i];
    }
    gpio_config_t cfg = { .pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_LOGI(TAG, "Quattro rele inizializzati OFF");
#endif
    const int roles[] = {CONFIG_CT_GPIO, CONFIG_CT_SETUP_BUTTON_GPIO, CONFIG_CT_RGB_GPIO,
                         CONFIG_CT_STATUS_LED_GPIO};
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); i++) {
        if (roles[i] < 0) continue;
        bool valid = GPIO_IS_VALID_OUTPUT_GPIO(roles[i]);
#if CONFIG_IDF_TARGET_ESP32
        valid = valid && !(roles[i] >= 6 && roles[i] <= 11);
#endif
#if CONFIG_CT_BOARD_4RELAY
        for (size_t r = 0; r < RELAY_COUNT; r++) valid = valid && roles[i] != relay_gpios[r];
#endif
        for (size_t j = 0; j < i; j++) valid = valid && roles[i] != roles[j];
        if (!valid) ESP_LOGE(TAG, "GPIO %d non valido, duplicato o riservato", roles[i]);
        ESP_ERROR_CHECK(valid ? ESP_OK : ESP_ERR_INVALID_ARG);
    }
}

void ct_plant_start(esp_mqtt_client_handle_t mqtt, const char *node, const char *availability)
{
#if CONFIG_CT_BOARD_4RELAY
    client = mqtt;
    snprintf(identity, sizeof(identity), "%s", node);
    snprintf(availability_topic, sizeof(availability_topic), "%s", availability);
#else
    (void)mqtt;
    (void)node;
    (void)availability;
#endif
}

void ct_plant_mqtt_connected(bool connected)
{
#if CONFIG_CT_BOARD_4RELAY
    if (!connected) return;
    char command_filter[128];
    snprintf(command_filter, sizeof(command_filter), "centrale/%s/relay/+/set", identity);
    int msg_id = esp_mqtt_client_subscribe(client, command_filter, 1);
    if (msg_id < 0) ESP_LOGW(TAG, "Sottoscrizione comandi rele fallita");
    remove_old_diagnostics();
    for (unsigned i = 0; i < RELAY_COUNT; i++) {
        relay_discovery(i);
        publish_relay_state(i);
    }
#else
    (void)connected;
#endif
}

void ct_plant_mqtt_data(const char *topic, int topic_len, const char *data, int data_len, bool retained)
{
#if CONFIG_CT_BOARD_4RELAY
    if (!topic || !data || retained || data_len <= 0) return;
    char expected[128];
    for (unsigned i = 0; i < RELAY_COUNT; i++) {
        snprintf(expected, sizeof(expected), "centrale/%s/relay/%u/set", identity, i + 1);
        if (topic_len != (int)strlen(expected) || memcmp(topic, expected, topic_len) != 0) continue;
        bool on;
        if (data_len == 2 && memcmp(data, "ON", 2) == 0) on = true;
        else if (data_len == 3 && memcmp(data, "OFF", 3) == 0) on = false;
        else {
            ESP_LOGW(TAG, "Comando rele %u ignorato: usare ON o OFF", i + 1);
            return;
        }
        relay_states[i] = on;
        ESP_ERROR_CHECK(gpio_set_level(relay_gpios[i], on ? CONFIG_CT_RELAY_ACTIVE_LEVEL : !CONFIG_CT_RELAY_ACTIVE_LEVEL));
        publish_relay_state(i);
        ESP_LOGI(TAG, "Rele %u %s", i + 1, on ? "ON" : "OFF");
        return;
    }
#else
    (void)topic;
    (void)topic_len;
    (void)data;
    (void)data_len;
    (void)retained;
#endif
}
