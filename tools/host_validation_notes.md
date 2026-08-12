# Host Validation Notes

The host WAV corpora are test stimuli, not protocol ground truth.

Current validation sources:

- `C:\projects\psk-script\committed-wav`: small committed 48 kHz BPSK corpus.
- `C:\projects\psk-script\generated-wav-48000`: local generated 48 kHz full-mode corpus.
- `C:\projects\fldigi\src\psk\psk.cxx`: behavioral reference for PSK mode structure,
  sample rates, matched filtering, timing recovery, differential phase mapping, and FEC.

Rules for decoder changes:

- Do not change constellation mapping or FEC behavior only to satisfy one generated corpus.
- Treat a corpus failure as an observation until checked against protocol behavior or another
  independently generated/captured signal.
- The firmware and host harness must share the same audio frontend implementation.
- Codec/input rate is 48 kHz; PSK decoder base rate is 16 kHz after decimation by 3.
- `PSK_RTTY_MODE_RTTY` remains a separate FSK path and is not part of PSK pipeline unification.

Current 48 kHz to 16 kHz observations:

- `committed-wav`: 5 PASS, 0 FAIL, 22 NO-WAV.
- `generated-wav-48000`: 27 PASS, 0 FAIL after the unified channel frontend.

Earlier failing observations in `generated-wav-48000` before the unified channel frontend:

- Multi-carrier BPSK: `12xPSK125`, `6xPSK250`, `4xPSK500`, `2xPSK1000`.
- Non-FEC high-rate 8PSK: `8PSK500`, `8PSK1000`.
- Highest-rate FEC 8PSK: `8PSK1200F`.

The remaining low-speed FEC parity issue was resolved by aligning the default
warmup parity to the unified frontend group delay. A more general future
hardening step is to add fldigi-style dual-phase FEC decoding for R=1/2 streams.
