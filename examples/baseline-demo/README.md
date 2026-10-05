<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Baseline Demo (Reference Only)

This directory holds the previous hardware-test demo (`baseline_main.c`,
`demo_*.c`, `ui_pixel.*`) moved out of `main/` so the OpenCode Go usage
application can start from a clean entry point. These files are **not** built
into the firmware; `main/CMakeLists.txt` no longer lists them.

They remain as a reference for BSP usage patterns (display, buttons, audio,
battery, Wi-Fi scan, BLE advertising, low power). The host tests in `tests/`
still compile some of them against stubs to preserve their runtime contracts.

Do not add new features here. New applications belong in `main/` with their own
screens; see `docs/development/ai-guide.md` for the mandatory UI redesign rule.
