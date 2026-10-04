#include "idle_sleep.h"

void idle_init(idle_state_t *state, uint32_t screen_off_ms,
               uint32_t shutdown_ms, int64_t now_ms) {
    if (!state) return;
    if (shutdown_ms <= screen_off_ms) {
        shutdown_ms = screen_off_ms + 60000UL;
    }
    state->screen_off_ms = screen_off_ms;
    state->shutdown_ms = shutdown_ms;
    state->last_activity_ms = now_ms;
    state->screen_off = false;
    state->shutdown_done = false;
}

void idle_notify_activity(idle_state_t *state, int64_t now_ms) {
    if (!state || state->shutdown_done) return;
    state->last_activity_ms = now_ms;
    state->screen_off = false;
}

idle_action_t idle_poll(idle_state_t *state, int64_t now_ms) {
    if (!state || state->shutdown_done) return IDLE_ACTION_NONE;
    int64_t elapsed = now_ms - state->last_activity_ms;
    if (elapsed < 0) return IDLE_ACTION_NONE;
    if ((uint64_t)elapsed >= state->shutdown_ms) {
        state->shutdown_done = true;
        state->screen_off = true;
        return IDLE_ACTION_SHUTDOWN;
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
