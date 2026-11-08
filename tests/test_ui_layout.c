// tests/test_ui_layout.c — portrait layout contract for ui_home.c.
// Geometry comes from ui_home_layout.h, the header ui_home.c uses, so this
// test cannot pass against a stale copy. Host-checkable: geometry only, no
// LVGL dependency.
#include <assert.h>
#include <stdio.h>

#include "bsp_pins.h"
#include "ui_home_layout.h"
#include "usage_parse.h"

int main(void)
{
    // Display contract: logical frame is portrait.
    assert(BSP_LCD_W == 240 && BSP_LCD_H == 320);

    // Rows: inside frame, bars clear the rule above and the mascot below.
    assert(CONTENT_X + CONTENT_W <= BSP_LCD_W - CONTENT_X);
    for (int i = 0; i < USAGE_WINDOW_COUNT; i++) {
        const int y = OVERVIEW_Y0 + i * OVERVIEW_DY;
        assert(y >= RULE_Y + RULE_H);
        assert(y + BAR_TOP_DY + BAR_H <= ANIM_Y - 4);
    }

    // Lower-right mascot viewport stays inside the content margins and
    // clears the footer.
    assert(ANIM_X + ANIM_W <= BSP_LCD_W - CONTENT_X);
    assert(ANIM_Y + ANIM_H <= FOOTER_Y - 4);

    // Month-end projection reads out in the empty band below the monthly
    // bar, clears the page/status row, and stays left of the mascot.
    {
        const int monthly_bar_bottom =
            OVERVIEW_Y0 + (USAGE_WINDOW_COUNT - 1) * OVERVIEW_DY +
            BAR_TOP_DY + BAR_H;
        assert(PROJ_LABEL_Y >= monthly_bar_bottom + 4);
        assert(PROJ_VALUE_Y + PROJ_H <= PAGE_Y - 4);
    }

    // Mascot sprite: horizontally centered and vertically centered in the
    // viewport, inside the brackets.
    {
        const int sprite_x = ANIM_X + (ANIM_W - PIPBOY_FRAME_WIDTH) / 2;
        const int sprite_y = ANIM_Y + (ANIM_H - PIPBOY_FRAME_HEIGHT) / 2;
        assert(sprite_x >= ANIM_X);
        assert(sprite_x + PIPBOY_FRAME_WIDTH <= ANIM_X + ANIM_W);
        assert(sprite_y >= ANIM_Y);
        assert(sprite_y + PIPBOY_FRAME_HEIGHT <= ANIM_Y + ANIM_H);
    }

    // Bottom rows: page indicator, status line, and footer all fit.
    assert(PAGE_X + PAGE_W <= BSP_LCD_W);
    assert(STATUS_X + STATUS_W <= ANIM_X - 8);
    assert(FOOTER_X + FOOTER_W <= BSP_LCD_W);
    assert(FOOTER_Y + FOOTER_H <= BSP_LCD_H);

    // Header rule spans the content width and stays below the clock/date.
    assert(RULE_Y >= 44);
    assert(CONTENT_X + RULE_W <= BSP_LCD_W - CONTENT_X);

    // Pace marker and overage slice use the bar's full 0..100 pixel span.
    assert(USAGE_BAR_X(0) == CONTENT_X);
    assert(USAGE_BAR_X(100) == CONTENT_X + CONTENT_W);
    assert(USAGE_BAR_X(50) == CONTENT_X + CONTENT_W / 2);
    for (int pct = 1; pct <= 100; pct++) {
        assert(USAGE_BAR_X(pct) >= USAGE_BAR_X(pct - 1));
        assert(USAGE_BAR_X(pct) <= CONTENT_X + CONTENT_W);
    }

    puts("UI layout tests: PASS");
    return 0;
}
