# clock_codex

`clock_codex` 是当前面向 `ESP32-S3` 智能终端项目的重构主代码库。
它取代了旧的 `weather_clock_esp_idf` 线路，并将当前架构、硬件说明、迁移规则以及 bring-up 历史统一保存在同一个仓库中。

当前主线是一个围绕显示、触摸、`WS2812`、本地音频播放，以及通过 `OTG` 端口实现首个可工作 USB 音箱路径的硬件 bring-up 与控制应用。

## 当前重点

当前已完成的里程碑：

- 已集成 `LVGL 8.3.11`
- 已迁移 `ILI9341` 显示驱动
- 已迁移 `XPT2046` 触摸驱动
- 已增加 `WS2812` 控制路径
- 已完成 `MAX98357A` 音频播放 bring-up
- 已加入支持 `WAV` 与首版 `MP3` 的 `SD Audio` 播放
- 已在真机上验证 `USB Audio Device` 最小播放闭环

当前仍在推进的方向：

- 保持显示 / 触摸 / 灯效 / 音频播放链路稳定
- 提升更多文件的 MP3 兼容性
- 将语音相关工作保持为仅供参考，不进入当前激活构建路径
- 保持新的 USB 音箱路径在真实主机上的稳定性
- 改进本地音频源与 USB 音频源切换时的 UX 与诊断能力
- 继续重构音频路径，朝更清晰的长期 USB 音箱产品线演进

## 优先阅读

如果你是在新的对话或新的机器上恢复这个项目，建议先阅读以下文件：

1. `docs/current-status.md`
2. `docs/architecture.md`
3. `docs/versioning.md`
4. `docs/runbook.md`
5. `docs/hardware-wiring.md`
6. `docs/usb-audio-roadmap.md`
7. `requirement.md`
8. `rules.md`

## 项目结构

- `src/`
  仅负责启动流程编排
- `components/bsp`
  板级引脚与硬件配置
- `components/display`
  显示驱动与 LVGL 显示注册
- `components/touch`
  触摸驱动与 LVGL 输入注册
- `components/ws2812`
  WS2812 底层驱动
- `components/audio_output`
  当前激活的 `MAX98357A` I2S 播放输出路径
- `components/audio_input`
  已暂停的 `INMP441` 实验，仅保留作参考
- `components/app_services`
  灯光服务、音乐播放服务，以及仅供参考的语音代码
- `components/app_ui`
  用于灯光与音频控制的 LVGL 界面
- `docs/`
  项目规则、状态记录、运行手册与硬件文档

## 构建状态

当前主线构建产物是一个 LVGL 控制应用，包含：

- 用于 `WS2812` 的 `Lighting` 标签页
- 用于 `Tone`、`SD Audio` 和 `USB Audio` 的 `Audio` 标签页
- 当已配置 Wi-Fi 且预期 SD 文件缺失时，可选地回退下载默认 WAV 测试文件
- 一个现在可以在 Windows 上完成枚举，并通过 `MAX98357A` 播放主机音频的 TinyUSB 音箱设备

当前分支上 `pio run` 可成功执行。只要正确的 ESP32-S3 串口存在且未被其他工具占用，上传也可正常工作。

精确的工作状态与调试流程请参见 `docs/current-status.md` 与 `docs/runbook.md`。

## 版本管理

本仓库默认使用 git 进行版本管理。

- `main`：最新、相对稳定的主线
- feature 分支：用于较大实验或隔离开发

具体规则请见 `docs/versioning.md`。
