# STM32H743 PSK Modem Debug Notes

Last updated: 2026-08-12

## Current Board State

- Board: EmbedFire STM32H743 Challenger V2.
- MCU: STM32H743.
- LCD: 800x480 RGB888 framebuffer in SDRAM at `0xD0000000`.
- Touch: GT911-compatible controller, currently working.
- Audio codec: WM8978, controlled over I2C1, sampled by SPI2/I2S2 RX DMA.
- Serial control/log port used in this session: `COM11` at `115200`.
- pyOCD probe used in this session: `4c2261d77b0dad97`.

Normal interactive UI is restored:

- Touch mode selection is enabled by default.
- Screen RX TEXT display is enabled by default.
- `TEST 1` is only an optional board-regression mode and should not be left enabled for manual use.
- Board was reset after the latest board tests, so runtime defaults are back to normal UI mode.

## Useful Commands

Build firmware:

```powershell
cmake --build --preset debug
```

Build host tools:

```powershell
.\tools\build_host_tools.ps1
```

Flash firmware:

```powershell
C:\Users\hazh\AppData\Roaming\Python\Python312\Scripts\pyocd.exe flash --target stm32h743xx --probe 4c2261d77b0dad97 --frequency 1000000 -O connect_mode=under-reset --erase auto C:\projects\stm32h7-psk-modem\build\debug\stm32h743iit6_psk_modem.elf
C:\Users\hazh\AppData\Roaming\Python\Python312\Scripts\pyocd.exe commander --target stm32h743xx --probe 4c2261d77b0dad97 --frequency 1000000 -O connect_mode=under-reset -c reset -c go -c exit
```

Board regression, reliable HEX log format:

```powershell
.\tools\board_wav_regression.ps1 -Port COM11 -MatchLength 40 -ModeWaitSeconds 15 -OutputDevice 7 -OutputGain 0.6 -RxLogFormat HEX
```

Board regression with display/touch disabled for less UI load:

```powershell
.\tools\board_wav_regression.ps1 -Port COM11 -MatchLength 40 -ModeWaitSeconds 15 -OutputDevice 7 -OutputGain 0.6 -RxLogFormat HEX -SerialTestMode $true
```

Host regression:

```powershell
.\tools\run_host_regression.ps1 -WavDir C:\projects\psk-script\generated-wav-48000
```

## Audio Architecture

Current design:

- Generated WAV stimulus: 48000 Hz.
- HAL I2S requested codec rate: `AUDIO_INPUT_CODEC_RATE_HZ = 48000`.
- Shared frontend decimation: `AUDIO_INPUT_DECIMATION = 3`.
- Nominal decoder rate: 16000 Hz.
- Actual board decoder rate currently set to `AUDIO_INPUT_ACTUAL_DECODE_RATE_HZ`.

Important clock finding:

- SPI2/I2S2 is currently sourced from PLL1Q = 200 MHz.
- With MCLK enabled, STM32H7 I2S uses an integer divider.
- A 48 kHz HAL request is realized close to:
  - `200 MHz / (256 * 16) = 48828.125 Hz`
- Post-decimation actual rate is therefore close to:
  - `48828.125 / 3 = 16276.0417 Hz`
- Firmware now tells the PSK decoder this actual post-decimation rate via:
  - `AUDIO_INPUT_ACTUAL_CODEC_RATE_HZ`
  - `AUDIO_INPUT_ACTUAL_DECODE_RATE_HZ`

Do not switch audio SPI123 clock to PLL2 casually:

- PLL2 is used by SDRAM/FMC/LCD support in `app/board/fire_lcd.c`.
- A trial switch of SPI123 to PLL2 caused startup/runtime problems.
- Earlier PLL2 changes also corrupted the framebuffer/LCD.

## Audio Input Path

File: `app/platform/audio_input.c`

WM8978/I2S pins:

- I2C1 SCL: PB6.
- I2C1 SDA: PB7.
- WM8978 I2C address: `0x34`.
- I2S2 LRC/WS: PB12.
- I2S2 BCLK/CK: PD3.
- I2S2 ADCDAT: PC2.
- I2S2 DACDAT: PI3.
- I2S2 MCLK: PC6.

DMA:

- `DMA1_Stream0`.
- request: `DMA_REQUEST_SPI2_RX`.
- circular RX DMA.

Shared frontend:

- `app/platform/audio_decim.c/.h`.
- 7-tap FIR plus decimate-by-3.
- DC blocker.
- Used by both firmware audio path and host WAV decoder.

Board diagnostics:

- `in=<avg>` rises during playback.
- idle input was around 110-140 in these tests.
- active playback was typically several thousand.
- `q=<queued>` should stay low; `q=0` is preferred.

An input gate was added:

- `RX_INPUT_OPEN_ABS_AVG = 500`.
- Below this level, samples are not fed into the decoder.
- This prevents idle line noise from producing continuous bogus decoded text.

## Decoder Architecture

Current receiver architecture:

- All PSK-family modes use the same backend path through `psk_channel_rx_feed()`.
- RTTY is intentionally not part of this work.
- The old split BPSK/simple/generic decode paths were removed or bypassed.
- Per-carrier state is maintained for multi-carrier modes.
- Frontend uses carrier mixing, matched-symbol integration/filtering, AGC/timing, differential phase, and shared output decode.

Mode registry:

- `app/modem/psk_modes.c/.h`.
- 27 PSK WAV modes are listed.
- Touch UI and serial mode commands use the same mode names.

Decoder backend:

- `third_party/psk_rtty/psk_rtty.c`
- `third_party/psk_rtty/include/psk_rtty.h`

Host tool:

- `tools/host_psk_rtty_wav.c`

## Serial Control

Serial commands supported by firmware:

- `MODE <name>`
- `TONE <hz>`
- `STATUS`
- `TEST 0`
- `TEST 1`
- `LOG TEXT`
- `LOG HEX`

Notes:

- `MODE <name>` changes the active decoder mode without touching the screen.
- `TEST 1` disables periodic touch/display work to reduce board-regression load.
- `TEST 0` returns to normal touch/display behavior.
- `LOG TEXT` prints decoded RX bytes as escaped text without a `[MODE]` prefix.
- `LOG HEX` prints decoded RX bytes as `[rxhex] XX YY ...`.
- `LOG HEX` is the reliable format for automated board regression.

Pitfall found:

- Earlier decoded serial lines used a `[BPSK1000]` prefix and were flushed in small chunks.
- That made board logs look like there were spaces between letters.
- HEX output proved the decoder bytes for passing modes are correct.
- Do not diagnose decode quality from human-formatted TEXT logs; use `LOG HEX`.

Known serial issue:

- After long runs, immediate manual command bursts can still be parsed with lost leading characters.
- The regression script mitigates this by resending mode commands while waiting for confirmation.
- If the board is left in a bad runtime state, reset it with pyOCD; reset restores normal UI defaults.

## UI / Touch

Files:

- `app/ui/display.c`
- `app/ui/display.h`
- `app/platform/touch_gt911.c/.h`

Current UI:

- Left top: active mode, baud, carrier count, tone, peak, input level, selected lane, queue depth.
- Left middle: spectrum/waterfall.
- Left bottom: RX TEXT.
- Right side: 3-column by 9-row mode selection matrix.

Touch:

- GT911 is working.
- On boot, log has shown:
  - `[touch] gt911 addr=0x5d id=911`
- Touch mode selection maps through `display_mode_at()`.

Important:

- Touch and periodic screen RX TEXT update are enabled by default.
- Board regression now defaults to `SerialTestMode = false` so tests do not disable touch/display unless explicitly requested.
- If `-SerialTestMode $true` is used and the script is interrupted, reset the board or send `TEST 0`.

## Host Validation

Host build:

```powershell
.\tools\build_host_tools.ps1
```

Host full regression against `C:\projects\psk-script\generated-wav-48000` passed all 27 modes after the unified frontend work:

- `PASS=27 FAIL=0 NO-WAV=0 TOTAL=27`
- Result file noted during development:
  - `tools\stage_unified_channel_48000.txt`

Do not treat `C:\projects\psk-script\generate_psk_wavs.py` as protocol truth:

- WAVs are useful stimuli.
- Protocol correctness still needs independent review against fldigi/industry behavior.
- This caution is documented in:
  - `tools/host_validation_notes.md`

## Board Validation Results

Board test command used for the full 27-mode pass/fail run:

```powershell
.\tools\board_wav_regression.ps1 -Port COM11 -MatchLength 40 -ModeWaitSeconds 15 -OutputDevice 7 -OutputGain 0.6 -RxLogFormat HEX
```

Audio output device:

- `7`: `Headphones (Synaptics HD Audio)`, 48000 Hz.
- Gain used: `0.6`.

Full board result:

- `PASS=24 FAIL=3 SKIP=0 TOTAL=27`

Board mode matrix:

| Mode | Board result | Notes |
| --- | --- | --- |
| `BPSK31` | PASS | Single-carrier non-FEC BPSK. |
| `BPSK63` | PASS | Single-carrier non-FEC BPSK. |
| `BPSK63F` | FAIL | Single-carrier FEC BPSK; host passes, board HEX has no expected 8-char fragment. |
| `BPSK125` | PASS | Single-carrier non-FEC BPSK. |
| `BPSK250` | PASS | Single-carrier non-FEC BPSK. |
| `BPSK500` | PASS | Single-carrier non-FEC BPSK. |
| `BPSK1000` | PASS | Single-carrier non-FEC BPSK; passed after using HEX logging. |
| `12xPSK125` | PASS | Multi-carrier BPSK. |
| `6xPSK250` | PASS | Multi-carrier BPSK. |
| `2xPSK500` | PASS | Multi-carrier BPSK. |
| `4xPSK500` | PASS | Multi-carrier BPSK. |
| `2xPSK800` | PASS | Multi-carrier BPSK. |
| `2xPSK1000` | PASS | Multi-carrier BPSK. |
| `QPSK31` | PASS | QPSK FEC path. |
| `QPSK63` | PASS | QPSK FEC path. |
| `QPSK125` | PASS | QPSK FEC path. |
| `QPSK250` | PASS | QPSK FEC path. |
| `QPSK500` | PASS | QPSK FEC path. |
| `8PSK125` | PASS | Non-FEC 8PSK. |
| `8PSK250` | PASS | Non-FEC 8PSK. |
| `8PSK500` | PASS | Non-FEC 8PSK. |
| `8PSK1000` | PASS | Non-FEC 8PSK. |
| `8PSK125F` | FAIL | Low-speed 8PSK FEC; host passes, board HEX has no expected 8-char fragment. |
| `8PSK250F` | FAIL | Low-speed 8PSK FEC; host passes, board HEX has no expected 8-char fragment. |
| `8PSK500F` | PASS | 8PSK FEC. |
| `8PSK1000F` | PASS | 8PSK FEC. |
| `8PSK1200F` | PASS | 8PSK FEC. |

Passing on board, grouped:

- `BPSK31`
- `BPSK63`
- `BPSK125`
- `BPSK250`
- `BPSK500`
- `BPSK1000`
- `12xPSK125`
- `6xPSK250`
- `2xPSK500`
- `4xPSK500`
- `2xPSK800`
- `2xPSK1000`
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
- `8PSK1000F`
- `8PSK1200F`

Failing on board:

- `BPSK63F`
- `8PSK125F`
- `8PSK250F`

Failure details:

- `BPSK63F` was retested with `MaxSeconds=240`; still failed.
- `BPSK63F`, `8PSK125F`, and `8PSK250F` HEX output was decoded offline from the board log.
- None of the three failing modes contained even an 8-character expected text fragment.
- Host decoder passes the same three WAVs:
  - `BPSK63F`: host match yes.
  - `8PSK125F`: host match yes.
  - `8PSK250F`: host match yes.

Conclusion:

- The remaining 3 failures are not serial formatting problems.
- They are not basic host decoder failures.
- They are likely board-side robustness issues for low-speed FEC modes:
  - actual audio clock/sample-rate tolerance,
  - symbol timing,
  - soft-decision quality,
  - FEC interleaver/Viterbi sensitivity under board analog playback/capture,
  - or mode-specific warmup/acquisition behavior.

Most likely failure directions, ordered:

1. Low-speed FEC acquisition/warmup is not robust enough on board audio.
   - The host WAV path passes, but the board receives the same WAV through Windows playback, analog output, WM8978 line input, I2S, DMA, and decimation.
   - `BPSK63F`, `8PSK125F`, and `8PSK250F` are low-speed FEC modes, so a bad initial lock can corrupt a long span before Viterbi/varicode output becomes useful.

2. Soft-decision quality is insufficient for the failing FEC modes.
   - `8PSK500F`, `8PSK1000F`, and `8PSK1200F` pass, so the FEC chain is not globally broken.
   - The failing low-speed FEC modes probably need better phase-confidence scaling and erasure/softbit handling under real board noise and timing drift.

3. Symbol timing recovery is still too fixed-window.
   - Current behavior is good enough for 24/27 board modes.
   - Low-speed FEC modes may need a more reliable timing loop, especially after analog playback/capture and the STM32 I2S fractional sample-rate mismatch.

4. Actual board audio clock is still an approximation.
   - Firmware compensates decoder rate for the measured/derived I2S rate.
   - This is a pragmatic fix, not as clean as generating an exact 48 kHz audio clock.

5. The generated WAV corpus remains a stimulus, not final protocol truth.
   - Do not assume `C:\projects\psk-script\generate_psk_wavs.py` is fully correct.
   - Host passing is necessary but not sufficient; board behavior must be checked against implementation conventions such as fldigi.

## Debug History Summary

Major resolved items:

- LCD was brought up with correct 800x480 RGB888 framebuffer and timing.
- Lowercase glyphs were added so RX TEXT preserves case.
- GT911 touch mode selection was brought up and mapped to the mode matrix.
- Codec/input was standardized around a 48 kHz audio stream with a 3:1 shared decimator.
- The older 49152 Hz / 8192 Hz assumptions were replaced by a 48k-to-16k design, with board actual-rate compensation for the STM32H7 I2S integer divider.
- Host decoder and firmware decoder now share the same decimation frontend.
- Serial `MODE` control was added so board tests do not depend on touch.
- Serial `LOG HEX` was added to eliminate text-log formatting ambiguity.
- Board regression was automated.

Important resolved misconceptions:

- The apparent "spaces between letters" in UART logs were largely a logging/line formatting artifact.
- HEX logging showed that passing modes decode correctly on the board.
- Screen text and serial text are sourced from the same `decoded` bytes, but screen rendering tails/wraps text and does not expose UART line chunking artifacts.

Current build status:

- Firmware build passes.
- Host tools build pass.
- Recent memory use:
  - RAM_D1 about 81%.
  - ROM below 4%.

## Architecture Tradeoffs

The current implementation deliberately chose a pragmatic path to get broad mode coverage and board evidence quickly. The tradeoffs are:

### 48 kHz Input, 16 kHz Decoder Base Rate

Decision:

- Use 48 kHz WAV/audio as the external audio standard.
- Decimate by 3 to a 16 kHz-class decoder rate.

Benefits:

- 48 kHz is a normal PC/audio-codec rate.
- 16 kHz is much cheaper for STM32 DSP work than directly decoding at 48 kHz.
- Host and firmware share the same decimator, reducing host/board mismatch.

Costs:

- STM32H7 I2S integer divider did not produce exact 48 kHz from the current PLL1Q source.
- Firmware now has `AUDIO_INPUT_ACTUAL_DECODE_RATE_HZ` compensation.
- Exact audio-clock design is still cleaner than compensating after the fact.

Possible improvement:

- Add a dedicated exact audio PLL plan for 48 kHz-family rates.
- Keep LCD/SDRAM PLL ownership separate; do not casually reuse PLL2.

### Shared Unified PSK Frontend

Decision:

- Route BPSK/QPSK/8PSK/multi-carrier modes through one unified frontend.

Benefits:

- Avoids separate fragile decode paths per mode.
- Makes adding/removing mode scaffolding easier.
- Host 27/27 pass proved the architecture can cover all generated PSK modes.

Costs:

- A single timing/phase/soft-decision strategy must serve very different modes.
- Low-speed FEC modes may need more mode-specific acquisition/timing tuning than the current common path provides.

Possible improvement:

- Keep the unified path, but add per-family/per-mode policy hooks:
  - timing loop constants,
  - soft-decision scaling,
  - warmup length,
  - carrier isolation width,
  - FEC lock thresholds.

### HEX Serial Logging

Decision:

- Add `LOG HEX` for board regression.
- Keep `LOG TEXT` for human inspection.

Benefits:

- Eliminates ambiguity from UART line breaks, escaping, and text prefixes.
- Proved many earlier apparent text-spacing issues were log-format artifacts.
- Lets scripts reconstruct exact decoded bytes.

Costs:

- HEX logs are larger and less readable to humans.
- 115200 baud can still be a bottleneck for high-rate modes if logging is too verbose.

Possible improvement:

- Add a compact binary or base64 framing mode with length and CRC.
- Add per-test summary counters on board to avoid streaming every decoded byte.

### Serial Test Mode

Decision:

- Add `TEST 1` to reduce touch/display load during automated board tests.

Benefits:

- Reduces CPU/UI interference during long regression.
- Keeps audio queue depth lower.

Costs:

- If left enabled, touch and periodic RX TEXT display are disabled.
- This confused manual observation until the board was reset.

Current mitigation:

- `board_wav_regression.ps1` now defaults to `SerialTestMode = false`.
- The script tries to restore `TEST 0` and `LOG TEXT` on exit.
- A board reset always restores normal UI defaults.

Possible improvement:

- Make `TEST 1` auto-timeout after inactivity.
- Add a visible screen indicator when test mode is active.

### Board Regression Through PC Analog Playback

Decision:

- Play WAVs from PC output into the board audio input.

Benefits:

- Tests the real codec/I2S/DMA/decoder path.
- Catches board-only issues that host WAV decode cannot catch.

Costs:

- Results depend on Windows audio device, output sample rate, gain, analog cable, codec input path, and possible OS audio processing.
- Some failures may be stimulus-chain related rather than decoder-core related.

Possible improvement:

- Capture board ADC samples to a WAV/log and replay them through the host decoder.
- Add a digital injection path on firmware: feed WAV samples over UART/USB/SWD or from flash/SDRAM to bypass analog playback.
- Use a known 48 kHz exclusive-mode output path or external audio generator.

## Current Working Tree Notes

Expected modified files after this debug session:

- `Core/Src/main.c`
  - serial mode control,
  - serial test mode,
  - TEXT/HEX RX log output,
  - input gate,
  - actual decoder sample-rate use.
- `app/platform/audio_input.h`
  - actual board audio rate constants.
- `tools/board_wav_regression.ps1`
  - automatic mode switching,
  - HEX/TEXT RX parsing,
  - optional serial test mode,
  - full 27-mode default list.

Generated local log:

- `tools/board_wav_regression.log`
- Treat as local artifact unless intentionally preserving a test run.

## Recommended Next Work

1. Keep the normal UI default:
   - touch enabled,
   - screen RX TEXT enabled,
   - board regression uses `LOG HEX`.

2. For the remaining 3 board failures:
   - add per-mode acquisition diagnostics:
     - symbol timing phase,
     - carrier phase error,
     - soft-decision confidence,
     - Viterbi metric spread,
     - FEC lock/warmup counters.

3. Compare board-captured decoded soft decisions against host:
   - start with `BPSK63F`, because it is slow and single-carrier.
   - then compare `8PSK125F` and `8PSK250F`.

4. Revisit audio clocking:
   - ideal fix is a precise audio PLL for 48 kHz-family clocks.
   - do not use PLL2 unless SDRAM/LCD clock ownership is redesigned.
   - consider using PLL3 only if LCD pixel clock can be preserved or independently sourced.

5. Keep using host AB tests first:
   - host must remain 27/27 before board-specific changes are accepted.
   - board tests should use `-RxLogFormat HEX`.

6. Add capture/replay tooling:
   - capture post-decimation board samples for failing modes,
   - decode the captured samples with host tools,
   - compare against direct WAV host decode.
   - This will show whether failure is before or inside the decoder.

7. Add FEC-specific debug counters:
   - Viterbi metric spread,
   - traceback confidence,
   - puncture count,
   - deinterleaver fill/warmup state,
   - varicode invalid/unknown count.
