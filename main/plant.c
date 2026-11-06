#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "nvs.h"
#include "cJSON.h"
#include "control_policy.h"
#include "plant.h"

static const char *TAG = "impianto";
static atomic_bool mqtt_up;
static esp_mqtt_client_handle_t client;
static char identity[32], availability_topic[96], state_topic[96];
static uint32_t boot_count;
static const char *reset_reason;
static QueueHandle_t snapshots;
static bool flow_at_boot;
typedef struct { bool raw_closed, relay_on; } snapshot_t;
#if CONFIG_CT_BOARD_4RELAY
static const int relays[] = {32, 33, 25, 26};
#endif

void ct_plant_gpio_init(void)
{
#if CONFIG_CT_BOARD_4RELAY
    uint64_t mask = 0;
    for (size_t i = 0; i < 4; i++) {
        // Set the output latch before enabling the output direction.
        ESP_ERROR_CHECK(gpio_set_level(relays[i], !CONFIG_CT_RELAY_ACTIVE_LEVEL));
        mask |= 1ULL << relays[i];
    }
    gpio_config_t cfg = { .pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    ESP_LOGI(TAG, "Rele inizializzati OFF; nessuna forzatura al boot");
#endif
    const int roles[] = {CONFIG_CT_GPIO, CONFIG_CT_SETUP_BUTTON_GPIO, CONFIG_CT_RGB_GPIO,
                         CONFIG_CT_STATUS_LED_GPIO, CONFIG_CT_FLOW_GPIO};
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); i++) {
        if (roles[i] < 0) continue;
        bool valid = GPIO_IS_VALID_OUTPUT_GPIO(roles[i]);
#if CONFIG_IDF_TARGET_ESP32
        valid = valid && !(roles[i] >= 6 && roles[i] <= 11); // Module flash bus.
#endif
#if CONFIG_CT_BOARD_4RELAY
        for (size_t r = 0; r < 4; r++) valid = valid && roles[i] != relays[r];
#endif
        for (size_t j = 0; j < i; j++) valid = valid && roles[j] != roles[i];
        if (!valid) ESP_LOGE(TAG, "GPIO %d non valido, duplicato o riservato", roles[i]);
        ESP_ERROR_CHECK(valid ? ESP_OK : ESP_ERR_INVALID_ARG);
    }
#if CONFIG_CT_FLOW_GPIO >= 0
    {
        gpio_config_t cfg = { .pin_bit_mask = 1ULL << CONFIG_CT_FLOW_GPIO,
            .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        flow_at_boot = gpio_get_level(CONFIG_CT_FLOW_GPIO) == 0;
    }
#endif
}

static const char *reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
    }
}

static void flow_task(void *arg)
{
    ct_flow_follow_t follow = {0};
    snapshot_t previous = {0};
    bool first = true;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        bool raw = CONFIG_CT_FLOW_GPIO >= 0 && gpio_get_level(CONFIG_CT_FLOW_GPIO) == 0;
        bool confirmed = ct_flow_follow_update(&follow, raw, esp_timer_get_time() / 1000, CONFIG_CT_FLOW_CLOSE_MS);
        bool relay = false;
#if CONFIG_CT_BOARD_4RELAY
        relay = confirmed;
        if (first || relay != previous.relay_on) {
            ESP_ERROR_CHECK(gpio_set_level(relays[CONFIG_CT_FLOW_RELAY - 1],
                relay ? CONFIG_CT_RELAY_ACTIVE_LEVEL : !CONFIG_CT_RELAY_ACTIVE_LEVEL));
        }
#else
        (void)confirmed;
#endif
        snapshot_t current = { .raw_closed = raw, .relay_on = relay };
        if (first || raw != previous.raw_closed || relay != previous.relay_on) {
            // Only a single current snapshot is kept; network congestion cannot block the relay.
            xQueueOverwrite(snapshots, &current);
            previous = current;
            first = false;
        }
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}

static bool publish_json(const char *topic, cJSON *json, bool retain)
{
    char *payload = cJSON_PrintUnformatted(json);
    bool ok = false;
    if (payload) {
        ok = esp_mqtt_client_enqueue(client, topic, payload, 0, 1, retain, true) >= 0;
        cJSON_free(payload);
    }
    cJSON_Delete(json);
    if (!ok) ESP_LOGW(TAG, "Pubblicazione non accodata: %s", topic);
    return ok;
}

static void discover(const char *domain, const char *key, const char *name,
                     const char *device_class, const char *unit, bool diagnostic)
{
    char id[80], topic[144], value_template[96];
    snprintf(id, sizeof(id), "%s_%s", identity, key);
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s/config", domain, id);
    cJSON *json = cJSON_CreateObject();
    if (!json) return;
    cJSON_AddStringToObject(json, "name", name);
    cJSON_AddStringToObject(json, "unique_id", id);
    cJSON_AddStringToObject(json, "state_topic", state_topic);
    bool flow_entity = !strcmp(key, "flow_closed") || !strcmp(key, "flow_at_boot") || !strcmp(key, "relay_on");
    if (flow_entity) {
        cJSON_AddStringToObject(json, "availability_mode", "all");
        cJSON *available = cJSON_AddArrayToObject(json, "availability");
        cJSON *node_available = cJSON_CreateObject();
        cJSON_AddStringToObject(node_available, "topic", availability_topic);
        cJSON_AddItemToArray(available, node_available);
        cJSON *input_available = cJSON_CreateObject();
        cJSON_AddStringToObject(input_available, "topic", state_topic);
        cJSON_AddStringToObject(input_available, "value_template", "{{ 'online' if value_json.flow_enabled else 'offline' }}");
        cJSON_AddItemToArray(available, input_available);
    } else cJSON_AddStringToObject(json, "availability_topic", availability_topic);
    cJSON_AddNumberToObject(json, "qos", 1);
    if (diagnostic) cJSON_AddStringToObject(json, "entity_category", "diagnostic");
    if (device_class) cJSON_AddStringToObject(json, "device_class", device_class);
    if (unit) cJSON_AddStringToObject(json, "unit_of_measurement", unit);
    if (!strcmp(domain, "binary_sensor")) {
        snprintf(value_template, sizeof(value_template), "{{ 'ON' if value_json.%s else 'OFF' }}", key);
        cJSON_AddStringToObject(json, "payload_on", "ON");
        cJSON_AddStringToObject(json, "payload_off", "OFF");
    } else snprintf(value_template, sizeof(value_template), "{{ value_json.%s }}", key);
    cJSON_AddStringToObject(json, "value_template", value_template);
    cJSON *device = cJSON_AddObjectToObject(json, "device");
    cJSON *ids = cJSON_AddArrayToObject(device, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString(identity));
    cJSON_AddStringToObject(device, "name", identity);
    cJSON_AddStringToObject(device, "manufacturer", "DIY");
    cJSON_AddStringToObject(device, "model", CONFIG_IDF_TARGET);
    cJSON_AddStringToObject(device, "sw_version", esp_app_get_description()->version);
    publish_json(topic, json, true);
}

static void diagnostics_task(void *arg)
{
    snapshot_t current = {0};
    bool was_connected = false, dirty = true;
    int64_t published_at = -15000000;
    while (true) {
        snapshot_t update;
        if (xQueueReceive(snapshots, &update, pdMS_TO_TICKS(100)) == pdTRUE) {
            current = update;
            dirty = true;
            if (CONFIG_CT_FLOW_GPIO >= 0) ESP_LOGI(TAG, "Flussostato %s, comando rele %s",
                current.raw_closed ? "chiuso" : "aperto", current.relay_on ? "ON" : "OFF");
        }
        bool connected = atomic_load(&mqtt_up);
        int64_t now = esp_timer_get_time();
        if (connected && (!was_connected || now - published_at >= 15000000)) {
            discover("binary_sensor", "flow_closed", "Flussostato chiuso", NULL, NULL, false);
            discover("binary_sensor", "flow_at_boot", "Flussostato chiuso al boot", NULL, NULL, true);
#if CONFIG_CT_BOARD_4RELAY
            discover("binary_sensor", "relay_on", "Comando rele flussostato", NULL, NULL, true);
#endif
            discover("sensor", "boot_count", "Numero avvii", NULL, NULL, true);
            discover("sensor", "reset_reason", "Causa riavvio", NULL, NULL, true);
            discover("sensor", "uptime", "Tempo dall'avvio", "duration", "s", true);
            dirty = true;
        }
        if (connected && dirty) {
            cJSON *json = cJSON_CreateObject();
            if (json) {
                cJSON_AddBoolToObject(json, "flow_enabled", CONFIG_CT_FLOW_GPIO >= 0);
                cJSON_AddBoolToObject(json, "flow_closed", current.raw_closed);
                cJSON_AddBoolToObject(json, "flow_at_boot", flow_at_boot);
                cJSON_AddBoolToObject(json, "relay_on", current.relay_on);
                cJSON_AddNumberToObject(json, "boot_count", boot_count);
                cJSON_AddStringToObject(json, "reset_reason", reset_reason);
                cJSON_AddNumberToObject(json, "uptime", now / 1000000);
                if (publish_json(state_topic, json, true)) { published_at = now; dirty = false; }
            }
        }
        was_connected = connected;
    }
}

void ct_plant_start(esp_mqtt_client_handle_t mqtt, const char *node, const char *availability)
{
    client = mqtt;
    snprintf(identity, sizeof(identity), "%s", node);
    snprintf(availability_topic, sizeof(availability_topic), "%s", availability);
    snprintf(state_topic, sizeof(state_topic), "centrale/%s/plant/state", node);
    reset_reason = reason_name(esp_reset_reason());
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("ct_plant", NVS_READWRITE, &h));
    esp_err_t err = nvs_get_u32(h, "boots", &boot_count);
    if (err != ESP_ERR_NVS_NOT_FOUND) ESP_ERROR_CHECK(err);
    if (boot_count != UINT32_MAX) boot_count++;
    ESP_ERROR_CHECK(nvs_set_u32(h, "boots", boot_count));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    snapshots = xQueueCreate(1, sizeof(snapshot_t));
    assert(snapshots);
    // Control starts before any MQTT task or temperature conversion.
    ESP_ERROR_CHECK(xTaskCreate(flow_task, "flow_follow", 3072, NULL, 5, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(diagnostics_task, "plant_mqtt", 6144, NULL, 2, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI(TAG, "Avvio %" PRIu32 " (%s), ingresso flussostato GPIO %d", boot_count, reset_reason, CONFIG_CT_FLOW_GPIO);
}

void ct_plant_mqtt_connected(bool connected)
{
    atomic_store(&mqtt_up, connected);
}
