# clock_codex 硬件接线说明

## 目的

本文记录当前 `clock_codex` 代码库所假定的硬件连接方式。
在 bring-up、迁移或在新机器上调试时，用它来避免反复猜测引脚映射。

## 开发板假设

当前目标板：

- `ESP32-S3`
- 假定为 `N16R8` 风格模组
- PlatformIO 环境：`esp32-s3-devkitm-1`

## 显示接线

当前显示目标：

- `ILI9341`

来自 `components/bsp/include/board_config.h` 的板级映射：

- `GPIO13` -> LCD `MISO`
- `GPIO11` -> LCD `MOSI`
- `GPIO12` -> LCD `CLK`
- `GPIO10` -> LCD `CS`
- `GPIO17` -> LCD `DC`
- `GPIO16` -> LCD `RST`

当前显示总线 host：

- `SPI2_HOST`

显示分辨率：

- `320 x 240`

## 触摸接线

当前触摸目标：

- `XPT2046`

板级映射：

- 与显示共用 SPI 总线
- `GPIO8` -> 触摸 `CS`

说明：

- 触摸当前使用保存在 `board_config.h` 中的软件校准值
- 触摸已作为 LVGL 指针输入设备集成

## 音频输出接线

当前音频输出目标：

- `MAX98357A`

当前项目说明：

- 语音 / 麦克风相关工作已暂停
- 当前激活音频路径只有播放输出

当前 I2S 映射：

- `GPIO5` -> `MAX98357A BCLK`
- `GPIO6` -> `MAX98357A LRC`
- `GPIO4` -> `MAX98357A DIN`
- `MAX98357A OUT+/-` -> 喇叭

当前软件预期：

- 音调播放默认使用 `44.1kHz`
- 文件播放可重配置输出采样率，以匹配受支持的 SD 音频文件，例如 `48kHz MP3`
- 使用 I2S 标准 TX 模式
- `MCLK` 未使用

## 已暂停的麦克风说明

早期的 `INMP441` 实验复用了同一组 `GPIO5/GPIO6/GPIO4` I2S 引脚。

这意味着：

- 当前音乐路径与旧麦克风路径不应被视为可同时激活
- 如果这些线上仍接着 `INMP441`，在重新处理语音前应先复查接线

## WS2812 接线

当前 LED 目标：

- `WS2812`

当前说明：

- `GPIO46` -> `WS2812 DIN`
- 当前软件在 `board_config.h` 中假定共有 `10` 颗 LED
- 一旦真实 LED 数量被确认，应同步更新本文档与 `board_config.h`

## SD 卡接线

当前软件假设：

- SD 卡使用一条独立于显示 / 触摸路径之外的 SPI 总线
- `GPIO19` -> SD `MISO`
- `GPIO21` -> SD `MOSI`
- `GPIO18` -> SD `SCK`
- `GPIO38` -> SD `CS`

当前软件预期：

- 挂载点：`/sdcard`
- 示例 WAV 路径：`/sdcard/music.wav`
- 当前首版支持格式：
  - `16-bit PCM WAV`，单声道或立体声
  - 通过当前轻量解码路径播放 `MP3`

当前说明：

- SD 不再与 LCD / 触摸共用 SPI 引脚组
- 软件现在假定卡使用独立的 `SPI3_HOST`
- 当前已知良好的 MP3 测试文件是 `TEST3.MP3`
- 某些 MP3 文件，例如 `TEST2.MP3`，仍会在解码器 priming 阶段很早失败，后续还需继续处理

## 软件事实来源

如有疑问，请以以下文件中的最新映射为准：

- `components/bsp/include/board_config.h`

不要只改代码中的接线假设而不更新本文档。
