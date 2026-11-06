#pragma once
#include <stdbool.h>
#include "mqtt_client.h"

// Validate all pin assignments and initialize relay outputs OFF before other work.
void ct_plant_gpio_init(void);
// Relay following runs independently of MQTT, temperature conversions and setup.
void ct_plant_start(esp_mqtt_client_handle_t mqtt, const char *node, const char *availability);
void ct_plant_mqtt_connected(bool connected);
