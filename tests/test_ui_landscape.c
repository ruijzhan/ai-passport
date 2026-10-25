// tests/test_ui_landscape.c — landscape layout contract for ui_home.c.
// The ST7789 panel is portrait glass; this branch rotates the frame 90° CCW
// in MADCTL so LVGL sees 320x240. Every widget in main/ui_home.c must live
// inside that logical frame without overlapping the mascot viewport or the
// footer. Geometry comes from ui_home_layout.h, the header ui_home.c uses,
// so this test cannot pass against a stale copy. Host-checkable: geometry
// only, no LVGL dependency.
#include <assert.h>
#include <stdio.h>

#include "bsp_pins.h"
#include "ui_home_layout.h"
#include "usage_parse.h"

int main(void)
{
    // Display contract: logical frame is landscape.
    assert(BSP_LCD_W == 320 && BSP_LCD_H == 240);

    // Rows: inside frame, bars clear the rule above and the page row below.
    assert(CONTENT_X + CONTENT_W <= ANIM_X - 6);
    for (int i = 0; i < USAGE_WINDOW_COUNT; i++) {
        const int y = OVERVIEW_Y0 + i * OVERVIEW_DY;
        assert(y >= RULE_Y + RULE_H);
        assert(y + BAR_TOP_DY + BAR_H <= PAGE_Y - 2);
    }

    // Side columns: left quota column and mascot viewport share the frame.
    // The borderless viewport may sit 4px from the screen edge: rounded
    // corners only clip y<30 / y>210, clear of y=56..204.
    assert(ANIM_X + ANIM_W <= BSP_LCD_W - 4);
    assert(ANIM_Y + ANIM_H <= FOOTER_Y - 4);

    // Mascot sprite: horizontally centered, bottom-aligned (not centered).
    {
        const int sprite_x = ANIM_X + (ANIM_W - PIPBOY_FRAME_WIDTH) / 2;
        const int sprite_y = ANIM_Y + ANIM_H - PIPBOY_FRAME_HEIGHT -
            ANIM_SPRITE_BOTTOM_PAD;
        const int centered_y = ANIM_Y + (ANIM_H - PIPBOY_FRAME_HEIGHT) / 2;
        assert(sprite_x >= ANIM_X);
        assert(sprite_x + PIPBOY_FRAME_WIDTH <= ANIM_X + ANIM_W);
        assert(sprite_y + PIPBOY_FRAME_HEIGHT + ANIM_SPRITE_BOTTOM_PAD ==
               ANIM_Y + ANIM_H);
        assert(sprite_y > centered_y);
    }

    // Bottom rows: page indicator, status line, and footer all fit.
    assert(PAGE_X + PAGE_W <= BSP_LCD_W);
    assert(FOOTER_X + FOOTER_W <= BSP_LCD_W);
    assert(FOOTER_Y + FOOTER_H <= BSP_LCD_H);

    // Header rule spans the content width and stays below the clock/date.
    assert(RULE_Y >= 44);
    assert(CONTENT_X + RULE_W <= BSP_LCD_W - CONTENT_X);

    puts("UI landscape layout tests: PASS");
    return 0;
}
