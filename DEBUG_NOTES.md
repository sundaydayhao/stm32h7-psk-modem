# STM32H743 PSK Modem Debug Notes

## Hardware

- Board: EmbedFire STM32H743 Challenger V2
- Core MCU: STM32H743
- LCD: 7 inch RGB panel, 800x480, 24-bit RGB interface
- Audio codec/input: WM8978 over I2C1 control and SPI2/I2S2 RX DMA
- Touch: GT911-compatible capacitive controller on the LCD I2C bus

## Working State

- LCD is working full-screen.
- Waterfall and spectrum are displayed.
- BPSK31 audio input is decoded and RX text is displayed.
- Touch mode selection is polled through GT911 and uses the on-screen WAV mode matrix.
- RX text display preserves lowercase letters.
- UART diagnostics are enabled at 115200 baud.

## LCD Configuration That Works

File: `app/board/fire_lcd.c`

- Backlight/enable:
  - `PD7`: BL
  - `PD4`: DISP
- Framebuffer:
  - `0xD0000000`
- Pixel format:
  - `LTDC_PIXEL_FORMAT_RGB888`
  - 24-bit framebuffer writes, 3 bytes per pixel
- Resolution:
  - `800x480`
- Timing:
  - `HBP=46`
  - `HFP=210`
  - `HSW=1`
  - `VBP=23`
  - `VFP=22`
  - `VSW=1`
- Pixel clock:
  - `PLL3M=25`
  - `PLL3N=330`
  - `PLL3R=10`
  - about 33 MHz

Important: do not change the LCD PLL3 settings unless retesting the RGB panel.

## SDRAM/FMC Configuration Note

The LCD framebuffer is in external SDRAM at `0xD0000000`.

FMC/SDRAM uses PLL2 in `app/board/fire_lcd.c`. Do not reconfigure PLL2 after SDRAM/LCD init. Reconfiguring PLL2 caused the LCD to turn into a TV-noise-style corrupted display while the MCU and decoder were still running.

## Audio Input Configuration

File: `app/platform/audio_input.c`

- WM8978 control: I2C1
  - SCL: PB6
  - SDA: PB7
  - address: `0x34`
- I2S2 receive:
  - LRC/WS: PB12
  - BCLK/CK: PD3
  - ADCDAT: PC2
  - DACDAT: PI3
  - MCLK: PC6
- DMA:
  - `DMA1_Stream0`
  - request: `DMA_REQUEST_SPI2_RX`
  - circular mode
- IRQ:
  - `DMA1_Stream0_IRQHandler()` in `Core/Src/stm32h7xx_it.c`
  - calls `audio_input_dma_irq_handler()`
- Input paths currently enabled:
  - `MIC_LEFT_ON | MIC_RIGHT_ON | LINE_ON | ADC_ON`
- Decode sample path:
  - I2S codec rate is defined by `AUDIO_INPUT_CODEC_RATE_HZ`
  - decimated by `AUDIO_INPUT_DECIMATION`
  - decoder receives `AUDIO_INPUT_DECODE_RATE_HZ`

Important: audio SPI2/I2S2 clock was changed to use existing PLL1 source. Do not use PLL2 for audio because PLL2 is used by SDRAM/FMC.

## UI Notes

File: `app/ui/display.c`

- Header: `STM32H743 PSK MODEM`
- Removed older duplicate title and UART line.
- Main sections:
  - left status row: active mode, baud, tone, peak, input status
  - left middle: spectrum/waterfall
  - left bottom: RX TEXT
  - right full-height: large touch targets for all WAV modes
- Input status appears as:
  - `IN <avg> <L/R> Q<queued>`
- `IN` should rise when real audio is entering WM8978.
- The right-side `MODE SELECT` matrix is touch selectable.
  - Green modes are currently backed by the real BPSK decoder.
  - Gray modes are selectable/displayed, but QPSK/8PSK/FEC/multi-carrier decoders are still pending.

File: `app/platform/touch_gt911.c`

- GT911 is polled over I2C1 after audio initialization.
- The driver probes both common GT911 addresses: `0x5D` and `0x14`.
- Default coordinate mapping is `800x480` direct orientation.
- If the panel reports rotated/mirrored coordinates, adjust:
  - `TOUCH_GT911_SWAP_XY`
  - `TOUCH_GT911_INVERT_X`
  - `TOUCH_GT911_INVERT_Y`

File: `app/board/fire_lcd.c`

- The original simple bitmap font converted lowercase to uppercase.
- Lowercase 5x7 glyphs were added so RX TEXT preserves decoded case.

## Debug History / Pitfalls

- Initial LCD was dark:
  - fixed with correct BL/DISP pins and LCD init.
- LCD showed only right half:
  - fixed with 800x480 RGB888 framebuffer/timing/pixel clock.
- LCD showed full screen but no waterfall/decoded output:
  - original `audio_input_read()` returned 0 forever.
- A synthetic internal 1526 Hz tone was temporarily added for waterfall testing.
  - It was removed when real WM8978/I2S input was implemented.
- Screen became TV-noise/corrupted after audio input was added:
  - cause: audio init reconfigured PLL2, breaking SDRAM/FMC framebuffer timing.
  - fix: audio SPI2 clock uses PLL1, SDRAM stays on PLL2.
- Decoded text looked uppercase on screen:
  - cause: LCD font mapped `a-z` to `A-Z`.
  - fix: added lowercase glyphs.

## Useful Build / Flash Commands

Build:

```powershell
cmake --build --preset debug
```

Flash:

```powershell
C:\Users\hazh\AppData\Roaming\Python\Python312\Scripts\pyocd.exe flash --target stm32h743xx --probe 4c2261d77b0dad97 --frequency 1000000 -O connect_mode=under-reset --erase auto C:\projects\stm32h7-psk-modem\build\debug\stm32h743iit6_psk_modem.elf
```

If SWD connection is unreliable, keep `-O connect_mode=under-reset`.

## Current Known Good Result

- LCD display is stable.
- Waterfall shows the incoming BPSK31 signal.
- Decoder produces characters.
- RX TEXT displays decoded text with original case.
