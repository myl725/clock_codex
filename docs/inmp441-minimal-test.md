# INMP441 最小测试

这是用于验证 `INMP441 -> ESP32-S3 -> 串口日志` 这条链路是否通的最小有用硬件测试。

它不会使用：

- LVGL
- 显示
- 触摸
- `esp-sr`
- 唤醒词

它只检查原始麦克风采样是否存在。

## 接线

只连接以下线路：

- `INMP441 VDD -> 3.3V`
- `INMP441 GND -> GND`
- `INMP441 SCK/BCLK -> GPIO5`
- `INMP441 WS/LRCL -> GPIO6`
- `INMP441 SD/DOUT -> GPIO4`
- `INMP441 L/R -> GND`

如果这种接法读出来仍然全是 0，再试一次：

- `INMP441 L/R -> 3.3V`

不要连接：

- 显示
- 触摸
- MAX98357
- 任何其他接在同一组 I2S 线上设备

## 使用方法

临时将 [src/main.c](/C:/Users/PC/Desktop/clock_codex/src/main.c) 替换为下列独立程序：

- [inmp441_minimal_main.c](/C:/Users/PC/Desktop/clock_codex/docs/inmp441_minimal_main.c)

然后构建并上传：

```powershell
pio run
pio run -t upload
pio device monitor --port COM3 --baud 115200
```

如果你的开发板不在 `COM3`，请替换为正确端口。

## 预期输出

当麦克风路径正常工作时，你应看到类似以下日志：

```text
raw_peak=18432 sample0=512 sample1=-384 sample2=640
raw_peak=22176 sample0=448 sample1=-576 sample2=704
```

如果输出始终像这样：

```text
raw_peak=0 sample0=0 sample1=0 sample2=0
```

那么原始 I2S 麦克风数据路径仍然没有工作，问题几乎可以确定在硬件侧：

- `SD` 没有真正连到 `GPIO4`
- `BCLK` / `WS` 没有接通
- `L/R` 声道槽位不匹配
- 麦克风模块本身故障
- 同一组线路上仍有其他设备在加载干扰
