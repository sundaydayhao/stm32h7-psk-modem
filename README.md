# STM32H743IIT6 PSK Modem

Bare-metal CMake project for an STM32H743IIT6-based PSK demodulator.

This project is derived from `C:\projects\stm32f7-psk-modem`, but the F769I-DISCO board-specific DSI LCD and WM8994 audio codec paths are replaced with an EmbedFire STM32H743 Challenger-style LTDC LCD backend. The DSP and PSK decoder layers are kept intact:

- `app/dsp/spectrum.c`
- `app/modem/psk_modem.c`
- `third_party/psk_rtty`

The default firmware boots the STM32H743 at 400 MHz from a 25 MHz HSE, prints diagnostics on USART1 PA9/PA10 at 115200 baud, and renders modem status on an 800x480 RGB LCD through LTDC using external SDRAM as the frame buffer. `app/platform/audio_input.c` is the board adaptation point for real ADC, SAI/I2S codec, or USB audio sample input. It must deliver 8192 Hz signed 16-bit mono blocks of `AUDIO_INPUT_DECODE_BLOCK_SAMPLES` samples to `audio_input_read()`.

## Build

Install STM32CubeCLT first. The CMake toolchain file looks for:

```text
C:\ST\STM32CubeCLT_*\GNU-tools-for-STM32\bin\arm-none-eabi-gcc.exe
```

It also accepts `arm-none-eabi-gcc` from `PATH`.

```powershell
cd C:\projects\stm32h7-psk-modem
cmake --preset gcc-arm-debug
cmake --build --preset debug
```

## Flash

```powershell
C:\ProgramData\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe -c port=SWD -w build\debug\stm32h743iit6_psk_modem.elf -v -rst
```

## Hardware Adaptation

Update these files for your H743IIT6 board schematic:

- `Core/Src/main.c`: clock source, UART pins, MPU regions.
- `app/board/fire_lcd.c`: EmbedFire H743 LTDC, SDRAM, LCD GPIO, and framebuffer backend.
- `app/platform/audio_input.c`: ADC/SAI/I2S/codec DMA input and optional decimation to 8192 Hz.
- `app/ui/display.c`: PSK modem screen layout rendered to the LCD backend.
