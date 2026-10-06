// tests/test_ui_pages.c — host tests for main/ui_pages.c.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ui_pages.h"

int main(void)
{
    assert(UI_PAGE_COUNT == 3);
    assert(UI_PAGE_OVERVIEW == 0);

    // Carousel order.
    assert(ui_pages_next(UI_PAGE_OVERVIEW) == UI_PAGE_DETAIL);
    assert(ui_pages_next(UI_PAGE_DETAIL) == UI_PAGE_SYS);
    assert(ui_pages_next(UI_PAGE_SYS) == UI_PAGE_OVERVIEW);
    assert(ui_pages_prev(UI_PAGE_OVERVIEW) == UI_PAGE_SYS);
    assert(ui_pages_prev(UI_PAGE_SYS) == UI_PAGE_DETAIL);
    assert(ui_pages_prev(UI_PAGE_DETAIL) == UI_PAGE_OVERVIEW);

    // Bad input recovers to the overview page.
    assert(ui_pages_next((ui_page_t)-1) == UI_PAGE_OVERVIEW);
    assert(ui_pages_next(UI_PAGE_COUNT) == UI_PAGE_OVERVIEW);
    assert(ui_pages_prev((ui_page_t)-1) == UI_PAGE_OVERVIEW);
    assert(ui_pages_prev(UI_PAGE_COUNT) == UI_PAGE_OVERVIEW);

    // Hints stay uppercase terminal text.
    assert(strcmp(ui_pages_hint(UI_PAGE_OVERVIEW), "OK REFRESH    HOLD SLEEP") == 0);
    assert(strcmp(ui_pages_hint(UI_PAGE_DETAIL), "UP/DN PAGE  OK REFRESH") == 0);
    assert(strcmp(ui_pages_hint(UI_PAGE_SYS), "UP/DN PAGE  OK REFRESH") == 0);
    assert(strcmp(ui_pages_hint(UI_PAGE_COUNT), "?") == 0);

    puts("ui_pages tests: PASS");
    return 0;
}
