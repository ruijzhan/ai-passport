<p align="right">
  <a href="README.md">English</a> · <strong>简体中文</strong>
</p>

# 基线 Demo（仅供参考）

本目录存放此前的硬件测试 demo（`baseline_main.c`、`demo_*.c`、`ui_pixel.*`），
已从 `main/` 移出，以便 OpenCode Go 额度应用从干净的入口开始。这些文件**不**
参与固件构建；`main/CMakeLists.txt` 已不再列出它们。

它们仅作为 BSP 用法参考（显示、按键、音频、电量、Wi-Fi 扫描、BLE 广播、
低功耗）。`tests/` 中的主机测试仍会用桩模块编译其中一部分，以保留其运行时
契约。

不要在此添加新功能。新应用应放在 `main/` 并拥有自己的界面；强制 UI 重设计
规则见 `docs/development/ai-guide.zh_CN.md`。
