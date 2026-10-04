#pragma once

#include <stdbool.h>
#include <stdint.h>

// Idle timeouts for the baseline demo: 60 s without a key press turns the
// backlight off, 10 min without a key press enters software shutdown
// (deep sleep without timer wakeup; power key / reset to restart).
#define IDLE_SCREEN_OFF_MS (60UL * 1000UL)
#define IDLE_SHUTDOWN_MS (10UL * 60UL * 1000UL)

typedef enum {
    IDLE_ACTION_NONE = 0,
    IDLE_ACTION_SCREEN_OFF,
    IDLE_ACTION_SHUTDOWN,
} idle_action_t;

typedef struct {
    uint32_t screen_off_ms;
    uint32_t shutdown_ms;
    int64_t last_activity_ms;
    bool screen_off;
    bool shutdown_done;
} idle_state_t;

void idle_init(idle_state_t *state, uint32_t screen_off_ms,
               uint32_t shutdown_ms, int64_t now_ms);
void idle_notify_activity(idle_state_t *state, int64_t now_ms);
idle_action_t idle_poll(idle_state_t *state, int64_t now_ms);
