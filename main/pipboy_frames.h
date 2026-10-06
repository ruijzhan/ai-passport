// main/pipboy_frames.h — Pip-Boy mascot animation frames for ui_home.
// Derived from the 8-frame Vault Boy walk cycle in the local reference
// project folotoy-pipboy-living-clock (main/vaultboy_frames.c, A8
// 104x150), halved with a 2x2 box filter. Third-party Fallout / Vault
// Boy visual material; not covered by this repository's MIT license.
#pragma once

#include "lvgl.h"

#define PIPBOY_FRAME_COUNT 8
#define PIPBOY_FRAME_WIDTH 52
#define PIPBOY_FRAME_HEIGHT 75

LV_IMAGE_DECLARE(pipboy_frame_0);
LV_IMAGE_DECLARE(pipboy_frame_1);
LV_IMAGE_DECLARE(pipboy_frame_2);
LV_IMAGE_DECLARE(pipboy_frame_3);
LV_IMAGE_DECLARE(pipboy_frame_4);
LV_IMAGE_DECLARE(pipboy_frame_5);
LV_IMAGE_DECLARE(pipboy_frame_6);
LV_IMAGE_DECLARE(pipboy_frame_7);
