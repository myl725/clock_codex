# clock_codex 当前状态

## 目的

本文记录 `clock_codex` 当前的实现状态，方便新的 Codex 会话、另一台机器，或未来贡献者无需依赖聊天记录，就能快速恢复工作。

本次更新对应的里程碑：

- LVGL bring-up 已完成
- ILI9341 显示与 XPT2046 触摸已迁移
- 语音相关工作已暂停
- 已增加 WS2812 控制路径
- 已完成 MAX98357A 音频输出 bring-up
- 已增加 `WAV` 与 `MP3` 的 SD 音频播放
- 已通过 `OTG` 端口完成首次真实 USB 音箱播放验证

## 当前可工作的部分

- `LVGL 8.3.11` 已通过 PlatformIO `lib_deps` 集成。
- 项目可启动，且已有可工作的 `lvgl_task`、`lv_tick_inc()` 与显示 flush 循环。
- `ILI9341` 显示驱动已迁移到 `components/display`。
- `XPT2046` 触摸驱动已迁移到 `components/touch`。
- 当前主线应用启动后会进入一个包含 `Lighting` 与 `Audio` 标签页的 LVGL 控制界面。
- 已应用的基础触摸稳定性修复包括：
  - 有符号坐标运算
  - 屏幕边界钳制
  - 3 样本中值滤波
  - 重复初始化保护
- 治理文档与本地 skill 已存在于以下位置：
  - `README.md`
  - `docs/architecture.md`
  - `docs/coding-rules.md`
  - `docs/migration-rules.md`
  - `docs/versioning.md`
  - `docs/runbook.md`
  - `docs/hardware-wiring.md`
  - `.codex/skills/clock-codex-governance/`

## 音频 / 语音状态

与语音相关的实现仍保留在仓库中以供参考，但它们不属于当前主线激活构建路径。

当前状态：

- `components/audio_input`
  - 包含之前的 `INMP441` 实验
- `components/app_services/app_speech_service.c`
  - 包含此前基于 `esp-sr` 的离线唤醒词 / 命令词尝试
- `components/audio_output`
  - 现在包含当前激活的 `MAX98357A` 播放输出路径
  - 音调播放默认使用 `44.1kHz`，文件播放时可在运行期重配置
- `components/app_services/app_music_service.c`
  - 现在包含当前有效的音调播放基线，以及 `Tone / SD Audio` 播放控制
  - `SD Audio` 模式现在会扫描 SD 根目录中的可播放 `.wav` 与 `.mp3` 文件，并允许 UI 在它们之间切换
- `components/app_services/app_music_wav.c`
  - 现在包含用于音乐播放的首版 `16-bit PCM WAV` 读取器
- `components/app_services/app_music_mp3.c`
  - 现在包含面向 SD 音乐播放的首版流式 MP3 解码路径
- `components/app_services/app_music_playback_source.c`
  - 现在负责 `Tone`、`SD Audio` 与当前 `USB Audio` 后端槽位的源后端选择
- `components/app_services/app_music_usb_stream.c`
  - 现在负责服务层侧的 USB PCM 环形缓冲区骨架，供未来原生 USB 音箱路径使用
- `components/app_services/app_usb_audio_service.c`
  - 现在负责安装 TinyUSB 设备栈、跟踪 USB 音频流状态，并将收到的立体声 PCM 转发到共享音乐服务 USB 缓冲区
  - 现在使用 TinyUSB 的有界分块读取，而不再尝试把一个跨越多毫秒的突发音频一次性排进单包大小的栈缓冲区
- `components/app_services/app_usb_audio_descriptors.c`
  - 现在负责当前仅音箱输出的 USB Audio 描述符，用于 `OTG` bring-up 路径

当前决策：

- 语音工作已暂停
- 当前主线的 `src/main.c` 不再启动语音栈
- 当前主线构建重点是显示 / 触摸 + WS2812 + 本地 SD 音乐播放
- 当前 WAV 路径默认期望 `/sdcard/music.wav`
- 如果默认 WAV 文件缺失且已配置 Wi-Fi 凭据，服务层可以下载一个默认测试 WAV 到 SD 卡
- 当前 SD WAV 播放假定使用独立 SPI 总线：
  - `SCK` 在 `GPIO18`
  - `MISO` 在 `GPIO19`
  - `MOSI` 在 `GPIO21`
  - `CS` 在 `GPIO38`
- 当前 SD 音乐路径支持 `.wav` 与 `.mp3`
- `TEST3.MP3` 已可通过当前 MP3 路径播放
- 某些 MP3 文件，例如 `TEST2.MP3`，如果轻量解码器找不到可用的首个音频帧，仍会在早期失败
- 当前音频栈已经被解耦到足以让 `Tone`、`SD Audio` 与未来 `USB Audio` 后端共享同一套输出协调器
- 当前 USB 工作已经跨过首个硬件验证的播放里程碑：
  - TinyUSB 会在 `app_usb_audio_service_init()` 中于启动时安装
  - Windows 能通过 `OTG` 端口把开发板枚举为一个 USB 音频播放设备
  - 主机可以打开音频流接口，并通过现有的 `MAX98357A` 输出路径发送可播放音频
  - 服务任务现在会按数据包大小分块排空 TinyUSB 音频，并送入服务层自有的 USB PCM 环形缓冲区
  - 播放仍然会等待 USB 帧缓冲积累到一定程度后才启动 `I2S` 输出路径
  - 这条路径已经可工作，但质量仍处于 bring-up 级，而非成品级

## 当前硬件重点

当前激活硬件重点：

- 显示：`ILI9341`
- 触摸：`XPT2046`
- WS2812：`GPIO46`，已加入软件路径用于灯效 / 颜色测试
- 音频输出：`MAX98357A`，引脚为 `GPIO5/GPIO6/GPIO4`
- SD 卡：软件路径当前使用专用 SPI 引脚 `GPIO18/GPIO19/GPIO21/GPIO38`

已暂停硬件路径：

- `INMP441`
- 旧的语音实验

## 当前构建状态

当前主线构建状态：

- 与语音相关的组件已排除在当前构建路径之外
- 当前激活应用是一个同时包含灯光和 `Tone / SD Audio / USB Audio` 控制的 LVGL 界面
- TinyUSB USB 音频设备初始化现在已纳入启动流程
- 当前分支上 `pio run` 可成功执行
- 只要 `COM3` 空闲，`pio run -t upload` 可成功执行

最近一次成功的主线构建特征：

- RAM 约 `27.7%`
- Flash 约 `35.6%`

当前上传说明：

- 当开发板连接且没有其他工具占用端口时，可通过 `COM3` 上传

## 已知阻塞点

### 1. 某些 MP3 文件仍然不适合当前解码器

当前观察：

- `TEST3.MP3` 已经足够稳定，可打开并播放
- `TEST2.MP3` 仍会在轻量级 MP3 解码器中走到很早的 `prime returned no audio` 失败路径

这意味着当前 `minimp3` 集成已经足够支撑首版 SD 音乐播放，但对所有 MP3 编码变体还不够健壮。

### 2. 语音代码被有意移出主线路径

这不是 bug。

这是当前阶段的明确项目决策：

- 不要在当前线程里继续花时间做 `INMP441` bring-up
- 语音代码只保留作参考
- 将实现注意力转向 `WS2812`

### 3. 当前 SD 接线与长期 USB 音频方向冲突

这是一个规划阻塞，而不是当前构建失败。

当前原因：

- 项目希望转向原生 `ESP32-S3` USB 音频设备路径
- 原生 USB 使用 `GPIO19/GPIO20`
- 当前 SD 卡接线已经使用了 `GPIO19`

这意味着：

- SD 仍然适合作为临时回归音源
- 但当前 SD 硬件映射不应被视为长期音频架构的最终方案

### 4. USB 音频已可工作，但仍只是 bring-up 级质量

当前观察：

- 代码现在已经完成 TinyUSB 安装、发布音箱描述符集合，并具备首版可工作的音频控制回调
- 代码已经包含数据包摄取与服务层自有 PCM 环形缓冲区，再交接到现有 `audio_output` 路径
- 这条路径已经通过 `OTG` 端口在真实 Windows 主机上验证

这意味着：

- 软件边界已经建立，首轮播放也已打通
- 但主机兼容性、长时间播放稳定性，以及围绕 USB / 本地音源控制的 UI 打磨仍需继续

## 建议的后续步骤

按以下顺序恢复工作：

1. 重新连接 ESP32-S3 开发板，并确认 `COM3`
2. 上传当前显示 / 触摸 / WS2812 / 音频主线构建
3. 验证以下内容：
   - 显示仍然正常
   - 触摸仍然正常
   - WS2812 控制标签页显示正确
   - 灯效切换正常
   - 颜色切换正常
   - 音频标签页可干净地启动和停止音调
   - `SD Audio` 能播放已知良好的 `WAV` 与 `MP3` 文件，例如 `TEST3.MP3`
4. 转向 `OTG + TTL` 的 USB 音频验证：
   - 保持 `TTL` 连接，用于日志与刷机
   - 将 `OTG` 连接到主机 PC
   - 检查主机是否将设备识别为 `Clock Codex USB Speaker`
   - 观察是否出现 `tinyusb attached`、`set interface ...` 与 `usb stream started ...` 日志
5. 当首次播放成功后：
   - 检查数据包摄取计数器与 USB 环形缓冲区健康情况
   - 如有需要，调优 underrun 或 overflow 行为
   - 清理围绕 `USB Audio` 源选择的 UI 行为
   - 一旦 SD 不再需要承担回归职责，就弱化或移除其产品级行为

## WS2812 方向

当前实现线程已经包含：

- 已确认数据引脚：`GPIO46`
- 干净的 `bsp` -> `ws2812` -> `app_services` -> `app_ui` 控制路径
- 用于测试的 UI，支持：
  - 灯效选择
  - 颜色选择
  - off / solid / blink / breathe / rainbow 行为
- 干净的 `bsp` -> `audio_output` -> `app_services` -> `app_ui` 音频路径
- 用于测试的 UI，支持：
  - `44.1kHz` 播放启动 / 停止
  - 文件播放时运行期采样率重配置
  - `Tone` 与 `SD Audio` 间的源切换
  - 音调或 SD 文件选择
- MAX98357A 输出验证

## 重要文件

- `src/main.c`
- `components/bsp/include/board_config.h`
- `components/display/display_ili9341.c`
- `components/touch/touch_xpt2046.c`
- `platformio.ini`
- `sdkconfig.defaults`
- `partitions.csv`
- `tools/configure_upload.py`
- `tools/upload_via_flash_args.py`

仓库中保留作参考的语音文件：

- `components/audio_input/audio_input.c`
- `components/app_services/app_speech_service.c`

## 恢复说明

如果未来某次会话需要从这里继续，请先阅读：

1. `docs/current-status.md`
2. `docs/architecture.md`
3. `docs/versioning.md`
4. `docs/runbook.md`
5. `docs/hardware-wiring.md`
6. `docs/usb-audio-roadmap.md`
7. `platformio.ini`
8. `components/bsp/include/board_config.h`

然后确认当前意图是：

- 保持显示 / 触摸稳定
- 还是继续推进音频重构与 USB 音频准备工作
