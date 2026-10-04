#include <assert.h>
#include <stdio.h>

#include "idle_sleep.h"

int main(void) {
    idle_state_t state;

    // Graded timeouts: screen off 60 s, light sleep 5 min, deep sleep 10 min.
    idle_init(&state, IDLE_SCREEN_OFF_MS, IDLE_LIGHT_SLEEP_MS,
              IDLE_DEEP_SLEEP_MS, 0);
    assert(IDLE_SCREEN_OFF_MS == 60000UL);
    assert(IDLE_LIGHT_SLEEP_MS == 300000UL);
    assert(IDLE_DEEP_SLEEP_MS == 600000UL);
    assert(idle_poll(&state, 59999) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 60000) == IDLE_ACTION_SCREEN_OFF);
    // Each stage fires once until activity resumes.
    assert(idle_poll(&state, 60001) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 299999) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 300000) == IDLE_ACTION_LIGHT_SLEEP);
    assert(idle_poll(&state, 300001) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 599999) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 600000) == IDLE_ACTION_DEEP_SLEEP);
    // Terminal: further polls stay silent until activity.
    assert(idle_poll(&state, 700000) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 3600000) == IDLE_ACTION_NONE);

    // Activity clears all stages and restarts every timer.
    idle_init(&state, 60000UL, 300000UL, 600000UL, 0);
    assert(idle_poll(&state, 60000) == IDLE_ACTION_SCREEN_OFF);
    idle_notify_activity(&state, 70000);
    assert(idle_poll(&state, 129999) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 130000) == IDLE_ACTION_SCREEN_OFF);
    assert(idle_poll(&state, 370000) == IDLE_ACTION_LIGHT_SLEEP);
    assert(idle_poll(&state, 670000) == IDLE_ACTION_DEEP_SLEEP);
    assert(idle_poll(&state, 670001) == IDLE_ACTION_NONE);

    // Activity between stages restarts from the first stage.
    idle_init(&state, 60000UL, 300000UL, 600000UL, 0);
    assert(idle_poll(&state, 300000) == IDLE_ACTION_LIGHT_SLEEP);
    idle_notify_activity(&state, 310000);
    assert(idle_poll(&state, 369999) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 370000) == IDLE_ACTION_SCREEN_OFF);
    assert(idle_poll(&state, 610000) == IDLE_ACTION_LIGHT_SLEEP);
    assert(idle_poll(&state, 910000) == IDLE_ACTION_DEEP_SLEEP);

    // Deep implies the earlier stages even on a clock jump.
    idle_init(&state, 60000UL, 300000UL, 600000UL, 0);
    assert(idle_poll(&state, 900000) == IDLE_ACTION_DEEP_SLEEP);
    assert(state.screen_off);
    assert(idle_poll(&state, 900001) == IDLE_ACTION_NONE);

    // Misconfigured order is normalized, not silently dropped.
    idle_init(&state, 60000UL, 10000UL, 10000UL, 1000);
    assert(state.light_sleep_ms == 120000UL);
    assert(state.deep_sleep_ms == 180000UL);
    assert(idle_poll(&state, 61000) == IDLE_ACTION_SCREEN_OFF);
    assert(idle_poll(&state, 121000) == IDLE_ACTION_LIGHT_SLEEP);
    assert(idle_poll(&state, 181000) == IDLE_ACTION_DEEP_SLEEP);

    // Non-monotonic clock never triggers an action.
    idle_init(&state, 60000UL, 300000UL, 600000UL, 5000);
    assert(idle_poll(&state, 4000) == IDLE_ACTION_NONE);

    puts("idle sleep policy tests: PASS");
    return 0;
}
