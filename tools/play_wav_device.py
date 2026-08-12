import argparse
import wave

import numpy as np
import sounddevice as sd


def read_wav(path: str) -> tuple[int, np.ndarray]:
    with wave.open(path, "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        frames = wav.readframes(wav.getnframes())

    if sample_width != 2:
        raise ValueError(f"unsupported WAV sample width: {sample_width}")

    audio = np.frombuffer(frames, dtype="<i2")
    if channels > 1:
        audio = audio.reshape(-1, channels)
    else:
        audio = np.repeat(audio.reshape(-1, 1), 2, axis=1)
    return sample_rate, audio


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("wav")
    parser.add_argument("--device", type=int, required=True)
    parser.add_argument("--gain", type=float, default=1.0)
    args = parser.parse_args()

    sample_rate, audio = read_wav(args.wav)
    if args.gain != 1.0:
        audio = np.clip(audio.astype(np.float32) * args.gain, -32768.0, 32767.0).astype(np.int16)
    sd.play(audio, samplerate=sample_rate, device=args.device, blocking=True)
    sd.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
