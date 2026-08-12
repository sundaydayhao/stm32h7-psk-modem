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

## 2026-08-11 Session Checkpoint

User goal:

- Show/select all WAV modes from `C:\projects\psk-script\generated-wav`.
- Touch-select modes on the STM32H743 board.
- Decode BPSK, QPSK, 8PSK, FEC, and multi-carrier modes where possible.
- Use `C:\projects\psk-script\input.txt` as the expected decode text for offline regression.

Important current files:

- Mode registry: `app/modem/psk_modes.c/.h`
- Modem wrapper: `app/modem/psk_modem.c/.h`
- Decoder backend: `third_party/psk_rtty/psk_rtty.c`
- Decoder state/API: `third_party/psk_rtty/include/psk_rtty.h`
- Touch driver: `app/platform/touch_gt911.c/.h`
- UI layout and mode hit-test: `app/ui/display.c/.h`
- Offline regression: `tools/wav_decode_regression.py`

Touch hardware/status:

- GT911 works on the EmbedFire H743 Challenger V2 wiring:
  - I2C2
  - SCL `PH4`
  - SDA `PH5`
  - RST `PI8`
  - INT `PD13`
  - address `0x5D`
- Confirmed log earlier:
  - `[touch] gt911 addr=0x5d id=911`
- Correct coordinate mapping for the current right-side mode matrix:
  - `display_x = 520 + raw_y`
  - `display_y = raw_x`
- This fixed the case where tapping `BPSK63` selected `8PSK250`.

UI state:

- Right side is a 3-column x 9-row `MODE SELECT` matrix.
- RX text display scrolls/tails when the text buffer fills, so latest decoded text stays visible.
- Touch diagnostics are still present on screen/log in parts of the UI code.

Tone/frequency finding:

- Default decoder tone is now `1500 Hz`, matching generated WAV files.
- Do not automatically set decoder tone from coarse spectrum peak on mode touch.
  - Spectrum bins can show approximate peaks like `1456 Hz` or `1520 Hz`.
  - Feeding those directly to PSK decode caused BPSK63 garbage even though the WAV was 1500 Hz.
- Diagnostics now include current mode and tone:
  - `[diag] mode=<name> tone=<hz> ...`

Implemented decoder architecture:

- `psk_modes.c` now lists all 27 WAV modes and maps them to backend enum values.
- Modes are selectable from UI.
- `psk_rtty.h` backend enum expanded for all WAV modes.
- Existing pskcore path remains for BPSK31/BPSK63.
- A generic PSK symbol path was added for:
  - QPSK
  - non-FEC 8PSK
  - BPSK63F
  - 8PSK FEC modes
  - multi-carrier BPSK modes
- Generic path:
  - supports up to 12 carriers
  - uses differential phase symbols
  - precomputes carrier NCO rotation coefficients; no per-sample `cosf/sinf` in the hot loop
  - uses different symbol integration windows by mode family/carrier count
- Added MFSK varicode decode table for 8PSK and PSK-R style modes.

Viterbi/FEC checkpoint:

- Replaced earlier lockstep FEC shortcut with a complete fixed-memory trellis Viterbi decoder.
- Design is based on fldigi `viterbi.cxx` behavior:
  - path metrics
  - survivor history
  - periodic traceback
  - 8-bit chunk output
- MCU implementation differs from fldigi to fit STM32H743 RAM:
  - double metric buffers
  - packed survivor decisions
  - `VIT_MAX_TRACEBACK=64`
  - `VIT_CHUNKSIZE=8`
- K16 full fldigi-style `PATHMEM=256 x 32768 states x int history/metrics` would not fit comfortably in MCU RAM.
- Current build RAM:
  - `RAM_D1: 413616 B / 512 KB = 78.89%`
  - `ROM: 78476 B / 2 MB = 3.74%`
- FEC pair ordering bug found and fixed:
  - For interleaved 8PSK FEC, emulate fldigi `rx_pskr` ordering:
    - latest encoded bit first into deinterleaver
    - swap pair again before Viterbi
  - This was required for `8PSK500F` to pass offline regression.
- FEC soft-symbol support added:
  - `0` strong zero
  - `255` strong one
  - `128` puncture
  - branch metric follows fldigi-style soft metric (`mettab`) behavior.
- 8PSK FEC uses fldigi Gray softbit table:
  - `{25,0,25}`, `{0,25,230}`, ...
  - current code feeds the softbits in reverse order `[2], [1], [0]` to match bit-stream order.

Offline regression command:

```powershell
python C:\projects\stm32h7-psk-modem\tools\wav_decode_regression.py
```

Useful targeted commands:

```powershell
python C:\projects\stm32h7-psk-modem\tools\wav_decode_regression.py --mode BPSK63F --mode QPSK31 --mode QPSK63 --mode QPSK125 --mode QPSK250 --mode QPSK500
python C:\projects\stm32h7-psk-modem\tools\wav_decode_regression.py --mode 8PSK125 --mode 8PSK250 --mode 8PSK500 --mode 8PSK1000
python C:\projects\stm32h7-psk-modem\tools\wav_decode_regression.py --mode 8PSK500F --min-fragment 40
```

Offline regression results at checkpoint:

- Passing:
  - `BPSK31`
  - `BPSK63`
  - `BPSK63F`
  - `BPSK125`
  - `BPSK250`
  - `BPSK500`
  - `BPSK1000`
  - `QPSK31`
  - `QPSK63`
  - `QPSK125`
  - `QPSK250`
  - `QPSK500`
  - `8PSK125`
  - `8PSK250`
  - `8PSK500`
  - `8PSK1000`
  - `8PSK500F`
- Not fully resolved:
  - `8PSK125F`
  - `8PSK250F`
  - `8PSK1000F`
  - `8PSK1200F`
  - multi-carrier BPSK (`12xPSK125`, `6xPSK250`, `2xPSK500`, `4xPSK500`, `2xPSK800`, `2xPSK1000`) still needs stronger carrier separation/filtering.

Important debug conclusions:

- BPSK bit polarity:
  - no phase change = bit `1`
  - 180-degree phase change = bit `0`
  - This was fixed in the simple BPSK path.
- QPSK/8PSK phase direction:
  - demodulated complex phase direction is conjugated relative to the generator.
  - BPSK hides this because 0/180 is symmetric.
  - QPSK/8PSK maps must use the reversed-angle tables.
- `8PSK500F` passing proves:
  - puncture handling
  - interleave/deinterleave ordering
  - Viterbi traceback
  - MFSK varicode path
  are working for at least one FEC 8PSK mode.
- `8PSK125F/250F` direct symbol-stream experiments passed when bypassing WAV demod, but WAV regression still fails.
  - This suggests the remaining issue is front-end phase/soft decision quality or symbol timing for low-speed K16 8PSK FEC, not the FEC chain itself.
- Python regression for K16 modes is slow because it vectorizes 32768-state Viterbi repeatedly. Use targeted mode tests.

Build/flash state:

- Last successful build:

```powershell
cmake --build --preset debug
```

- Last successful flash:

```powershell
C:\Users\hazh\AppData\Roaming\Python\Python312\Scripts\pyocd.exe flash --target stm32h743xx --probe 4c2261d77b0dad97 --frequency 1000000 -O connect_mode=under-reset --erase auto C:\projects\stm32h7-psk-modem\build\debug\stm32h743iit6_psk_modem.elf
C:\Users\hazh\AppData\Roaming\Python\Python312\Scripts\pyocd.exe commander --target stm32h743xx --probe 4c2261d77b0dad97 --frequency 1000000 -O connect_mode=under-reset -c reset -c go -c exit
```

Serial log note:

- `C:\projects\stm32h7-psk-modem\log.txt` was 0 bytes at the end of the session.
- COM10 exists, but no active capture was writing to `log.txt`.
- If logs are needed next time, start/verify serial capture before testing.

Next recommended work:

1. Improve generic front-end for multi-carrier BPSK:
   - add per-carrier matched/bandpass filtering or a least-squares/Goertzel-style projection instead of simple carrier dot product over overlapping tones.
2. Improve 8PSK FEC low-speed soft decisions:
   - add phase-quality based soft error scaling like fldigi
   - tune integration window for `8PSK125F/250F`
   - consider symbol timing recovery rather than fixed symbol windows.
3. Re-run targeted WAV regression after each front-end change.
4. After regression improves, reduce touch/debug log noise.
