#include "idle_sleep.h"

void idle_init(idle_state_t *state, uint32_t screen_off_ms,
               uint32_t light_sleep_ms, uint32_t deep_sleep_ms,
               int64_t now_ms) {
    if (!state) return;
    if (light_sleep_ms <= screen_off_ms) {
        light_sleep_ms = screen_off_ms + 60000UL;
    }
    if (deep_sleep_ms <= light_sleep_ms) {
        deep_sleep_ms = light_sleep_ms + 60000UL;
    }
    state->screen_off_ms = screen_off_ms;
    state->light_sleep_ms = light_sleep_ms;
    state->deep_sleep_ms = deep_sleep_ms;
    state->last_activity_ms = now_ms;
    state->screen_off = false;
    state->light_fired = false;
    state->deep_fired = false;
}

void idle_notify_activity(idle_state_t *state, int64_t now_ms) {
    if (!state) return;
    state->last_activity_ms = now_ms;
    state->screen_off = false;
    state->light_fired = false;
    state->deep_fired = false;
}

idle_action_t idle_poll(idle_state_t *state, int64_t now_ms) {
    if (!state) return IDLE_ACTION_NONE;
    int64_t elapsed = now_ms - state->last_activity_ms;
    if (elapsed < 0) return IDLE_ACTION_NONE;
    if ((uint64_t)elapsed >= state->deep_sleep_ms) {
        if (!state->deep_fired) {
            state->deep_fired = true;
            state->light_fired = true;
            state->screen_off = true;
            return IDLE_ACTION_DEEP_SLEEP;
        }
        return IDLE_ACTION_NONE;
    }
    if ((uint64_t)elapsed >= state->light_sleep_ms) {
        if (!state->light_fired) {
            state->light_fired = true;
            state->screen_off = true;
            return IDLE_ACTION_LIGHT_SLEEP;
        }
        return IDLE_ACTION_NONE;
    }
    if ((uint64_t)elapsed >= state->screen_off_ms) {
        if (!state->screen_off) {
            state->screen_off = true;
            return IDLE_ACTION_SCREEN_OFF;
        }
        return IDLE_ACTION_NONE;
    }
    return IDLE_ACTION_NONE;
}
