idf_component_register(SRCS "main.c" INCLUDE_DIRS "." REQUIRES esp_wifi esp_event esp_netif nvs_flash esp_system cjson mqtt ds18b20 onewire_bus)
