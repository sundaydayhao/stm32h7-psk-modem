# Stage 1 host-decoder baseline (historical, recorded 2026-08-12)

Historical note: this file records the earlier host-decoder experiment and
the later 49152 Hz to 8192 Hz path notes. It is not the current system
baseline. The current top-level audio design is 48000 Hz codec/input to
16000 Hz PSK decode rate. Use `host_validation_notes.md` and
`stage_48k16k_*.txt` for current validation observations.

Produced by `tools/run_host_regression.ps1` running the real, unmodified
`third_party/psk_rtty/psk_rtty.c` decoder (via `host_psk_rtty_wav.exe`,
built by `tools/build_host_tools.ps1` using the MSVC host toolchain)
against all 27 WAV files in `C:\projects\psk-script\generated-wav`,
checked against `C:\projects\psk-script\input.txt`.

This is the first recorded run of the actual shipped C decoder, not
`tools/wav_decode_regression.py`'s independent Python reimplementation.
Treat the table below as a historical observation for that corpus, not as
protocol ground truth.

| Mode      | Result | decoded | symbols | bits   | chars | sq     |
|-----------|--------|---------|---------|--------|-------|--------|
| BPSK31    | PASS   | 2199    | 13640   | 13608  | 2199  | 0/5    |
| BPSK63    | PASS   | 2199    | 13774   | 13712  | 2199  | 0/5    |
| BPSK63F   | PASS   | 2199    | 23969   | 23907  | 2199  | 0/5    |
| BPSK125   | PASS   | 2199    | 14037   | 13911  | 2199  | 99/5   |
| BPSK250   | PASS   | 4398    | 28573   | 28322  | 4398  | 99/5   |
| BPSK500   | PASS   | 8796    | 57152   | 56651  | 8796  | 99/5   |
| BPSK1000  | PASS   | 17592   | 113413  | 112412 | 17592 | 0/5    |
| 12xPSK125 | FAIL   | 12781   | 98160   | 98035  | 12781 | 0/5    |
| 6xPSK250  | FAIL   | 13272   | 98928   | 98678  | 13272 | 0/5    |
| 2xPSK500  | PASS   | 10995   | 70798   | 70298  | 10995 | 0/5    |
| 4xPSK500  | FAIL   | 31067   | 221600  | 221100 | 31067 | 0/5    |
| 2xPSK800  | PASS   | 30786   | 193692  | 192892 | 30786 | 0/5    |
| 2xPSK1000 | FAIL   | 34192   | 222626  | 221626 | 34192 | 0/5    |
| QPSK31    | PASS   | 2196    | 13638   | 27212  | 2196  | 0/5    |
| QPSK63    | PASS   | 2199    | 13772   | 27420  | 2199  | 0/5    |
| QPSK125   | PASS   | 2199    | 14041   | 27832  | 2199  | 0/5    |
| QPSK250   | PASS   | 4398    | 28083   | 55666  | 4398  | 0/5    |
| QPSK500   | PASS   | 6597    | 42663   | 84326  | 6597  | 0/5    |
| 8PSK125   | PASS   | 4496    | 8184    | 24177  | 4496  | 0/5    |
| 8PSK250   | PASS   | 8894    | 16258   | 48024  | 8894  | 0/5    |
| 8PSK500   | PASS   | 30884   | 55400   | 164700 | 30884 | 0/5    |
| 8PSK1000  | PASS   | 51333   | 95360   | 283080 | 51333 | 0/5    |
| 8PSK125F  | PASS   | 2226    | 8504    | 25512  | 2226  | 0/5    |
| 8PSK250F  | PASS   | 4454    | 16673   | 50016  | 4454  | 0/5    |
| 8PSK500F  | PASS   | 22174   | 59566   | 177198 | 22174 | 0/5    |
| 8PSK1000F | PASS   | 35352   | 95914   | 284742 | 35352 | 0/5    |
| 8PSK1200F | PASS   | 39794   | 109257  | 324081 | 39794 | 0/5    |

**Totals: PASS=23 FAIL=4 (all four failures are multi-carrier modes with
carriers >= 4: 12xPSK125, 6xPSK250, 4xPSK500, 2xPSK1000. Note 2xPSK500 and
2xPSK800, both carriers=2, PASS.)**

This contradicts `validated-gathering-pine.md`'s expectation that "several
of the Passing Python-verified modes" would fail on the real decoder on
clean WAV input — in fact the real decoder already handles clean-WAV,
non-impaired input correctly for all single/dual-carrier modes tested here.
The originally diagnosed live-decode failure (works on clean WAV, fails
over the real analog WM8978 path) is therefore about missing
AFC/AGC/timing-recovery robustness against *impairments* (frequency
offset, DC bias, clock drift, noise), not about basic clean-signal
correctness — the impairment-injection harness (Stage 5) is what will
actually validate the unification's benefit. The 4 multi-carrier failures
here are a separate, real bug in `generic_psk_rx_feed`'s handling of
carriers >= 4 and should also be checked against post-unification
behavior.

## Sample-rate validity update (2026-08-12)

This baseline was originally recorded by feeding `host_psk_rtty_wav.exe` the
test WAVs at their native 48000 Hz directly (`psk_rtty_config_t.sample_rate_hz
= rate`), **not** the real hardware decode rate of 8192 Hz
(`AUDIO_INPUT_DECODE_RATE_HZ`) that `app/platform/audio_input.c`'s
`process_dma_half()` actually delivers to the decoder after box-car
decimating the WM8978's native 49152 Hz stream by `AUDIO_INPUT_DECIMATION`
(6). This was a real testing-validity gap: the decoder was being validated
at the wrong operating point.

Fixed by: (1) extracting `process_dma_half()`'s box-car averaging math into
a shared `audio_decim_boxcar()` (`app/platform/audio_decim.c/.h`) callable
from both firmware and host tool; (2) regenerating all 27 test WAVs at
49152 Hz (`python generate_psk_wavs.py --all --sample-rate 49152`); (3)
updating `host_psk_rtty_wav.c` to run WAV samples through
`audio_decim_boxcar()` (decimation=6) before calling `psk_rtty_rx_feed`,
matching the real firmware signal path exactly, with `sample_rate_hz` now
set to 8192 (the true decode rate) instead of the WAV's native rate.

Re-running the full 27-mode regression through this corrected path produced
**byte-identical results** to the table above (same PASS=23/FAIL=4 split,
same decoded/symbols/bits/chars counts per mode). This confirms the
original baseline was numerically valid — `generate_psk_wavs.py` scales
symbol timing to the WAV's sample rate correctly, so the 48kHz-native test
and the 49152→8192 Hz real-decimation test carry equivalent signal
content — but it is now verified against the real hardware signal path
instead of an assumption. Use this corrected harness
(`audio_decim_boxcar` + 49152 Hz WAVs) for all Stage 3+ validation going
forward.
