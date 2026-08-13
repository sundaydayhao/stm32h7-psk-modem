# STM32H7 PSK Modem Session Handoff

Date: 2026-08-11

## Goal

Create a PSK demodulator project in:

```text
C:\projects\stm32h7-psk-modem
```

Reference project:

```text
C:\projects\stm32f7-psk-modem
```

Target MCU:

```text
STM32H743IIT6
```

## Current Result

The STM32H7 project has been generated and builds successfully with STM32CubeCLT 1.22.0.

Output ELF:

```text
C:\projects\stm32h7-psk-modem\build\debug\stm32h743iit6_psk_modem.elf
```

The firmware has already been flashed successfully once using Fire-DAP-HIGH over CMSIS-DAP/SWD.

## Important Project Notes

The original F7 project is for STM32F769I-DISCO and includes board-specific WM8994 audio codec, DSI LCD, LTDC, SDRAM, and LVGL display code.

The H743IIT6 target is an MCU package, not a board definition, so the H7 project intentionally does not carry over F769I-DISCO board assumptions.

Kept from the F7 project:

- `app/dsp/spectrum.c`
- `app/modem/psk_modem.c`
- `third_party/psk_rtty`
- general CMake/naked-metal style

Changed/replaced:

- STM32F7 HAL/CMSIS replaced by STM32H7 HAL/CMSIS from:

```text
C:\Users\hazh\STM32Cube\Repository\STM32Cube_FW_H7_V1.13.0
```

- Startup file:

```text
startup_stm32h743xx.s
```

- Linker script:

```text
STM32H743IITX_FLASH.ld
```

- Main firmware entry:

```text
Core\Src\main.c
```

- Audio backend is currently a stub:

```text
app\platform\audio_input.c
```

- Display backend is currently UART/headless:

```text
app\ui\display.c
```

## Toolchain

STM32CubeCLT is installed at:

```text
C:\ProgramData\STM32CubeCLT_1.22.0
```

The CMake toolchain file auto-detects CubeCLT GCC from this location:

```text
C:\projects\stm32h7-psk-modem\cmake\gcc-arm-none-eabi.cmake
```

Preset was updated to use CubeCLT Ninja:

```text
C:\projects\stm32h7-psk-modem\CMakePresets.json
```

## Build Commands

From:

```powershell
cd C:\projects\stm32h7-psk-modem
```

Configure:

```powershell
cmake --fresh --preset gcc-arm-debug
```

Build:

```powershell
cmake --build --preset debug
```

Build was verified successfully. Last known memory usage:

```text
ITCMRAM: 0 B / 64 KB
DTCMRAM: 0 B / 128 KB
RAM_D1: 5704 B / 512 KB
RAM_D2: 0 B / 288 KB
RAM_D3: 0 B / 64 KB
ROM: 28768 B / 2 MB
```

## Flashing

The user uses:

```text
Fire-DAP-HIGH
```

This probe enumerates as CMSIS-DAP/DAPLink:

```text
Embedfire fireDAP CMSIS-DAP
Unique ID: 4c2261d77b0dad97
USB VID/PID observed: VID_1FC9&PID_5601
```

It also exposes `COM10`, but `COM10` is the probe virtual serial port, not the STM32 ROM Bootloader UART. Attempts to flash with `STM32_Programmer_CLI -c port=COM10` failed with Bootloader ACK timeout.

Working flashing method uses pyOCD over CMSIS-DAP/SWD.

pyOCD was installed with:

```powershell
python -m pip install --user pyocd
```

Installed executable:

```text
C:\Users\hazh\AppData\Roaming\Python\Python312\Scripts\pyocd.exe
```

List probes:

```powershell
C:\Users\hazh\AppData\Roaming\Python\Python312\Scripts\pyocd.exe list
```

Flash command:

```powershell
C:\Users\hazh\AppData\Roaming\Python\Python312\Scripts\pyocd.exe flash --target stm32h743xx --probe 4c2261d77b0dad97 --frequency 4000000 --erase auto C:\projects\stm32h7-psk-modem\build\debug\stm32h743iit6_psk_modem.elf
```

Reset/run command:

```powershell
C:\Users\hazh\AppData\Roaming\Python\Python312\Scripts\pyocd.exe commander --target stm32h743xx --probe 4c2261d77b0dad97 --frequency 4000000 -c reset -c go -c exit
```

Successful flash output included:

```text
Erased 131072 bytes (1 sector), programmed 29696 bytes (29 pages)
Successfully resumed device
```

## Current Firmware Behavior

The current firmware:

- boots STM32H743 from HSE configuration in `Core\Src\main.c`
- configures USART1 on PA9/PA10 at 115200 baud
- initializes PSK modem state
- prints diagnostics over UART/headless display backend
- does not yet receive real audio samples

UART pins currently assumed:

```text
USART1_TX = PA9
USART1_RX = PA10
Baud = 115200
```

## Known Limitations

`app\platform\audio_input.c` is a stub. It returns no samples.

The next meaningful engineering task is to adapt the audio input path to the actual H743IIT6 board schematic. It must provide signed 16-bit mono samples at:

```text
AUDIO_INPUT_DECODE_RATE_HZ = 8192
AUDIO_INPUT_DECODE_BLOCK_SAMPLES = 128
```

Possible implementations:

- ADC + timer + DMA, then scale to signed 16-bit
- I2S/SAI external audio codec + DMA, with decimation if needed
- USB audio or other digital sample source

`app\ui\display.c` is also a stub. Replace it only if the board has a real display.

## Files Most Likely To Edit Next

```text
Core\Src\main.c
app\platform\audio_input.c
app\platform\audio_input.h
app\ui\display.c
CMakeLists.txt
STM32H743IITX_FLASH.ld
```

## Caution

Do not use this command for Fire-DAP-HIGH flashing:

```powershell
STM32_Programmer_CLI.exe -c port=COM10 ...
```

That opens the DAP virtual serial port, not SWD. Use pyOCD or another CMSIS-DAP/OpenOCD-compatible SWD flow.
