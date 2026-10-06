// main/ui_pages.h — top-level page state machine without ESP-IDF/LVGL.
//
// Keeps page order and footer hints testable on the host; main/ui_home.c
// renders the resulting page with LVGL objects.
#pragma once

typedef enum {
    UI_PAGE_OVERVIEW = 0,  // quota bars (existing layout)
    UI_PAGE_DETAIL,        // per-window reset detail
    UI_PAGE_SYS,           // wifi/battery/uptime/fetch status
    UI_PAGE_COUNT,
} ui_page_t;

// Linear carousel: OVERVIEW -> DETAIL -> SYS -> OVERVIEW.
ui_page_t ui_pages_next(ui_page_t page);
ui_page_t ui_pages_prev(ui_page_t page);

// Footer hint for the page; "?" on a bad index. All pages keep
// OK=refresh and long-press=sleep; UP/DOWN switch pages.
const char *ui_pages_hint(ui_page_t page);
