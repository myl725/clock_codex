# clock_codex Runbook

## Purpose

This runbook captures how to build, package, upload, and debug the current project state on this machine.
It exists because the current toolchain still has a few known workarounds.

## Normal Build Entry

Project root:

```powershell
cd C:\Users\PC\Desktop\clock_codex
```

Standard build command:

```powershell
pio run
```

## Current Reality

At the current milestone, `pio run` may still be blocked by a wrong PlatformIO size check even when
the source code itself compiles and links successfully.

Known symptoms:

- app size reported against `1MB`
- `checkprogsize` fails with a maximum allowed value that does not match the custom partition table

## Standard Verification Flow

Use this order when checking the current speech demo:

1. run `pio run`
2. if the build reaches `firmware.elf`, verify whether the failure is only the size gate
3. package `esp-sr` models if needed
4. generate `firmware.bin` manually if needed
5. upload through the custom flash-args uploader
6. verify on hardware

## Manual Model Packaging

`esp-sr` model packing currently fails if the build system tries to call bare `python`.
Use the PlatformIO Python interpreter directly:

```powershell
& 'C:\Users\PC\.platformio\penv\Scripts\python.exe' `
  'C:\Users\PC\Desktop\clock_codex\managed_components\espressif__esp-sr\model\movemodel.py' `
  -d1 'C:\Users\PC\Desktop\clock_codex\sdkconfig.esp32-s3-devkitm-1' `
  -d2 'C:\Users\PC\Desktop\clock_codex\managed_components\espressif__esp-sr' `
  -d3 'C:\Users\PC\Desktop\clock_codex\.pio\build\esp32-s3-devkitm-1'
```

Expected artifact:

- `.pio/build/esp32-s3-devkitm-1/srmodels/srmodels.bin`

## Manual Firmware Image Generation

If `firmware.elf` exists but PlatformIO blocks before emitting the final app image, generate it manually:

```powershell
& 'C:\Users\PC\.platformio\penv\Scripts\python.exe' `
  'C:\Users\PC\.platformio\packages\tool-esptoolpy\esptool.py' `
  --chip esp32s3 elf2image --flash_mode dio --flash_freq 80m --flash_size 16MB `
  -o 'C:\Users\PC\Desktop\clock_codex\.pio\build\esp32-s3-devkitm-1\firmware.bin' `
  'C:\Users\PC\Desktop\clock_codex\.pio\build\esp32-s3-devkitm-1\firmware.elf'
```

## Upload Flow

The project uses a custom upload path based on `flash_args`.
Relevant files:

- `tools/configure_upload.py`
- `tools/upload_via_flash_args.py`

Standard upload command:

```powershell
pio run -t upload
```

If manual upload is needed:

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

Note:

- `COM5` is only an example based on a previous successful session
- always check the real board port before uploading

## Serial Port Check

Use this command to list serial ports on Windows:

```powershell
Get-CimInstance Win32_SerialPort | Select-Object DeviceID,Name,Description | Format-Table -AutoSize
```

If the expected ESP32 port does not appear:

- reconnect the board
- close any serial monitor that may be holding the port
- verify USB cable and USB-UART visibility

## Current Important Build Files

- `platformio.ini`
- `sdkconfig.defaults`
- `partitions.csv`
- `sdkconfig.esp32-s3-devkitm-1`
- `dependencies.lock`

## Current Important Output Files

- `.pio/build/esp32-s3-devkitm-1/firmware.elf`
- `.pio/build/esp32-s3-devkitm-1/firmware.bin`
- `.pio/build/esp32-s3-devkitm-1/bootloader.bin`
- `.pio/build/esp32-s3-devkitm-1/partitions.bin`
- `.pio/build/esp32-s3-devkitm-1/srmodels/srmodels.bin`

## Troubleshooting Notes

### 1. Wrong app size limit

Symptom:

- `maximum allowed (1048576 bytes)`

Meaning:

- PlatformIO board-side size validation is still out of sync with the custom partition layout

Current workaround:

- continue with manual image generation and manual upload flow

### 2. `Python was not found`

Symptom:

- model packaging script fails through bare `python`

Current workaround:

- call the script using `C:\Users\PC\.platformio\penv\Scripts\python.exe`

### 3. Upload port missing or busy

Symptom:

- `Could not open COMx`
- board not listed

Current workaround:

- identify the real serial port first
- close monitor tools
- retry upload

## Next Resume Point

If resuming from a new session, after reading this runbook:

1. read `docs/current-status.md`
2. confirm board serial port
3. confirm whether the goal is:
   - offline `esp-sr` demo
   - or networked speech transport
