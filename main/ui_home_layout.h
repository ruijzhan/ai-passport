// main/ui_home_layout.h — screen geometry for the Pip-Boy home UI.
// Shared by main/ui_home.c and tests/test_ui_layout.c so the layout
// contract has one source of truth. Pure macros: no LVGL dependency.
#pragma once

#include "bsp_pins.h"

// Right edge of the screen: the rounded-corner mask
// (BSP_LVGL_SCREEN_RADIUS=30) clips the top-right bars.
#define WIFI_BARS_RIGHT (BSP_LCD_W - 16)
#define WIFI_BARS_BOTTOM 20
#define WIFI_BAR_W 6
#define WIFI_BAR_GAP 3

// Shared geometry for the quota rows and the status line.
// The 240px portrait frame leaves 224px between the 8px margins; the
// three quota rows stack above the mascot viewport.
#define CONTENT_X 8
#define CONTENT_W 224
// Tall bars fill the space left by removing the per-window RESET lines
// (reset times live on the DETAIL page): last bar bottom (188+16=204)
// clears the mascot viewport top (208) by 4px.
#define BAR_H 16
// Bar top offset inside its 56px row (title line above).
#define BAR_TOP_DY 20
// Horizontal edge for a 0..100 usage or time-progress value: the bar
// spans CONTENT_W pixels from CONTENT_X, and the returned x is the
// exclusive right edge of that fraction. The pace marker is centered on
// it and the overage slice starts there.
#define USAGE_BAR_X(pct) (CONTENT_X + (CONTENT_W * (pct)) / 100)
// Overview rhythm: three rows from y=56 in 56px steps (bars end at 204,
// mascot viewport starts at 208).
#define OVERVIEW_Y0 56
#define OVERVIEW_DY 56

// Green rule separating the status header from the quota rows.
#define RULE_Y 48
#define RULE_H 2
#define RULE_W (BSP_LCD_W - 2 * CONTENT_X)

// Lower-right mascot viewport: green viewfinder brackets tightly around the
// 52x75 walk-cycle sprite (4px side padding, ~1-2px top/bottom). The bottom
// edge stops 4px above the footer and the right edge aligns with the rest
// of the content (x=232). Top edge at y=208 clears the monthly bar
// (ends y=204) by 4px.
#define ANIM_X 172
#define ANIM_Y 208
#define ANIM_W 60
#define ANIM_H 78
#define ANIM_BRACKET_ARM 10
#define ANIM_BRACKET_T 2

// Mascot sprite dimensions (the frame data lives in pipboy_frames.h).
#define PIPBOY_FRAME_WIDTH 52
#define PIPBOY_FRAME_HEIGHT 75

// Dim footer bar with dark text, like the reference terminal footer.
#define FOOTER_X CONTENT_X
#define FOOTER_Y 290
#define FOOTER_W RULE_W
#define FOOTER_H 17

// Page indicator "1/3" at the bottom-left above the footer (y=270):
// left-aligned like the status line; the mascot viewport (x>=172)
// never overlaps it and it clears the footer (y=290) by ~6px.
#define PAGE_X CONTENT_X
#define PAGE_Y 270
#define PAGE_W 44

// Status text shares the page-indicator row: starts right after "1/3"
// with an 8px gap, single line with ellipsis so a long error never
// wraps into the footer. Right edge (164) clears the mascot viewport
// (x>=172) by 8px.
#define STATUS_X (PAGE_X + PAGE_W + 8)
#define STATUS_Y PAGE_Y
#define STATUS_W (ANIM_X - STATUS_X - 8)

// Detail rows mirror the overview rhythm (56px each from y=56) so the
// mascot viewport (top y=208) stays clear: title line plus one status
// line per window.
#define DETAIL_SUB_DY 18

// System rows: five status lines from y=56 in 24px steps.
#define SYS_Y0 56
#define SYS_DY 24

// Battery group is right-aligned at the content edge: [icon] 4px "87%".
// Text width is measured at runtime so 8%..100% all hug the icon.
#define BATT_GROUP_RIGHT (BSP_LCD_W - CONTENT_X)
#define BATT_FRAME_Y 32
#define BATT_FRAME_W 25
#define BATT_FRAME_H 13
#define BATT_TIP_W 3
#define BATT_TIP_H 6
#define BATT_FILL_PAD 2
#define BATT_FILL_MAX_W (BATT_FRAME_W - 2 * BATT_FILL_PAD)
#define BATT_FILL_MAX_H (BATT_FRAME_H - 2 * BATT_FILL_PAD)
