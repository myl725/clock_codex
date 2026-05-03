# clock_codex 运行手册

## 目的

本文记录了如何在当前这台机器上构建、打包、上传并调试当前项目状态。
之所以需要这份文档，是因为当前工具链仍然有一些已知绕过方案。

## 常规构建入口

项目根目录：

```powershell
cd C:\Users\PC\Desktop\clock_codex
```

标准构建命令：

```powershell
pio run
```

## 标准验证流程

在检查当前灯光 / 音频控制应用时，按以下顺序进行：

1. 运行 `pio run`
2. 如果构建成功，运行 `pio run -t upload`
3. 如果上传被阻塞，改用基于 flash-args 的自定义上传器
4. 在真机上验证：
   - 显示能够初始化
   - 触摸可用
   - `Lighting` 标签页能控制 WS2812
   - `Audio` 标签页能在 `Tone`、`SD Audio` 和 `USB Audio` 之间切换
   - `Tone` 能本地播放
   - `SD Audio` 能列出并播放已知良好的文件
5. 如果当前目标是 USB 音频 bring-up：
   - 保持 `TTL` 端口连接，用于日志和刷机
   - 用数据线将 `OTG` 端口连接到主机 PC
   - 观察串口日志中是否出现 `tinyusb attached`
   - 检查主机是否识别到 `Clock Codex USB Speaker`
   - 当主机打开音频流时，观察是否出现 `set interface ...` 和 `usb stream started ...`
   - 确认 Windows 播放的音频能通过喇叭听到

## 手动生成固件镜像

如果 `firmware.elf` 已存在，但需要手动生成 `firmware.bin`，可使用：

```powershell
& 'C:\Users\PC\.platformio\penv\Scripts\python.exe' `
  'C:\Users\PC\.platformio\packages\tool-esptoolpy\esptool.py' `
  --chip esp32s3 elf2image --flash_mode dio --flash_freq 80m --flash_size 16MB `
  -o 'C:\Users\PC\Desktop\clock_codex\.pio\build\esp32-s3-devkitm-1\firmware.bin' `
  'C:\Users\PC\Desktop\clock_codex\.pio\build\esp32-s3-devkitm-1\firmware.elf'
```

## 上传流程

项目当前使用一条基于 `flash_args` 的自定义上传路径。
相关文件：

- `tools/configure_upload.py`
- `tools/upload_via_flash_args.py`

标准上传命令：

```powershell
pio run -t upload
```

如果需要手动上传：

```powershell
& 'C:\Users\PC\.platformio\penv\Scripts\python.exe' `
  'C:\Users\PC\Desktop\clock_codex\tools\upload_via_flash_args.py' `
  --python 'C:\Users\PC\.platformio\penv\Scripts\python.exe' `
  --esptool 'C:\Users\PC\.platformio\packages\tool-esptoolpy\esptool.py' `
  --build-dir 'C:\Users\PC\Desktop\clock_codex\.pio\build\esp32-s3-devkitm-1' `
  --port 'COM5' `
  --baud '460800' `
  --chip esp32s3
```

说明：

- `COM5` 只是基于之前成功会话的示例
- 上传前一定要先确认开发板真实串口号

## 串口检查

使用以下命令列出 Windows 上的串口：

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name,Description | Format-Table -AutoSize
```

如果预期的 ESP32 端口没有出现：

- 重新连接开发板
- 关闭可能占用端口的串口监视工具
- 检查 USB 线与 USB-UART 是否正常可见

如果你需要在 PowerShell 和 PlatformIO 中快速查看串口日志，可以使用：

```powershell
pio device monitor -p COM3 -b 115200
```

如果你的板子不在 `COM3`，请改成正确端口。

## 当前重要构建文件

- `platformio.ini`
- `sdkconfig.defaults`
- `partitions.csv`
- `sdkconfig.esp32-s3-devkitm-1`
- `dependencies.lock`

## 当前重要输出文件

- `.pio/build/esp32-s3-devkitm-1/firmware.elf`
- `.pio/build/esp32-s3-devkitm-1/firmware.bin`
- `.pio/build/esp32-s3-devkitm-1/bootloader.bin`
- `.pio/build/esp32-s3-devkitm-1/partitions.bin`

## 故障排查说明

### 1. 上传串口缺失或被占用

现象：

- `Could not open COMx`
- 或者开发板端口未列出

当前绕过方式：

- 先识别真实串口
- 关闭监视工具
- 重试上传

### 2. SD 音频文件缺失

现象：

- `SD Audio` 模式下没有可播放文件
- 播放时报文件打开失败

当前检查项：

- 确认 SD 卡已连接到专用 SD SPI 引脚
- 确认 SD 卡能成功挂载
- 在 SD 根目录放入一个已知良好的 `.wav` 或 `.mp3` 文件
- 如果已配置 Wi-Fi 凭据，服务层可能会在需要时下载默认 WAV 测试文件

### 3. 某些 MP3 文件很早失败

现象：

- 文件能出现在列表里，但在打开或 priming 阶段就停止

含义：

- 当前轻量 MP3 路径还无法兼容所有 MP3 编码变体

当前绕过方式：

- 先用已知良好的文件验证播放链路
- 将失败文件与可工作的样本（如 `TEST3.MP3`）进行对比

### 4. USB 音箱无法枚举或无法开始流播放

现象：

- 主机看不到 USB 音箱设备
- 或串口日志中始终没有 `tinyusb attached`
- 或者主机能识别设备，但日志始终到不了 `usb stream started ...`

当前检查项：

- 确保主机连接的是开发板的 `OTG` 端口，而不是只有 `TTL` 端口
- 在 bring-up 阶段保持 `TTL` 端口连接以查看日志
- 确认固件启动日志包含 `tinyusb audio driver installed`
- 如果主机打开了流，确认日志中出现 `set interface itf=... alt=1`
- 如果流已经开始但音频仍失败，检查日志中的周期性 USB 包统计和环形缓冲区计数器

### 5. 主机开始播放后 USB 音频断续、噪声或崩溃

现象：

- 设备成功枚举
- 主机打开了音频流
- 但音频损坏，或开发板崩溃

当前已知根因与修复：

- 不要尝试把 TinyUSB 中持续数毫秒的一大段音频突发，直接读进只够容纳单个 USB 包的栈缓冲区
- 当前主线会按受限的单包大小分块排空 TinyUSB 数据，再送入服务层自有的 USB PCM 环形缓冲区
- 如果这一行为发生回归，优先检查 `components/app_services/app_usb_audio_service.c`

## 下次恢复的起点

如果是在新的会话中恢复，阅读完这份手册后：

1. 阅读 `docs/current-status.md`
2. 确认开发板串口
3. 确认本次目标是：
   - 保持显示 / 触摸稳定
   - 验证 WS2812 行为
   - 改进 SD 音频与 MP3 兼容性
   - 验证 USB 音箱枚举与首次音频播放
