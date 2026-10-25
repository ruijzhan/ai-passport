// main/ui_home_layout.h — screen geometry for the Pip-Boy home UI.
// Shared by main/ui_home.c and tests/test_ui_landscape.c so the layout
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
// Left column (x=8..254) holds the three quota rows; the right side
// (x=260..316) holds the mascot viewport. Bars widened 240 -> 246,
// near the physical limit for a fixed 52px sprite on a 320px screen
// (6px left margin/gap budget); the 2px pace marker stays inside.
#define CONTENT_X 8
#define CONTENT_W 246
// Tall bars fill the space left by removing the per-window RESET lines
// (reset times live on the DETAIL page): last bar bottom (174+16=190)
// clears the page/status row (y=192) by 2px.
#define BAR_H 16
// Bar top offset inside its 49px row (title line above).
#define BAR_TOP_DY 20
// Overview rhythm: three rows from y=56 in 49px steps (bars end at 190,
// page row at 192 stays clear).
#define OVERVIEW_Y0 56
#define OVERVIEW_DY 49

// Green rule separating the status header from the quota rows.
#define RULE_Y 48
#define RULE_H 2
#define RULE_W (BSP_LCD_W - 2 * CONTENT_X)

// Right-side mascot viewport: the 52x75 walk-cycle sprite (2px side
// padding, 4px bottom padding). The viewport sits 4px from the screen
// edge; its borderless sprite visually aligns with the content ending
// at x=312. Top edge at y=56 aligns with the first quota row. The
// sprite sits on the viewport bottom instead of the viewport center so
// the tall viewport leaves no gap under its feet.
#define ANIM_X 260
#define ANIM_Y 56
#define ANIM_W 56
#define ANIM_H 148
#define ANIM_SPRITE_BOTTOM_PAD 4

// Mascot sprite dimensions (the frame data lives in pipboy_frames.h).
#define PIPBOY_FRAME_WIDTH 52
#define PIPBOY_FRAME_HEIGHT 75

// Dim footer bar with dark text, like the reference terminal footer.
#define FOOTER_X CONTENT_X
#define FOOTER_Y 210
#define FOOTER_W RULE_W
#define FOOTER_H 17

// Page indicator "1/3" at the bottom-left above the footer (y=192):
// left-aligned like the status line; the footer (y=210) clears it by ~4px.
#define PAGE_X CONTENT_X
#define PAGE_Y 192
#define PAGE_W 44

// Status text shares the page-indicator row: starts right after "1/3"
// with an 8px gap, single line with ellipsis so a long error never
// wraps into the footer. Right edge stops 8px left of the mascot
// viewport (x>=260 spans y=56..204, overlapping this row).
#define STATUS_X (PAGE_X + PAGE_W + 8)
#define STATUS_Y PAGE_Y
#define STATUS_W (ANIM_X - STATUS_X - 8)

// Detail rows mirror the overview rhythm (49px each from y=56):
// title line plus one status line per window.
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
