# DSP Design

This document explains the signal-processing choices behind each effect in
`engine/`, and how the streaming/latency model works. For the measured
latency numbers themselves, see the main [README](../README.md).

## 1. Butterworth IIR filter (lowpass / highpass)

**File:** [`engine/include/biquad.h`](../engine/include/biquad.h), [`engine/src/biquad.cpp`](../engine/src/biquad.cpp)

### IIR vs FIR, and why Butterworth

An FIR filter of comparable rolloff steepness would need dozens to hundreds
of taps (direct convolution per sample), which is both more CPU per sample
and adds more delay (an FIR filter's group delay is roughly half its tap
count in samples). A low-order IIR filter reaches a comparable rolloff with
2-4 multiply-adds per sample and near-zero added delay, which is the right
tradeoff for a real-time effect where the filter is a small part of a larger
chain. The cost is a non-linear phase response — audible on very steep,
high-order IIR filters as phase smearing — but for the modest orders used
here (2nd-4th order) that's inaudible for the kind of tone-shaping this
engine targets.

Among IIR designs, **Butterworth** was chosen over Chebyshev/Elliptic
because it has a maximally flat passband (no ripple before the cutoff),
which matters for an effect meant to sound like "the frequencies above/below
X are gone," not like an EQ with a resonant bump at the cutoff.

### Implementation

Each Butterworth filter is a **cascade of second-order sections (biquads)**,
which is the standard way to implement higher-order IIR filters: a single
high-order transfer function is numerically ill-conditioned to implement
directly (coefficients span many orders of magnitude, causing serious
round-off error), so it's factored into second-order sections that are each
individually well-conditioned.

Each biquad's coefficients come from the **RBJ Audio EQ Cookbook**
formulas (the standard reference for these), parameterized by a per-section
quality factor `Q`. For an order-`N` Butterworth filter, the `Q` of pole
pair `k` (`k = 1..floor(N/2)`) is the closed form:

```
Q_k = 1 / (2 * sin((2k-1)*pi / (2N)))
```

This is derived from the Butterworth pole layout (poles equally spaced on
the unit circle in the s-plane) and reproduces the standard published
Butterworth `Q` tables, e.g. for `N=4`: `Q1=0.5412, Q2=1.3066`; for `N=3`:
one first-order real-pole section plus `Q=1.0`. `engine/tools/verify_correctness.py`
checks this numerically against the pole-derived values for several
orders. Odd orders get one extra first-order section (a single real pole),
built directly via the bilinear transform of the analog RC prototype.

### Correctness verification

`verify_correctness.py` doesn't trust the C++ engine's own math — it
independently FFTs the *output* audio with `numpy.fft` (a library the
engine doesn't use) and checks:

- A 4th-order lowpass @ 1000 Hz keeps a 200 Hz tone and rejects a 6000 Hz
  tone by > 40 dB (measured, not assumed).
- The same filter's response is monotonically non-increasing as frequency
  rises past the cutoff, swept across 100 Hz-8000 Hz single tones.
- The mirror-image highpass check.

## 2. STFT-based spectral noise gate

**File:** [`engine/include/spectral_gate.h`](../engine/include/spectral_gate.h), [`engine/src/spectral_gate.cpp`](../engine/src/spectral_gate.cpp)

### Why FFT-based, and why a noise gate

A time-domain noise gate (threshold on the raw waveform's envelope) can
only make a single broadband open/close decision — it can't attenuate a
noisy high end while leaving a clean low end alone. Doing this properly
requires knowing the *frequency content* of what's currently "quiet" vs
"loud," which means an actual FFT: an STFT (short-time Fourier transform)
is computed on overlapping analysis frames, gated per-bin in the frequency
domain, and reconstructed with an inverse FFT. The FFT itself is
[KissFFT](https://github.com/mborgerding/kissfft) (`engine/third_party/kissfft/`,
vendored unmodified, BSD-3), used via its real-input `kiss_fftr`/`kiss_fftri`
API rather than a hand-rolled transform.

### STFT parameters

- **Frame size:** 1024 samples (configurable via `--gate-frame`).
- **Hop size:** 512 samples (50% overlap) — chosen specifically because a
  **periodic Hann window at 50% hop sums to exactly 1** across overlapping
  frames (`w[n] + w[n - N/2] = 1` for all `n`, a standard COLA identity).
  That means analysis-only windowing plus a plain overlap-add reconstructs
  an *unmodified* spectrum exactly, with no synthesis window or extra
  normalization step needed — one less place for bugs or coloration to
  creep in.
- **Window:** periodic (DFT-even) Hann, `w[n] = 0.5*(1 - cos(2*pi*n/N))`.

### Gating

For each bin, a **per-bin adaptive noise floor** is tracked with an
asymmetric, damped envelope follower:

- It decays toward a new low magnitude faster (`0.15` per-frame) than it
  rises toward a new high one (`0.01` per-frame) — the classic
  minimum-statistics idea: quiet stretches pull the floor down quickly,
  but a loud transient doesn't drag the floor up with it, so the gate
  doesn't get fooled by the very signal it's supposed to pass through.
- It's **bootstrapped from the first analyzed frame** rather than starting
  at zero, so it reflects the real ambient level immediately instead of
  ramping up over ~100 frames (~1.2s at this hop size).
- An **instant snap-to-minimum** was tried first and rejected: with
  random noise, *some* bin/frame combination is always near-silent by
  chance, so an unbounded minimum tracker ratchets down toward zero over
  time and the gate never closes on real noise again. The damped version
  fixes this — see the comment in `spectral_gate.cpp` for the exact
  failure mode this was tuned against.

The gate itself compares a **smoothed magnitude** (a fast `0.3`-coefficient
envelope, separate from the floor) against `floor + threshold`, with a
soft-knee gain ramp (default 6 dB knee) rather than a hard on/off, to avoid
audible zippering. The smoothing matters: a single raw STFT frame of
stationary noise has high frame-to-frame magnitude variance (it's a random
variable, not a constant), so gating on the instantaneous value made the
gate flicker open on quiet noise purely by chance; a short envelope
follower before the gate decision knocks that variance down.

### Algorithmic latency (distinct from processing time)

This is the one effect in the engine with **real, unavoidable algorithmic
latency**: output for sample `n` only exists once a full analysis frame
covering `n` has been accumulated, so the first `frameSize - hopSize` (512
samples ≈ 11.6 ms @ 44.1kHz) of a stream are structurally silent before any
real output appears. This is not a shortcut — it's the actual latency cost
of doing FFT-based processing in a streaming context, and it's on top of
(not instead of) the per-buffer compute time reported in the latency
tables. Any FFT-based real-time effect (this gate, a vocoder, pitch
correction, etc.) pays this same kind of frame-based delay; it's a genuine
tradeoff of frequency-domain processing that a time-domain filter doesn't
have.

### Correctness verification

`verify_correctness.py` synthesizes a signal with quiet Gaussian noise and
three loud 1kHz tone bursts, runs it through the gate, and checks — via
independent RMS measurement on the *output* WAV — that quiet-segment
energy is attenuated markedly more than burst-segment energy (in the
current tuning: ~12 dB more), and that the loud bursts themselves lose
less than 6 dB (i.e. the gate isn't just crushing everything).

## 3. Delay / echo (circular buffer)

**File:** [`engine/include/delay_line.h`](../engine/include/delay_line.h), [`engine/src/delay_line.cpp`](../engine/src/delay_line.cpp)

A fixed-capacity ring buffer (`CircularBuffer`) sized to the requested delay
time in samples (`delay_ms * sample_rate / 1000`). Each sample:

```
delayed = buffer.read(delaySamples)     // read N samples behind the write head
buffer.write(input + feedback*delayed)  // feed back into the line, decayed
output  = (1-mix)*input + mix*delayed   // wet/dry blend
```

This is a standard feedback delay line: each pass through the buffer
decays by `feedback` (clamped to 0.98 to guarantee the geometric series
converges rather than building up toward clipping), producing a
naturally-decaying series of echoes at integer multiples of the delay time.
`read`/`write` are O(1) index operations with no dynamic allocation in the
per-sample path.

### Correctness verification

`verify_correctness.py` runs a single-sample impulse through the delay and
confirms (against the *output* WAV, not the engine's internal state): an
echo appears within ±1 sample of the configured delay time, at a
significant amplitude, and a second, smaller echo appears one more delay
period later (confirming feedback decay, not just a single fixed-offset
copy).

## Streaming / block-processing model

All three effects implement a common `Effect::process(in, out, numSamples)`
interface and carry their own state (`BiquadStage` history, `CircularBuffer`
head position, the gate's OLA/floor state) across calls. `main.cpp` reads
the input WAV once but then feeds it through the effect chain in
fixed-size blocks via a loop, exactly as a real-time audio callback would
receive a `numSamples`-sized buffer at a time — there's no "process the
whole file's spectrum at once" shortcut anywhere in the effect
implementations themselves. This is what makes the per-buffer latency
measurements in the README meaningful: they're timing the same
buffer-in/buffer-out call shape a live audio callback would make, just
driven by a file loop instead of a sound card's interrupt.
