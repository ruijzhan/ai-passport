// main/ui_pages.c — see ui_pages.h.
#include "ui_pages.h"

ui_page_t ui_pages_next(ui_page_t page)
{
    if (page < 0 || page >= UI_PAGE_COUNT) {
        return UI_PAGE_OVERVIEW;
    }
    return (ui_page_t)((page + 1) % UI_PAGE_COUNT);
}

ui_page_t ui_pages_prev(ui_page_t page)
{
    if (page < 0 || page >= UI_PAGE_COUNT) {
        return UI_PAGE_OVERVIEW;
    }
    return (ui_page_t)((page + UI_PAGE_COUNT - 1) % UI_PAGE_COUNT);
}

const char *ui_pages_hint(ui_page_t page)
{
    switch (page) {
    case UI_PAGE_OVERVIEW:
        return "OK REFRESH    HOLD SLEEP";
    case UI_PAGE_DETAIL:
    case UI_PAGE_SYS:
        return "UP/DN PAGE  OK REFRESH";
    default:
        return "?";
    }
}
