#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CT_SETUP_MAX_SENSORS 8

typedef struct {
    uint64_t rom;
    float temperature;
    bool valid;
} ct_probe_status_t;

typedef struct {
    char node[32];
    int gpio;
    int last_error;
    unsigned sensor_count;
    bool bus_ok;
    bool wifi_connected;
    bool mqtt_connected;
    ct_probe_status_t sensors[CT_SETUP_MAX_SENSORS];
} ct_probe_snapshot_t;

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
void ct_setup_mqtt_connected(bool connected);
void ct_setup_probe_status_update(const ct_probe_snapshot_t *snapshot);
