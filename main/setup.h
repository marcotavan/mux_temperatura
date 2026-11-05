#pragma once
#include <stdbool.h>

typedef struct {
    unsigned version;
    char ssid[33];
    char wifi_password[65];
    char mqtt_uri[256];
    char mqtt_user[129];
    char mqtt_password[129];
} ct_settings_t;

// Call after NVS init, before configuring station Wi-Fi.
void ct_settings_load(ct_settings_t *settings);
// Call after Wi-Fi start; a task watches BOOT and connection timeout.
void ct_setup_init(bool has_ssid);
void ct_setup_wifi_connected(bool connected);
