#include <stdbool.h>
#include <inttypes.h>
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "reconnect.h"
#include "control_policy.h"

ESP_EVENT_DEFINE_BASE(CT_RETRY_EVENT);
static const char *TAG = "reconnect";
static bool enabled, started, connected, attempting;
static int64_t retry_at = -1, attempt_since, connected_since;
static ct_retry_policy_t policy;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void schedule(int64_t now)
{
    unsigned delay = ct_retry_delay(&policy, now);
    retry_at = now + delay * 1000;
    ESP_LOGI(TAG, "Prossimo tentativo Wi-Fi tra %u s", delay);
}

static void tick(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    int64_t now = now_ms();
    if (!enabled || !started) return;
    if (connected) {
        // Brief connections do not defeat the backoff during a flapping outage.
        if (now - connected_since >= 60000) policy = (ct_retry_policy_t){ .cycle_start = now };
        return;
    }
    if (attempting && now - attempt_since >= 30000) {
        ESP_LOGW(TAG, "Timeout connessione/IP: nuovo tentativo con backoff");
        attempting = false;
        schedule(now);
        esp_err_t err = esp_wifi_disconnect();
        if (err != ESP_OK) ESP_LOGW(TAG, "Disconnect: %s", esp_err_to_name(err));
        return; // Process the disconnect event before beginning another attempt.
    }
    if (!attempting && retry_at >= 0 && now >= retry_at) {
        retry_at = -1;
        attempting = true;
        attempt_since = now;
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Connect: %s", esp_err_to_name(err));
            attempting = false;
            schedule(now);
        }
    }
}

static void timer_tick(void *arg)
{
    // Never block the timer service or call Wi-Fi from another execution context.
    esp_event_post(CT_RETRY_EVENT, 0, NULL, 0, 0);
}

void ct_reconnect_init(bool use_wifi)
{
    enabled = use_wifi;
    policy.cycle_start = now_ms();
    ESP_ERROR_CHECK(esp_event_handler_register(CT_RETRY_EVENT, 0, tick, NULL));
    esp_timer_handle_t timer;
    esp_timer_create_args_t args = { .callback = timer_tick, .name = "wifi_retry" };
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 250000));
}

void ct_reconnect_event(esp_event_base_t base, int32_t id)
{
    int64_t now = now_ms();
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        started = true;
        if (retry_at < 0) retry_at = now;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_STOP) {
        started = connected = attempting = false;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        connected = attempting = false;
        if (retry_at < 0) schedule(now);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (!connected) connected_since = now;
        connected = true;
        attempting = false;
        retry_at = -1;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP && connected) {
        connected = false;
        attempting = true;
        attempt_since = now; // Allow DHCP to recover, then force a fresh connection.
    }
}
