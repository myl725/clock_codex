# INMP441 Minimal Test

This is the smallest useful hardware validation for `INMP441 -> ESP32-S3 -> serial log`.

It does not use:

- LVGL
- display
- touch
- `esp-sr`
- wake word

It only checks whether raw microphone samples are present.

## Wiring

Use only these connections:

- `INMP441 VDD -> 3.3V`
- `INMP441 GND -> GND`
- `INMP441 SCK/BCLK -> GPIO5`
- `INMP441 WS/LRCL -> GPIO6`
- `INMP441 SD/DOUT -> GPIO4`
- `INMP441 L/R -> GND`

If this version still reads zero, try one more time with:

- `INMP441 L/R -> 3.3V`

Do not connect:

- display
- touch
- MAX98357
- any other device on the same I2S lines

## How To Use

Temporarily replace [src/main.c](/C:/Users/PC/Desktop/clock_codex/src/main.c) with the standalone program from:

- [inmp441_minimal_main.c](/C:/Users/PC/Desktop/clock_codex/docs/inmp441_minimal_main.c)

Then build and upload:

```powershell
pio run
pio run -t upload
pio device monitor --port COM3 --baud 115200
```

If your board is not on `COM3`, replace the port with the correct one.

## Expected Output

When the microphone path is working, you should see logs like:

```text
raw_peak=18432 sample0=512 sample1=-384 sample2=640
raw_peak=22176 sample0=448 sample1=-576 sample2=704
```

If the output stays like this:

```text
raw_peak=0 sample0=0 sample1=0 sample2=0
```

then the raw I2S microphone data path is still not working, and the problem is almost certainly hardware-side:

- `SD` not really reaching `GPIO4`
- `BCLK` / `WS` not connected
- `L/R` slot mismatch
- microphone module fault
- another device still loading the same lines

