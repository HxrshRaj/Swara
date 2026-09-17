"""Generate WAV test signals with known, controlled frequency content, using
only stdlib `wave` + numpy (no scipy) so verification is independent of any
audio library the C++ engine itself might use.
"""
import wave
import struct
import numpy as np
import sys
import os

SR = 44100


def write_wav(path, samples, sr=SR):
    samples = np.clip(samples, -1.0, 1.0)
    ints = (samples * 32767.0).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(ints.tobytes())


def read_wav(path):
    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        n = w.getnframes()
        ch = w.getnchannels()
        raw = w.readframes(n)
    data = np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0
    if ch > 1:
        data = data.reshape(-1, ch)
    return data, sr


def tone(freq, dur, sr=SR, amp=0.5, phase=0.0):
    t = np.arange(int(dur * sr)) / sr
    return amp * np.sin(2 * np.pi * freq * t + phase)


def main():
    os.makedirs("test_audio", exist_ok=True)

    # 1. Two-tone signal for lowpass/highpass verification: a low tone
    #    well below typical cutoffs and a high tone well above them.
    low = tone(200, 2.0, amp=0.4)
    high = tone(6000, 2.0, amp=0.4)
    two_tone = low + high
    write_wav("test_audio/two_tone.wav", two_tone)

    # 2. Single impulse (unit spike) for delay-line verification: the
    #    echo should reappear exactly `delay_ms` later.
    n = int(2.0 * SR)
    impulse = np.zeros(n)
    impulse[1000] = 0.9
    write_wav("test_audio/impulse.wav", impulse)

    # 3. Burst + quiet noise for noise-gate verification: loud tone bursts
    #    separated by quiet broadband noise.
    rng = np.random.default_rng(42)
    dur = 3.0
    n = int(dur * SR)
    sig = np.zeros(n)
    quiet_noise = rng.normal(0, 0.01, n)
    sig += quiet_noise
    burst = tone(1000, 0.5, amp=0.6)
    for start_s in (0.2, 1.2, 2.2):
        s = int(start_s * SR)
        sig[s:s + len(burst)] += burst
    write_wav("test_audio/burst_noise.wav", sig)

    # 4. Sweep-ish multi-tone signal, useful as a general "does it still
    #    sound like audio" smoke test / for the demo frontend.
    mix = tone(220, 3.0, amp=0.25) + tone(440, 3.0, amp=0.2) + tone(880, 3.0, amp=0.15)
    write_wav("test_audio/music_like.wav", mix)

    print("Generated test_audio/two_tone.wav, impulse.wav, burst_noise.wav, music_like.wav")


if __name__ == "__main__":
    main()
