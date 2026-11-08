#pragma once
#include <stdbool.h>
#include "mqtt_client.h"

// Validate all pin assignments and initialize relay outputs OFF before other work.
void ct_plant_gpio_init(void);
// Initialize MQTT identity before connecting; relay commands arrive via MQTT events.
void ct_plant_start(esp_mqtt_client_handle_t mqtt, const char *node, const char *availability);
void ct_plant_mqtt_connected(bool connected);
void ct_plant_mqtt_data(const char *topic, int topic_len, const char *data, int data_len, bool retained);
