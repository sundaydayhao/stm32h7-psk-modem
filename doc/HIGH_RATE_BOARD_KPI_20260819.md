# STM32H743 High-Rate Board KPI Baseline

Date: 2026-08-19

## Test Conditions

- Board: EmbedFire STM32H743 Challenger V2, normal LVGL/touch UI enabled.
- Firmware: current `debug` build, programmed with Fire-DAP over JTAG.
- Board serial: `COM11`, 115200 bit/s, `LOG HEX`.
- Stimulus: the common `C:\projects\psk-script\generated-wav-48000` WAV corpus;
  Windows output device 7 at gain 0.6.
- Each WAV was played to completion.  This is intentionally different from the
  ordinary regression's early exit after a matching fragment.

The reference payload is `input.txt` after whitespace normalization: 2,198
characters.  The emitted stream is that payload followed by one separator
character, repeated as needed.  A *perfect payload* is one contiguous
2,198-character exact payload match in the board's `[rxhex]` output.  The
longest clean run includes the expected separators, which gives a reproducible
maximum continuous error-free character KPI.

| Mode | Raw modulation rate | Output chars captured | Longest clean run | Perfect payloads | Whole-capture CER / BER | Queue observation |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| 12xPSK125 | 12 x 125 = 1,500 bit/s | 15,328 | 15,328 | 6 | 0 / 0 | peak `q=12`, `ov=0` |
| 4xPSK500 | 4 x 500 = 2,000 bit/s | 35,183 | 35,183 | 16 | 0 / 0 | peak `q=12`, `ov=0` |
| 2xPSK1000 | 2 x 1,000 = 2,000 bit/s | 35,185 | 35,184 | 16 | 0.00284% / 0.00107% | peak `q=15`, cumulative `ov` reached 7 |
| 8PSK1000 | 3 x 1,000 = 3,000 bit/s | 52,777 | 52,776 | 24 | 0.00189% / 0.00118% | peak `q=12`, `ov` remained 7 |
| 8PSK1200F | 3 x 1,230.77 = 3,692 bit/s coded | 20,607 | 141 | 0 | 92.95385% / 33.42857% | cold run: peak `q=12`, `maxq=15`, `ov=0` |

8PSK1200F was then repeated from a JTAG-reset cold state, making the result in
the table authoritative.  Its best 2,198-character local payload alignment is
still poor: 126 substitutions, 1 inserted character and 140 deleted characters
(CER 12.14741%; bit-equivalent BER 8.61010%).  No complete payload was
acquired, even though that cold run had `ov=0`; this failure is therefore not
explained solely by the audio queue.  BER treats an inserted or deleted
character as eight bit errors; it is an engineering comparison metric, not an
RF modem BER instrument measurement.

`ov` is a cumulative firmware counter and is not reset when changing modes.
Therefore it proves that the high-rate sequence reached the audio queue limit,
but cannot be attributed precisely to a single payload after the first
overflow.  The zero-error payload counts above are the primary decode-quality
metric.

## Reproduce

```powershell
.\tools\board_wav_regression.ps1 `
  -Modes 12xPSK125,4xPSK500,2xPSK1000,8PSK1000,8PSK1200F `
  -Port COM11 -MatchLength 999999 -ModeWaitSeconds 20 -MaxSeconds 120 `
  -OutputDevice 7 -OutputGain 0.6 -RxLogFormat HEX `
  -LogPath .\tools\board_kpi_highrate_20260819.log
```

The `999999` match length intentionally disables early-success termination.
The final 8PSK1200F capture was repeated separately because the five-mode
batch exceeded the command's ten-minute execution limit.

Raw captures:

- `tools/board_kpi_highrate_20260819.log`
- `tools/board_kpi_8PSK1200F_20260819.log`
- `tools/board_kpi_8PSK1200F_cold_20260819.log` (authoritative 8PSK1200F run)

For the HPM6750 comparison, use this exact WAV corpus, Windows output device
and gain, full-playback collection, whitespace normalization, the
2,198-character payload and its separator.  No HPM6750 board measurement is
included here.
