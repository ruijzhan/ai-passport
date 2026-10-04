#include <assert.h>
#include <stdio.h>

#include "idle_sleep.h"

int main(void) {
    idle_state_t state;

    // Basic timeouts: screen off at 60 s, shutdown at 600 s.
    idle_init(&state, IDLE_SCREEN_OFF_MS, IDLE_SHUTDOWN_MS, 0);
    assert(IDLE_SCREEN_OFF_MS == 60000UL);
    assert(IDLE_SHUTDOWN_MS == 600000UL);
    assert(idle_poll(&state, 59999) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 60000) == IDLE_ACTION_SCREEN_OFF);
    // Screen-off is emitted once until activity resumes.
    assert(idle_poll(&state, 60001) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 599999) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 600000) == IDLE_ACTION_SHUTDOWN);
    // Terminal: further polls stay silent.
    assert(idle_poll(&state, 700000) == IDLE_ACTION_NONE);

    // Activity clears the screen-off flag and restarts both timers.
    idle_init(&state, 60000UL, 600000UL, 0);
    assert(idle_poll(&state, 60000) == IDLE_ACTION_SCREEN_OFF);
    idle_notify_activity(&state, 70000);
    assert(idle_poll(&state, 129999) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 130000) == IDLE_ACTION_SCREEN_OFF);
    assert(idle_poll(&state, 669999) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 670000) == IDLE_ACTION_SHUTDOWN);

    // Activity just before screen-off prevents it.
    idle_init(&state, 60000UL, 600000UL, 0);
    idle_notify_activity(&state, 59999);
    assert(idle_poll(&state, 60000) == IDLE_ACTION_NONE);
    assert(idle_poll(&state, 119999) == IDLE_ACTION_SCREEN_OFF);

    // Shutdown implies screen-off even when the screen was still on.
    idle_init(&state, 60000UL, 61000UL, 0);
    assert(idle_poll(&state, 61000) == IDLE_ACTION_SHUTDOWN);
    assert(state.screen_off);

    // Misconfigured shutdown <= screen-off is normalized, not silently dropped.
    idle_init(&state, 60000UL, 10000UL, 1000);
    assert(state.shutdown_ms == 120000UL);
    assert(idle_poll(&state, 61000) == IDLE_ACTION_SCREEN_OFF);
    assert(idle_poll(&state, 121000) == IDLE_ACTION_SHUTDOWN);

    // Non-monotonic clock never triggers an action.
    idle_init(&state, 60000UL, 600000UL, 5000);
    assert(idle_poll(&state, 4000) == IDLE_ACTION_NONE);

    puts("idle sleep policy tests: PASS");
    return 0;
}
