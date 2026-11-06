#pragma once
#include <stdbool.h>
#include "esp_event.h"

void ct_reconnect_init(bool enabled);
// Called on the default event loop; the timer also posts to that loop.
void ct_reconnect_event(esp_event_base_t base, int32_t id);
