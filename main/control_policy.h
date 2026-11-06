#pragma once
#include <stdint.h>
#include <stdbool.h>

// All times are milliseconds. A ten-minute outage starts another fast cycle.
typedef struct {
    int64_t cycle_start;
    unsigned delay_s;
} ct_retry_policy_t;

static inline unsigned ct_retry_delay(ct_retry_policy_t *p, int64_t now)
{
    if (now - p->cycle_start >= 600000) {
        p->cycle_start = now;
        p->delay_s = 0;
    }
    unsigned delay = p->delay_s;
    p->delay_s = delay == 0 ? 1 : (delay >= 32 ? 60 : delay * 2);
    return delay;
}

typedef enum { CT_BUTTON_NONE, CT_BUTTON_AP, CT_BUTTON_RESET } ct_button_action_t;

static inline ct_button_action_t ct_button_action(int64_t held_ms)
{
    if (held_ms >= 15000) return CT_BUTTON_RESET;
    if (held_ms >= 5000) return CT_BUTTON_AP;
    return CT_BUTTON_NONE;
}

typedef struct {
    bool raw, down, armed;
    int64_t raw_since, pressed_since;
} ct_button_t;

static inline ct_button_action_t ct_button_update(ct_button_t *p, bool pressed, int64_t now)
{
    if (pressed != p->raw) { p->raw = pressed; p->raw_since = now; }
    if (now - p->raw_since < 50) return CT_BUTTON_NONE;
    ct_button_action_t action = CT_BUTTON_NONE;
    if (pressed && !p->down) {
        p->down = true;
        if (p->armed) p->pressed_since = p->raw_since;
    } else if (!pressed) {
        if (p->down && p->armed) action = ct_button_action(p->raw_since - p->pressed_since);
        p->down = false;
        p->armed = true; // A button held at boot must first be released.
    }
    return action;
}
