# USB 音箱工作日志

## 目的

本文总结了本轮迭代中 `clock_codex` 已完成的主要 USB 音箱相关工作。
它既是交接说明，也是恢复笔记，便于后续会话理解：

- 实现了什么
- 按什么顺序实现的
- 遇到了哪些问题
- 如何诊断并修复这些问题

## 目标

将当前这个 `ESP32-S3 + MAX98357A + LVGL + WS2812` bring-up 项目，通过 `OTG` 口改造成一个可工作的
`USB Audio Device` 音箱，同时保留现有 `I2S` 输出路径，并维持通过独立 `TTL` 端口进行调试的能力。

目标播放链路：

`PC -> USB Audio Device (ESP32-S3) -> TinyUSB/UAC -> PCM -> I2S -> MAX98357A -> Speaker`

## 起点

在 USB 音箱相关工作开始之前，仓库里已经具备：

- 可工作的 `LVGL + ILI9341 + XPT2046`
- 可工作的 `WS2812` 控制路径
- 可工作的 `MAX98357A` 输出路径
- 本地 `Tone` 播放
- 支持 `WAV` 和首版 `MP3` 的 `SD Audio` 播放

那时的音频代码耦合度仍然偏高，还不足以平滑迁移到 USB 音频流式播放。

## 主要实现阶段

## 阶段 1：解耦音频源与音频输出

### 已实现内容

- 在音乐栈中引入更清晰的 source/output 分离
- 增加内部音源抽象文件，使播放协调逻辑不再直接持有全部 `tone/wav/mp3` 细节
- 将以下职责拆开：
  - source 打开
  - source 读取
  - source 关闭
  - 通过 `audio_output` 进行输出传输

### 主要涉及文件

- `components/app_services/app_music_source.*`
- `components/app_services/app_music_playback_source.*`
- `components/app_services/app_music_service.c`

### 为什么重要

这为未来接入 `USB Audio` 音源后端创造了一个稳定的插入点，无需重写整个播放循环。

## 阶段 2：保留 Tone 和 SD 作为回归音源

### 已实现内容

- 保留 `Tone`
- 保留 `SD WAV`
- 保留 `SD MP3`
- 在重构播放内部实现时，将它们作为回归参考源

### 为什么重要

这降低了风险。后续 USB 相关工作失败时，仍然可以判断问题更可能位于：

- 输出侧
- 音源侧
- 主机 USB 侧

## 阶段 3：将 USB Audio 引入为新的播放后端

### 已实现内容

- 新增 `APP_MUSIC_SOURCE_USB_AUDIO`
- 在音源选择层增加 USB 播放后端槽位
- 引入服务层自有的 USB PCM 环形缓冲区
- 将 TinyUSB 启动流程接入开机路径

### 主要涉及文件

- `components/app_services/app_music_usb_stream.*`
- `components/app_services/app_usb_audio_service.*`
- `components/app_services/app_usb_audio_descriptors.*`
- `src/main.c`

### 结果

架构层面已经能够把 `USB Audio` 当作另一种播放源，与现有 `audio_output` 模块共用同一条输出路径。

## 阶段 4：跑通 TinyUSB 音箱枚举

### 已实现内容

- 开机时初始化 TinyUSB 设备栈
- 增加面向音箱的 USB 音频描述符
- 增加首版 USB Audio 类请求回调
- 围绕以下事件增加日志：
  - attach/detach
  - 接口 alternate setting 变化
  - entity/interface/endpoint 控制请求

### 早期问题

#### 问题 1：Windows 显示未知/错误设备

症状：

- 设备管理器中出现黄色感叹号
- `Code 10`
- `Code 43`
- `USB device descriptor request failed`

### 根因与修复

#### 修复 1：将描述符移到 DMA 安全内存

问题：

- `ESP32-S3` USB device 路径使用 DMA
- 从普通常量存储区提供 device/config 描述符会导致底层枚举失败

修复：

- 将 USB 描述符移动到 `CFG_TUD_MEM_SECTION`

这是一个重大突破。完成后，主机终于能够开始正常请求描述符。

#### 修复 2：调整描述符形态与 Windows 缓存行为

为了提升 Windows 侧的接受度，经历了多轮调整：

- 修改 PID 值，强制重新枚举
- 更新串号字符串，避免 Windows 复用过期设备缓存
- 简化并收紧描述符布局
- 让实现更贴近 TinyUSB 的 speaker 示例

#### 修复 3：修正 endpoint 假设

在 bring-up 过程中，为了更适配 ESP32-S3/TinyUSB 环境并避免无效组合，对 endpoint 布局做了调整。

### 结果

开发板最终能够在 Windows 上被成功枚举为 USB 音频播放设备。

## 阶段 5：拿到第一次真实 USB 播放

### 已实现内容

- 主机可以打开音频流接口
- USB 数据包能够被接收并推入服务层自有的 PCM 环形缓冲区
- 播放启动会等待足够的 USB 帧缓冲，再启动 `I2S`

### 新出现的问题

#### 问题 2：任务看门狗复位

症状：

- `task_wdt`
- USB worker 占用过多时间，导致 idle 时机被饿死

修复：

- 将 USB worker 调整到更安全的核心/优先级组合
- 放宽轮询行为
- 降低过于激进的任务时序

#### 问题 3：来自 `xTaskDelayUntil` 的 ESP 断言

症状：

- `assert failed: xTaskDelayUntil ... xTimeIncrement > 0U`

根因：

- `pdMS_TO_TICKS(1)` 在某些系统 tick 配置下可能收缩为 `0`

修复：

- 将轮询延时钳制为至少 `1` 个 tick

## 阶段 6：解决 USB 音频路径中的噪声与崩溃

这是技术上最重要的阶段。

### 问题 4：设备可以枚举并播放，但听到的只有噪声

观察到的现象：

- USB 设备显示正常
- 播放能够启动
- 输出听起来像嘶声/静电/噪声，而不是正常音频

### 问题 5：开发板在流处理中还会触发 `StoreProhibited` 崩溃

观察到的现象：

- 在 alternate-setting 切换后或流刚开始不久出现随机崩溃

### 实际根因

USB 数据摄取任务尝试一次性读取一个“数毫秒级”的音频突发，
但临时栈缓冲区的大小只够容纳一个 USB 包。

这会导致两种不同的失败模式：

- 音频数据被破坏，听起来像嘶声/噪声
- 缓冲区覆盖 / 内存破坏，最终导致崩溃

### 修复

修改 TinyUSB 音频读取逻辑，使其：

- 按受限的单包大小分块读取
- 以增量方式逐步排空 TinyUSB 音频数据
- 永远不再尝试把一个大突发塞进单包大小的本地缓冲区

### 主要涉及文件

- `components/app_services/app_usb_audio_service.c`

### 结果

这是让 USB 音箱路径真正可用的关键修复：

- 崩溃停止了
- 真实音频播放开始正常工作
- 由数据摄取破坏引发的嘶声/噪声消失了

## 阶段 7：减少日志噪声并清理运行期行为

### 调整内容

- 周期性的 USB 包统计不再默认以 `info` 级别频繁输出
- 高频渲染循环日志被减少
- `audio_output` 周期性成功写入日志被减少
- 控制请求日志被下调到 `debug`

### 为什么重要

这让串口日志重新变得可用，能够聚焦真实故障，而不是被稳定运行时的大量成功信息淹没。

## 阶段 8：向实际产品方向重做 UI

### 旧的 UI 方向

早先的音频 UI 仍然保留着本地 bring-up 阶段的痕迹：

- `Tone`
- `SD Audio`
- 文件/音调选择

### 新的目标方向

产品方向转向为：

1. `USB speaker` 控件
2. `WS2812 lighting` 控件

### 已实现内容

- 将主界面重写为两个标签页：
  - `Speaker`
  - `Lighting`
- 从主 Speaker 工作流中移除以音源选择为中心的 UI
- 让界面默认进入 `USB Audio` 源模式
- 增加 Speaker 状态页，显示：
  - USB 链路状态
  - 流状态
  - 采样率
  - 缓冲区占用
  - 包/错误计数器

### 主要涉及文件

- `components/app_ui/app_ui.c`

## 阶段 9：在 UI 中加入本地音箱控制

### 已实现内容

在屏幕上新增 USB 音箱控制：

- `Vol-`
- `Mute/Unmute`
- `Vol+`

### 工作方式

- UI 调用 USB 音频服务 API
- USB 音频服务维护：
  - `master_mute`
  - `master_volume_db_256`
- 接收到的 USB PCM 在被送入共享音乐服务 USB 路径前，会先进行音量调整

### 主要涉及文件

- `components/app_services/include/app_usb_audio_service.h`
- `components/app_services/app_usb_audio_service.c`
- `components/app_ui/app_ui.c`

## 阶段 10：提升显示响应并减少页面撕裂

### 观察到的问题

切换页面时，屏幕存在明显撕裂和卡顿。

### 第一次尝试

增大 `BOARD_LCD_DRAW_BUF_LINES`，让 LVGL 每次 flush 更大的数据块。

### 引入的新问题

这导致了启动期崩溃：

- `spi_master: txdata transfer > hardware max supported len`
- 在 `lvgl_flush_cb` 内崩溃

### 根因

更大的 LVGL flush 区域可能会超过 SPI 控制器单次传输的安全上限。

### 最终修复

不再依赖较小次数的整块区域传输，而是重写 ILI9341 flush 路径，使其能够：

- 将大 flush 拆分成安全的小块
- 每个小块分别更新 panel window
- 通过多次更小的 SPI 事务发送颜色缓冲区

### 主要涉及文件

- `components/display/display_ili9341.c`

### 额外的显示/UI 调优

- 将标签页动画时间降为零
- 禁用标签页内容拖动滚动
- 将 LVGL 任务的最大休眠窗口从 `20ms` 降到 `10ms`

### 结果

显示路径变得更安全，也更具扩展性：

- 使用更大 draw buffer 时不再出现 SPI 超长传输崩溃
- 页面切换更稳定
- UI 响应性提升

## 当前工作状态

在本轮迭代结束时，项目已经具备：

- 通过 `OTG` 在 Windows 上可工作的 `USB Audio Device` 播放
- 可工作的 `I2S -> MAX98357A` 输出路径
- 可工作的 `WS2812` 灯光控制
- 一个以产品方向为中心的 UI，核心围绕：
  - USB 音箱状态/控制
  - 灯光控制
- 屏幕上的本地音箱控制：
  - 降低音量
  - 提高音量
  - 静音切换
- 默认串口日志噪声减少
- 通过 SPI 分块 flush 获得更安全的 LCD 刷新行为

## 遇到的主要问题与解决方案

### 1. Windows 上的枚举失败

问题：

- `Code 10`
- `Code 43`
- 描述符请求失败

修复：

- 将描述符移入 DMA 安全内存
- 多轮调整描述符布局
- 通过 PID/串号变化强制重新枚举

### 2. 看门狗与调度故障

问题：

- task watchdog
- `xTaskDelayUntil` 断言

修复：

- 更安全的任务调度
- 至少 `1 tick` 的时序保障

### 3. 播放期间出现嘶声/噪声

问题：

- 音频包被不安全地读入过小的临时存储区

修复：

- 将摄取逻辑改为受限的单包分块读取

### 4. 流运行时崩溃

问题：

- 由于 USB 读取过大导致内存破坏

修复：

- 同样采用受限分块的 TinyUSB 摄取逻辑

### 5. 显示撕裂与页面切换卡顿

修复：

- 关闭标签页动画
- 降低 LVGL 任务休眠上限
- 谨慎增大 draw buffer
- 最终让 LCD flush 能将大更新拆成安全的 SPI 小块

### 6. SPI 过长传输导致显示崩溃

问题：

- 单次 flush 事务超过 SPI 硬件传输上限

修复：

- 对 `ILI9341` flush 实现做分块

## 本轮工作中涉及的重要文件

- `src/main.c`
- `components/app_services/app_music_service.c`
- `components/app_services/app_music_source.*`
- `components/app_services/app_music_playback_source.*`
- `components/app_services/app_music_usb_stream.*`
- `components/app_services/app_usb_audio_service.*`
- `components/app_services/app_usb_audio_descriptors.*`
- `components/app_ui/app_ui.c`
- `components/audio_output/audio_output.c`
- `components/display/display_ili9341.c`
- `components/bsp/include/board_config.h`
- `docs/current-status.md`
- `docs/usb-audio-roadmap.md`
- `docs/runbook.md`
- `README.md`

## 收获与经验

### USB 音频 bring-up 经验

- ESP32-S3 USB device 模式下的枚举问题，根因可能是 DMA/描述符放置位置，而不只是描述符语法
- 如果不有意识地通过 PID/串号变化处理，Windows 缓存会让 USB 音频 bring-up 更难调试
- 早期记录 USB 类控制流量非常有帮助，但后续必须收敛日志量

### 音频流水线经验

- 本地 `I2S` 路径可工作，并不意味着 USB 摄取路径天然安全
- 包大小假设非常关键
- 时序和缓冲错误，既可能表现为崩溃，也可能表现为音频噪声

### 显示路径经验

- 仅仅增大 draw buffer 还不够
- 显示驱动必须遵守硬件 SPI 事务大小上限
- 与其假设整块 LVGL 区域总能一次发完，不如采用分块 flush，更安全

## 建议的下一步

1. 在真机上验证新的 Speaker UI
2. 确认本地音量/静音控制的手感是否正确
3. 如果页面切换时仍可见撕裂，继续优化
4. 决定 `Tone` 和 `SD Audio` 是否只保留为隐藏调试路径
5. 记录 `44.1kHz` 与 `48kHz` 在主机侧行为上的最终差异
6. 继续产品化工作：
   - 更完善的 Speaker 页面视觉打磨
   - 更精致的 Lighting 页面布局
   - 更长时间的播放稳定性测试

## 简短总结

本轮迭代已成功将项目从：

- 以本地 tone/SD 音频 bring-up 为主，并计划未来支持 USB 的状态

推进为：

- 一个首版可工作的 USB 音箱设备，具备面向产品的 UI、本地音箱控制、更干净的日志，以及更安全的显示 flush 路径。
