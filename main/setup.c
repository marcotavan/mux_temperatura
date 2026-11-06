#include "setup.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "led_strip.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "control_policy.h"

static const char *TAG = "setup";
static ct_settings_t current;
static atomic_bool wifi_connected;
static atomic_bool mqtt_connected;
static atomic_bool reboot_pending;
static atomic_bool ota_busy;
static led_strip_handle_t rgb;
static char csrf[33];
extern const unsigned char setup_html_start[] asm("_binary_setup_html_start");
extern const unsigned char setup_html_end[] asm("_binary_setup_html_end");

static bool valid_password(const char *s)
{
    size_t n = strlen(s);
    if (!n || (n >= 8 && n <= 63)) return true;
    if (n != 64) return false;
    for (size_t i = 0; i < n; i++) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

static bool settings_valid(const ct_settings_t *s)
{
    if (s->version != 1 || !memchr(s->ssid, 0, sizeof(s->ssid)) ||
        !memchr(s->wifi_password, 0, sizeof(s->wifi_password)) ||
        !memchr(s->mqtt_uri, 0, sizeof(s->mqtt_uri)) ||
        !memchr(s->mqtt_user, 0, sizeof(s->mqtt_user)) ||
        !memchr(s->mqtt_password, 0, sizeof(s->mqtt_password))) return false;
    const char *host = s->mqtt_uri + 7;
    return s->ssid[0] && valid_password(s->wifi_password) &&
        strncmp(s->mqtt_uri, "mqtt://", 7) == 0 && host[0] &&
        !strpbrk(s->mqtt_uri, " \r\n\t") && !strchr(host, '@') &&
        !strpbrk(host, "/?#");
}

void ct_settings_load(ct_settings_t *settings)
{
    current = (ct_settings_t){ .version = 1 };
    snprintf(current.ssid, sizeof(current.ssid), "%s", CONFIG_CT_WIFI_SSID);
    snprintf(current.wifi_password, sizeof(current.wifi_password), "%s", CONFIG_CT_WIFI_PASSWORD);
    snprintf(current.mqtt_uri, sizeof(current.mqtt_uri), "%s", CONFIG_CT_MQTT_URI);
    snprintf(current.mqtt_user, sizeof(current.mqtt_user), "%s", CONFIG_CT_MQTT_USER);
    snprintf(current.mqtt_password, sizeof(current.mqtt_password), "%s", CONFIG_CT_MQTT_PASSWORD);
    nvs_handle_t h;
    if (nvs_open("ct_setup", NVS_READONLY, &h) == ESP_OK) {
        uint8_t force_setup = 0;
        nvs_get_u8(h, "force_setup", &force_setup);
        if (force_setup) {
            current = (ct_settings_t){ .version = 1 };
            strcpy(current.mqtt_uri, "mqtt://192.168.1.10:1883");
        }
        ct_settings_t saved = {};
        size_t len = sizeof(saved);
        esp_err_t err = nvs_get_blob(h, "settings", &saved, &len);
        if (err == ESP_OK && len == sizeof(saved) && settings_valid(&saved)) {
            current = saved;
            ESP_LOGI(TAG, "Configurazione caricata dalla memoria NVS");
        }
        nvs_close(h);
    }
    *settings = current;
}

void ct_setup_wifi_connected(bool connected)
{
    atomic_store(&wifi_connected, connected);
}

void ct_setup_mqtt_connected(bool connected)
{
    atomic_store(&mqtt_connected, connected);
}

static uint32_t socket_ipv4(const struct sockaddr_storage *address)
{
    if (address->ss_family == AF_INET) {
        return ((const struct sockaddr_in *)address)->sin_addr.s_addr;
    }
#if CONFIG_LWIP_IPV6
    if (address->ss_family == AF_INET6) {
        const uint8_t *bytes = (const uint8_t *)&((const struct sockaddr_in6 *)address)->sin6_addr;
        static const uint8_t mapped[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff };
        if (!memcmp(bytes, mapped, sizeof(mapped))) {
            uint32_t ip;
            memcpy(&ip, bytes + 12, sizeof(ip));
            return ip;
        }
    }
#endif
    return 0;
}

static bool ap_request(httpd_req_t *req)
{
    // The HTTP socket also listens on STA: restrict setup to clients addressing the AP.
    struct sockaddr_storage local = {}, peer = {};
    socklen_t size = sizeof(local);
    bool local_ok = getsockname(httpd_req_to_sockfd(req), (struct sockaddr *)&local, &size) == 0;
    size = sizeof(peer);
    bool peer_ok = getpeername(httpd_req_to_sockfd(req), (struct sockaddr *)&peer, &size) == 0;
    if (!local_ok || !peer_ok || socket_ipv4(&local) != inet_addr("192.168.4.1") ||
        (ntohl(socket_ipv4(&peer)) & 0xffffff00U) != 0xc0a80400U) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Collegarsi alla rete Wi-Fi di setup");
        return false;
    }
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    return true;
}

static esp_err_t page_get(httpd_req_t *req)
{
    if (!ap_request(req)) return ESP_OK;
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)setup_html_start, setup_html_end - setup_html_start);
}

static esp_err_t config_get(httpd_req_t *req)
{
    if (!ap_request(req)) return ESP_OK;
    cJSON *json = cJSON_CreateObject();
    if (!json) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memoria insufficiente");
    cJSON_AddStringToObject(json, "ssid", current.ssid);
    cJSON_AddStringToObject(json, "mqtt_uri", current.mqtt_uri);
    cJSON_AddStringToObject(json, "mqtt_user", current.mqtt_user);
    cJSON_AddStringToObject(json, "token", csrf);
    cJSON_AddStringToObject(json, "version", esp_app_get_description()->version);
    cJSON_AddStringToObject(json, "chip", CONFIG_IDF_TARGET);
    cJSON_AddBoolToObject(json, "ota_ready", esp_ota_get_next_update_partition(NULL) != NULL);
    char *body = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memoria insufficiente");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, body);
    cJSON_free(body);
    return err;
}

static bool field(cJSON *json, const char *key, char *out, size_t capacity, bool keep_empty)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(json, key);
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= capacity) return false;
    if (!keep_empty || item->valuestring[0]) strcpy(out, item->valuestring);
    return true;
}

static esp_err_t config_post(httpd_req_t *req)
{
    if (!ap_request(req)) return ESP_OK;
    if (atomic_load(&reboot_pending) || atomic_load(&ota_busy)) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Aggiornamento o riavvio in corso");
    char content_type[48];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", content_type, sizeof(content_type)) != ESP_OK ||
        strcmp(content_type, "application/json") != 0 || req->content_len == 0 || req->content_len > 2048) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Richiesta non valida");
    }
    char body[2049];
    size_t read = 0;
    while (read < req->content_len) {
        int n = httpd_req_recv(req, body + read, req->content_len - read);
        if (n <= 0) return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Invio incompleto; riprovare");
        read += n;
    }
    body[read] = 0;
    cJSON *json = cJSON_ParseWithOpts(body, NULL, true);
    cJSON *token = cJSON_GetObjectItemCaseSensitive(json, "token");
    if (!cJSON_IsString(token) || strcmp(token->valuestring, csrf)) {
        cJSON_Delete(json);
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Sessione scaduta; ricaricare la pagina");
    }
    ct_settings_t next = current;
    bool valid = field(json, "ssid", next.ssid, sizeof(next.ssid), false) &&
        field(json, "wifi_password", next.wifi_password, sizeof(next.wifi_password), true) &&
        field(json, "mqtt_uri", next.mqtt_uri, sizeof(next.mqtt_uri), false) &&
        field(json, "mqtt_user", next.mqtt_user, sizeof(next.mqtt_user), false) &&
        field(json, "mqtt_password", next.mqtt_password, sizeof(next.mqtt_password), true);
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "wifi_open"))) next.wifi_password[0] = 0;
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(json, "mqtt_clear_password"))) next.mqtt_password[0] = 0;
    valid = valid && settings_valid(&next);
    cJSON_Delete(json);
    if (!valid) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
        "Controllare SSID (1-32 byte), password Wi-Fi (8-63 caratteri o 64 cifre hex) e broker mqtt://host:porta senza percorso");
    nvs_handle_t h;
    esp_err_t err = nvs_open("ct_setup", NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "settings", &next, sizeof(next));
        if (err == ESP_OK) {
            esp_err_t cleared = nvs_erase_key(h, "force_setup");
            if (cleared != ESP_ERR_NVS_NOT_FOUND) err = cleared;
        }
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Salvataggio fallito; riprovare");
    // No live credential changes: settings are loaded atomically after restart.
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    esp_err_t result = httpd_resp_sendstr(req, "Configurazione salvata. Riavvio: riconnettersi alla rete di casa.");
    atomic_store(&reboot_pending, true);
    return result;
}

static esp_err_t firmware_post(httpd_req_t *req)
{
    if (!ap_request(req)) return ESP_OK;
    char token[sizeof(csrf)], type[48];
    if (httpd_req_get_hdr_value_str(req, "X-Setup-Token", token, sizeof(token)) != ESP_OK ||
        strcmp(token, csrf)) return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Sessione scaduta; ricaricare la pagina");
    if (atomic_load(&reboot_pending) || atomic_exchange(&ota_busy, true))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Aggiornamento o riavvio in corso");
    const char *error = NULL;
    esp_ota_handle_t handle = 0;
    bool begun = false;
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        strcmp(type, "application/octet-stream") || req->content_len < 288) {
        error = "Selezionare il file applicazione mux_temperatura.bin";
        goto done;
    }
    if (!partition || req->content_len > partition->size) {
        error = "Partizione OTA assente o firmware troppo grande: verificare il primo flash USB";
        goto done;
    }
    esp_err_t err = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) { error = "Impossibile iniziare OTA"; goto done; }
    begun = true;
    char buffer[2048];
    size_t remaining = req->content_len;
    while (remaining) {
        int n = httpd_req_recv(req, buffer, remaining < sizeof(buffer) ? remaining : sizeof(buffer));
        if (n <= 0) { error = "Invio interrotto; il firmware attuale resta attivo. Riprovare"; goto done; }
        err = esp_ota_write(handle, buffer, n);
        if (err != ESP_OK) { error = "Firmware non valido per questa scheda o scrittura fallita"; goto done; }
        remaining -= n;
    }
    err = esp_ota_end(handle);
    begun = false; // esp_ota_end frees the handle even on failure.
    if (err != ESP_OK) { error = "Verifica firmware fallita; il firmware attuale resta attivo"; goto done; }
    esp_app_desc_t description;
    if (esp_ota_get_partition_description(partition, &description) != ESP_OK ||
        strncmp(description.project_name, esp_app_get_description()->project_name, sizeof(description.project_name))) {
        error = "Il file appartiene a un altro progetto";
        goto done;
    }
    if (esp_ota_set_boot_partition(partition) != ESP_OK) {
        error = "Impossibile selezionare il nuovo firmware";
        goto done;
    }
    ESP_LOGI(TAG, "OTA verificato: %u byte, versione %s", (unsigned)req->content_len, description.version);
    atomic_store(&reboot_pending, true);
done:
    if (begun) esp_ota_abort(handle);
    atomic_store(&ota_busy, false);
    if (error) {
        ESP_LOGW(TAG, "OTA: %s", error);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, error);
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, "Firmware verificato e salvato. Riavvio in corso.");
}

static void start_ap(void)
{
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    // A compiled password keeps printed Wi-Fi QR labels valid across all resets.
    static const char password[] = CONFIG_CT_SETUP_AP_PASSWORD;
    _Static_assert(sizeof(password) >= 9 && sizeof(password) <= 64,
                   "Password AP: usare da 8 a 63 caratteri");
    snprintf(csrf, sizeof(csrf), "%08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32,
             esp_random(), esp_random(), esp_random(), esp_random());
    esp_netif_t *ap = esp_netif_create_default_wifi_ap();
    assert(ap);
    esp_netif_ip_info_t ip = {};
    ip.ip.addr = ip.gw.addr = inet_addr("192.168.4.1");
    ip.netmask.addr = inet_addr("255.255.255.0");
    esp_err_t dhcp = esp_netif_dhcps_stop(ap);
    if (dhcp != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) ESP_ERROR_CHECK(dhcp);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap, &ip));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap));
    wifi_config_t cfg = {};
    snprintf((char *)cfg.ap.ssid, sizeof(cfg.ap.ssid), "Centrale-Setup-%02X%02X%02X", mac[3], mac[4], mac[5]);
    strcpy((char *)cfg.ap.password, password);
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.max_connection = 4;
    cfg.ap.channel = 1;
    // Configure WPA2 before starting the AP; never expose the driver's default open AP.
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    httpd_config_t server_cfg = HTTPD_DEFAULT_CONFIG();
    server_cfg.stack_size = 8192;
    server_cfg.recv_wait_timeout = 5;
    server_cfg.max_uri_handlers = 4;
    server_cfg.lru_purge_enable = true;
    httpd_handle_t server;
    ESP_ERROR_CHECK(httpd_start(&server, &server_cfg));
    const httpd_uri_t routes[] = {
        { .uri = "/", .method = HTTP_GET, .handler = page_get },
        { .uri = "/api/config", .method = HTTP_GET, .handler = config_get },
        { .uri = "/api/config", .method = HTTP_POST, .handler = config_post },
        { .uri = "/api/firmware", .method = HTTP_POST, .handler = firmware_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    ESP_LOGI(TAG, "AP: %s | password setup: %s | pagina http://192.168.4.1", cfg.ap.ssid, password);
}

static void rgb_init(void)
{
    const int pin = CONFIG_CT_RGB_GPIO;
    if (pin < 0) return;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin) || pin == CONFIG_CT_GPIO || pin == CONFIG_CT_SETUP_BUTTON_GPIO) {
        ESP_LOGW(TAG, "LED RGB disabilitato: GPIO non valido o condiviso con sonde/pulsante");
        return;
    }
    led_strip_config_t config = { .strip_gpio_num = pin, .max_leds = 1,
        .led_model = LED_MODEL_WS2812, .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB };
    led_strip_rmt_config_t rmt = { .resolution_hz = 10000000 };
    esp_err_t err = led_strip_new_rmt_device(&config, &rmt, &rgb);
    if (err != ESP_OK) { rgb = NULL; ESP_LOGW(TAG, "LED RGB: %s", esp_err_to_name(err)); }
}

static void rgb_update(int64_t now, bool ap, int64_t held_ms)
{
    if (!rgb) return;
    unsigned r = 0, g = 0, b = 0, level = CONFIG_CT_RGB_BRIGHTNESS;
    if (atomic_load(&ota_busy)) { r = b = level; }
    else if (held_ms >= 15000) { if ((now / 100000) % 2 == 0) r = level; }
    else if (held_ms >= 5000) { if ((now / 250000) % 2 == 0) b = level; }
    else if (ap && (now / 500000) % 2 == 0) b = level;
    else if (!atomic_load(&wifi_connected)) { if ((now / 500000) % 2 != 0) r = level; }
    else if (!atomic_load(&mqtt_connected)) { r = level; g = level / 2; }
    else g = level;
    static unsigned previous = UINT32_MAX;
    unsigned color = (r << 16) | (g << 8) | b;
    if (color == previous) return;
    esp_err_t err = led_strip_set_pixel(rgb, 0, r, g, b);
    if (err == ESP_OK) err = led_strip_refresh(rgb);
    if (err == ESP_OK) previous = color;
    else { ESP_LOGW(TAG, "LED RGB: %s; indicazione disabilitata", esp_err_to_name(err)); rgb = NULL; }
}

static void factory_reset(void)
{
    if (atomic_exchange(&ota_busy, true)) return;
    // Erase this application's settings, not the firmware or calibration data.
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("ct_setup", NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_erase_all(h));
    // Suppress compiled credentials too: reset must always return to setup.
    ESP_ERROR_CHECK(nvs_set_u8(h, "force_setup", 1));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
    ESP_LOGW(TAG, "Reset completo configurazione; riavvio in setup con password AP predefinita");
    atomic_store(&reboot_pending, true);
    atomic_store(&ota_busy, false);
}

static void setup_task(void *arg)
{
    bool has_ssid = (bool)(uintptr_t)arg;
    bool ap_active = false;
    int64_t offline_since = esp_timer_get_time();
    ct_button_t press = {0};
    rgb_init();
    const int pin = CONFIG_CT_SETUP_BUTTON_GPIO;
    bool button = pin >= 0 && GPIO_IS_VALID_GPIO(pin) && pin != CONFIG_CT_GPIO && pin != CONFIG_CT_RGB_GPIO;
    if (button) {
        gpio_config_t cfg = { .pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_DISABLE };
        ESP_ERROR_CHECK(gpio_config(&cfg));
    } else ESP_LOGW(TAG, "Pulsante setup disabilitato o GPIO non utilizzabile");
    while (true) {
        int64_t now = esp_timer_get_time();
        if (atomic_load(&reboot_pending)) { vTaskDelay(pdMS_TO_TICKS(1200)); esp_restart(); }
        if (atomic_load(&wifi_connected)) offline_since = now;
        ct_button_action_t action = CT_BUTTON_NONE;
        if (atomic_load(&ota_busy)) {
            press = (ct_button_t){0};
        } else if (button) action = ct_button_update(&press, gpio_get_level(pin) == 0, now / 1000);
        if (action == CT_BUTTON_RESET) { factory_reset(); continue; }
        if (!ap_active && (!has_ssid || now - offline_since >= 45000000 || action == CT_BUTTON_AP)) {
            start_ap();
            ap_active = true;
        }
        rgb_update(now, ap_active, press.down && press.armed ? now / 1000 - press.pressed_since : 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void ct_setup_init(bool has_ssid)
{
    BaseType_t created = xTaskCreate(setup_task, "setup", 6144, (void *)(uintptr_t)has_ssid, 3, NULL);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
