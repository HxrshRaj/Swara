"""Independent correctness verification for the C++ DSP engine.

This does NOT trust the engine's own math. It re-derives expectations from
first principles (or from numpy's FFT, a library the engine itself does not
use) and checks the *measured output* against them:

  1. Lowpass filter: two-tone signal (200 Hz + 6000 Hz) through a 4th-order
     Butterworth lowpass @ 1000 Hz must keep the low tone and attenuate the
     high tone by roughly the theoretical Butterworth rolloff.
  2. Highpass filter: same signal, opposite expectation.
  3. Delay: an impulse must reappear as a distinct echo at the configured
     delay time (within one sample of the requested delay).
  4. Noise gate: RMS energy during quiet segments must drop much more than
     RMS energy during loud burst segments.

Exits with a non-zero status and a printed failure list if anything is off.
"""
import subprocess
import sys
import os
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from gen_test_signal import read_wav, SR  # noqa: E402

ENGINE = os.path.join(os.path.dirname(__file__), "..", "build", "swara_engine.exe")
AUDIO = os.path.join(os.path.dirname(__file__), "..", "test_audio")

failures = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name}" + (f" -- {detail}" if detail else ""))
    if not cond:
        failures.append(name)


def run_engine(*args):
    cmd = [ENGINE] + list(args)
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"engine failed: {r.stderr}")
    return r.stdout


def band_energy(signal, sr, f_lo, f_hi):
    n = len(signal)
    spec = np.fft.rfft(signal * np.hanning(n))
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    mask = (freqs >= f_lo) & (freqs <= f_hi)
    return float(np.sum(np.abs(spec[mask]) ** 2))


def test_lowpass():
    out_path = os.path.join(AUDIO, "two_tone_lp.wav")
    run_engine("--input", os.path.join(AUDIO, "two_tone.wav"), "--output", out_path,
               "--effects", "lowpass", "--lp-cutoff", "1000", "--lp-order", "4", "--buffer-size", "512")
    data, sr = read_wav(out_path)
    low_energy = band_energy(data, sr, 150, 250)
    high_energy = band_energy(data, sr, 5900, 6100)
    ratio_db = 10 * np.log10((low_energy + 1e-20) / (high_energy + 1e-20))
    # 4th-order Butterworth = 24 dB/octave. 6000 Hz is ~2.6 octaves above the
    # 1000 Hz cutoff, so theoretical attenuation is large; require a strong,
    # clearly-audible separation (>= 40 dB) without pinning to the exact
    # theoretical number (windowing/leakage add slop).
    check("lowpass keeps 200 Hz / rejects 6000 Hz",
          ratio_db > 40, f"low/high energy ratio = {ratio_db:.1f} dB (want > 40 dB)")


def test_highpass():
    out_path = os.path.join(AUDIO, "two_tone_hp.wav")
    run_engine("--input", os.path.join(AUDIO, "two_tone.wav"), "--output", out_path,
               "--effects", "highpass", "--hp-cutoff", "1000", "--hp-order", "4", "--buffer-size", "512")
    data, sr = read_wav(out_path)
    low_energy = band_energy(data, sr, 150, 250)
    high_energy = band_energy(data, sr, 5900, 6100)
    ratio_db = 10 * np.log10((high_energy + 1e-20) / (low_energy + 1e-20))
    check("highpass keeps 6000 Hz / rejects 200 Hz",
          ratio_db > 40, f"high/low energy ratio = {ratio_db:.1f} dB (want > 40 dB)")


def test_filter_rolloff_direction():
    """Sanity check the *shape* of the response, not just two spot frequencies:
    sweep a set of single-tone files through the lowpass filter and confirm
    output energy is monotonically non-increasing as frequency rises past
    the cutoff."""
    freqs = [100, 500, 1000, 2000, 4000, 8000]
    energies = []
    for f in freqs:
        t = np.arange(int(1.0 * SR)) / SR
        tone = 0.5 * np.sin(2 * np.pi * f * t)
        in_path = os.path.join(AUDIO, f"_tone_{f}.wav")
        out_path = os.path.join(AUDIO, f"_tone_{f}_lp.wav")
        from gen_test_signal import write_wav
        write_wav(in_path, tone)
        run_engine("--input", in_path, "--output", out_path,
                   "--effects", "lowpass", "--lp-cutoff", "1000", "--lp-order", "4", "--buffer-size", "512")
        data, sr = read_wav(out_path)
        rms = float(np.sqrt(np.mean(data[2000:] ** 2)))  # skip filter settling transient
        energies.append(rms)
    print("    lowpass RMS by frequency:", dict(zip(freqs, [round(e, 4) for e in energies])))
    passband_ok = energies[0] > 0.3  # 100 Hz should pass through near-unattenuated
    stopband_ok = energies[-1] < energies[0] * 0.02  # 8000 Hz should be crushed
    monotonic_past_cutoff = all(energies[i] >= energies[i + 1] - 1e-3 for i in range(2, len(energies) - 1))
    check("lowpass passband near-unity at 100 Hz", passband_ok, f"rms={energies[0]:.4f}")
    check("lowpass stopband strongly attenuated at 8000 Hz", stopband_ok,
          f"rms={energies[-1]:.4f} vs passband {energies[0]:.4f}")
    check("lowpass response falls monotonically above cutoff", monotonic_past_cutoff, str(energies))


def test_delay():
    out_path = os.path.join(AUDIO, "impulse_delay.wav")
    delay_ms = 200.0
    run_engine("--input", os.path.join(AUDIO, "impulse.wav"), "--output", out_path,
               "--effects", "delay", "--delay-ms", str(delay_ms), "--delay-feedback", "0.5",
               "--delay-mix", "0.6", "--buffer-size", "256")
    data, sr = read_wav(out_path)
    impulse_idx = 1000
    expected_echo_idx = impulse_idx + int(delay_ms * sr / 1000.0)
    window = data[expected_echo_idx - 5: expected_echo_idx + 6]
    peak_offset = int(np.argmax(np.abs(window))) - 5
    peak_val = float(np.max(np.abs(window)))
    check("delay produces an echo at the configured delay time",
          abs(peak_offset) <= 1 and peak_val > 0.1,
          f"echo peak at offset {peak_offset} samples from expected, amplitude={peak_val:.3f}")

    # Second echo (feedback) should appear roughly delay_ms later still, at
    # a smaller amplitude (feedback < 1 means decay).
    second_idx = expected_echo_idx + int(delay_ms * sr / 1000.0)
    window2 = data[second_idx - 5: second_idx + 6]
    peak2 = float(np.max(np.abs(window2)))
    check("delay feedback produces a second, decayed echo",
          0 < peak2 < peak_val, f"first echo={peak_val:.3f}, second echo={peak2:.3f}")


def test_noise_gate():
    out_path = os.path.join(AUDIO, "burst_gated.wav")
    run_engine("--input", os.path.join(AUDIO, "burst_noise.wav"), "--output", out_path,
               "--effects", "noisegate", "--gate-frame", "1024", "--buffer-size", "512")
    orig, sr = read_wav(os.path.join(AUDIO, "burst_noise.wav"))
    gated, _ = read_wav(out_path)

    def rms(x):
        return float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0

    # Quiet region (well before first burst, past STFT startup latency).
    quiet_orig = rms(orig[3000:8000])
    quiet_gated = rms(gated[3000:8000])
    # Loud burst region (burst starts at 0.2s = sample 8820, well inside it).
    burst_start = int(0.25 * sr)
    burst_end = burst_start + 15000
    loud_orig = rms(orig[burst_start:burst_end])
    loud_gated = rms(gated[burst_start:burst_end])

    quiet_atten_db = 20 * np.log10((quiet_orig + 1e-9) / (quiet_gated + 1e-9))
    loud_atten_db = 20 * np.log10((loud_orig + 1e-9) / (loud_gated + 1e-9))

    check("noise gate attenuates quiet noise much more than loud bursts",
          quiet_atten_db > loud_atten_db + 6,
          f"quiet attenuation={quiet_atten_db:.1f} dB, burst attenuation={loud_atten_db:.1f} dB")
    check("noise gate preserves most of the loud burst energy",
          loud_atten_db < 6, f"burst attenuation={loud_atten_db:.1f} dB (want < 6 dB)")


def main():
    test_lowpass()
    test_highpass()
    test_filter_rolloff_direction()
    test_delay()
    test_noise_gate()

    print()
    if failures:
        print(f"{len(failures)} check(s) FAILED: {failures}")
        sys.exit(1)
    else:
        print("All correctness checks PASSED.")


if __name__ == "__main__":
    main()
