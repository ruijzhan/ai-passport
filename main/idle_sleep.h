#pragma once

#include <stdbool.h>
#include <stdint.h>

// Idle policy for the baseline demo: graded automatic power saving.
//   60 s without a key press turns the backlight off,
//   5 min without a key press enters light sleep (2 s, timer wake),
//   10 min without a key press enters deep sleep (5 s, timer wake + restart).
// Light/deep use timer wake only: keys pressed during the short sleep window
// may be delayed/missed, and deep wake reboots. No GPIO key wake is armed.
#define IDLE_SCREEN_OFF_MS (60UL * 1000UL)
#define IDLE_LIGHT_SLEEP_MS (5UL * 60UL * 1000UL)
#define IDLE_DEEP_SLEEP_MS (10UL * 60UL * 1000UL)

typedef enum {
    IDLE_ACTION_NONE = 0,
    IDLE_ACTION_SCREEN_OFF,
    IDLE_ACTION_LIGHT_SLEEP,
    IDLE_ACTION_DEEP_SLEEP,
} idle_action_t;

typedef struct {
    uint32_t screen_off_ms;
    uint32_t light_sleep_ms;
    uint32_t deep_sleep_ms;
    int64_t last_activity_ms;
    bool screen_off;
    bool light_fired;
    bool deep_fired;
} idle_state_t;

void idle_init(idle_state_t *state, uint32_t screen_off_ms,
               uint32_t light_sleep_ms, uint32_t deep_sleep_ms,
               int64_t now_ms);
void idle_notify_activity(idle_state_t *state, int64_t now_ms);
idle_action_t idle_poll(idle_state_t *state, int64_t now_ms);
