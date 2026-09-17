# Swara — Real-Time Audio DSP Processor

A full-stack, deployable audio effects processor built around a genuine C++
DSP engine: real IIR filter math, a real FFT (KissFFT) driving a spectral
noise gate, and a real circular-buffer delay line — wrapped in a FastAPI
backend and a React frontend so it's demoable from a browser, not just a
CLI.

Live deployment: not yet deployed — see [Deployment](#deployment) below for
why and what's ready to go.

## What it does

Upload a `.wav` file, pick one or more effects (lowpass/highpass Butterworth
filter, delay/echo, or an FFT-based spectral noise gate), and get back the
processed audio plus the *actual measured* per-buffer processing latency
from the C++ engine — not an estimate.

## Real measured latency

Numbers below are from `engine/tools/verify_correctness.py`'s test signals,
run through the compiled `swara_engine` binary on the development machine
(Windows 11, MinGW-w64 g++ 15.2, `-O2`), instrumented with
`std::chrono::high_resolution_clock` around each buffer-in → buffer-out
call — see [`engine/include/latency.h`](engine/include/latency.h) and the
`Stopwatch`/`LatencyStats` usage in [`engine/src/main.cpp`](engine/src/main.cpp).
Reproduce with:

```bash
cd engine
make
./build/swara_engine --input test_audio/music_like.wav --output /tmp/out.wav \
  --effects lowpass,delay,noisegate --buffer-size 512 \
  --benchmark-sizes 64,128,256,512,1024,2048,4096
```

### Full chain (lowpass + delay + noise gate) vs. real-time budget

| Buffer (samples) | RT budget (µs) | mean (µs) | p50 (µs) | p95 (µs) | p99 (µs) | max (µs) |
|---:|---:|---:|---:|---:|---:|---:|
| 64   | 1,451  | 6.5   | 1.4   | 34.6  | 68.7   | 120.8  |
| 128  | 2,902  | 14.1  | 2.4   | 42.5  | 103.1  | 1189.2 |
| 256  | 5,805  | 33.4  | 34.2  | 104.8 | 198.8  | 711.7  |
| 512  | 11,610 | 62.6  | 42.0  | 126.2 | 181.6  | 223.3  |
| 1024 | 23,220 | 102.1 | 78.7  | 210.9 | 296.7  | 497.4  |
| 2048 | 46,440 | 220.4 | 173.5 | 389.0 | 470.6  | 480.4  |
| 4096 | 92,880 | 497.3 | 318.5 | 964.5 | 1453.2 | 1664.3 |

"RT budget" is how long a buffer of that size represents at 44.1 kHz
(`buffer_size / sample_rate`) — the deadline a real-time audio callback
would have to hit. At every buffer size tested, mean processing time is
**1-2 orders of magnitude under budget**, with real per-run variance
(min/max spread) rather than a single flat number — that spread is genuine
OS scheduling jitter on a non-realtime desktop OS, not synthetic noise.

### Per-effect breakdown (buffer = 512 samples, single effect each)

| Effect | mean (µs) | p50 (µs) | p95 (µs) | p99 (µs) |
|---|---:|---:|---:|---:|
| Lowpass (Butterworth, order 4)  | 3.3  | 3.4  | 4.0   | 4.2   |
| Delay / echo                    | 4.7  | 4.0  | 7.7   | 8.2   |
| Spectral noise gate (FFT, 1024) | 48.3 | 45.2 | 73.7  | 86.2  |

The noise gate costs ~10-15x more than the biquad filter or delay line per
buffer — expected, since it's doing a forward FFT, per-bin gain
computation, and inverse FFT per hop, versus a handful of multiply-adds per
sample for the others. It also has **algorithmic latency** the other two
don't (see [`docs/dsp-design.md`](docs/dsp-design.md#algorithmic-latency-distinct-from-processing-time)):
the first ~11.6 ms of gated output is structurally silent because a full
STFT analysis frame has to accumulate first. That's a genuine property of
frequency-domain block processing, not a measurement artifact, and it's
reported separately from the compute-time numbers above.

Full JSON output (all buffer sizes, all effects) is reproducible via the
command above; `engine/tools/verify_correctness.py` also re-derives
correctness independently (see below) each time it runs.

## Verified correctness, not just "it compiled"

`engine/tools/verify_correctness.py` doesn't trust the C++ engine's own
math — it re-analyzes the *output audio* with `numpy.fft` (a library the
engine doesn't use) against known test signals:

```bash
cd engine
python tools/gen_test_signal.py       # generates test_audio/*.wav
python tools/verify_correctness.py    # builds+runs the engine, checks output spectra
```

Current result: **all 9 checks pass**, including a 4th-order Butterworth
lowpass rejecting a 6000 Hz tone by 64 dB while passing a 200 Hz tone,
monotonic rolloff across a 100 Hz-8000 Hz sweep, a delay line reproducing
echoes within ±1 sample of the configured delay time with correct feedback
decay, and a spectral gate attenuating quiet noise ~12 dB more than loud
bursts. Details and the filter math behind each check are in
[`docs/dsp-design.md`](docs/dsp-design.md).

## Architecture

```
 Browser (React)                FastAPI backend               C++ DSP engine
┌──────────────────┐  multipart ┌───────────────────┐  subprocess ┌──────────────────┐
│ upload .wav       │──────────▶│ POST /api/process  │────────────▶│ swara_engine CLI  │
│ pick effects      │           │  - saves upload    │             │  - reads WAV       │
│ play orig/processed│◀──────────│  - shells out to   │◀────────────│  - streams through │
│ show latency table │  JSON:    │    the engine       │  stats.json │    effect chain,   │
└──────────────────┘  audio+    │  - returns audio     │  + out.wav │    fixed-size      │
                       stats     │    (base64) + stats  │            │    blocks          │
                                 └───────────────────┘             │  - writes WAV +    │
                                                                    │    measured latency │
                                                                    └──────────────────┘
```

- **C++ engine** (`engine/`): standalone CLI, no dependency on the backend.
  Built with a plain `Makefile` (no CMake — see "Why no CMake" below)
  against [KissFFT](https://github.com/mborgerding/kissfft) (vendored in
  `engine/third_party/kissfft/`, BSD-3, unmodified).
- **Backend** (`backend/`): FastAPI, chosen for async file I/O, automatic
  request validation via Pydantic, and because Python's `subprocess` module
  makes shelling out to a compiled binary trivial and safe (argument list,
  not shell string — no injection surface). The engine is invoked via
  **subprocess**, not a Python/C++ binding (pybind11 etc.) — see
  [`backend/app/engine_bridge.py`](backend/app/engine_bridge.py) for the
  reasoning (the engine is a self-contained streaming CLI with no
  per-sample call boundary to cross, so a binding would add build
  complexity without a real benefit here).
- **Frontend** (`frontend/`): React + Vite. Plain `fetch`/`FormData` to the
  backend, `<audio>` elements with `URL.createObjectURL` for playback —
  no audio-specific framework needed since the DSP work all happens
  server-side.

### Why no CMake

The engine builds with a small hand-written `Makefile`
(`engine/Makefile`) instead of CMake. This was a pragmatic call for the
development environment this was built in (CMake wasn't installed, and the
build graph here — a handful of `.cpp`/`.c` files with one shared include
path — doesn't need CMake's cross-platform abstraction machinery). The
Makefile does still handle both targets it needs to: MinGW/Windows
(static-links the C++ runtime, since dynamically linking against whatever
`libstdc++-6.dll` happens to be first on `PATH` caused real, silent heap
corruption during development — see the commit history) and Linux/Docker
(dynamic linking, which is fine and normal inside a controlled container
image).

## Running locally

### 1. Build and verify the C++ engine

```bash
cd engine
make
python tools/gen_test_signal.py
python tools/verify_correctness.py
```

### 2. Run the backend

```bash
cd backend
python -m venv venv && source venv/bin/activate   # or venv\Scripts\activate on Windows
pip install -r requirements.txt
export SWARA_ENGINE_PATH=../engine/build/swara_engine   # swara_engine.exe on Windows
uvicorn app.main:app --reload --port 8000
```

Check it's alive and can see the engine: `curl http://127.0.0.1:8000/api/health`

### 3. Run the frontend

```bash
cd frontend
npm install
npm run dev
```

Open the printed local URL (default `http://localhost:5173`), upload a
`.wav` file, pick effects, and hit "Process audio."

### 4. Or run everything in one container (production-equivalent)

```bash
docker build -t swara:local .
docker run -p 8000:8000 swara:local
```

This compiles the engine for Linux (not the Windows build above), builds
the frontend, and serves both from one FastAPI process at
`http://localhost:8000` — the same image used for deployment.

## Deployment

The app is packaged as a single Docker image (root [`Dockerfile`](Dockerfile))
that compiles the real C++ engine for Linux in a build stage, builds the
frontend, and serves both from one FastAPI process — this was built and run
locally with `docker build` + `docker run` and verified end to end (health
check, a real `/api/process` upload returning genuinely different audio and
real latency numbers, and the static frontend all working from the
container) before writing this section. [`render.yaml`](render.yaml) is a
ready-to-use [Render Blueprint](https://render.com/docs/blueprint-spec) for
it.

**Live link:** not deployed by this build — I do not have credentials for a
Render/Railway/similar account, and creating a hosting account isn't
something I'll do on someone else's behalf. Everything needed to deploy is
committed and tested; deploying it is a ~2-minute manual step:

1. Push this repo to GitHub (or use it directly if already there).
2. On [render.com](https://render.com): **New → Blueprint**, point it at
   this repo. Render reads `render.yaml` and builds the `Dockerfile`
   automatically — no manual config needed.
3. Once it's live, put the URL at the top of this README and in the
   frontend's `.env.production` if you want a non-relative `VITE_API_URL`
   (not required — the production build already talks to its own origin).

Railway works the same way (point it at the repo; it detects the
`Dockerfile`) if preferred instead.

## Repository layout

```
engine/     C++ DSP engine (filters, FFT noise gate, delay line, CLI, tests)
backend/    FastAPI service wrapping the engine
frontend/   React UI
docs/       DSP design notes (docs/dsp-design.md)
Dockerfile  Multi-stage build: frontend (Node) + engine (Linux g++) + runtime (Python)
```

## Docs

- [`docs/dsp-design.md`](docs/dsp-design.md) — filter design (cutoff, order,
  IIR vs FIR, Butterworth Q derivation), the STFT/FFT approach behind the
  noise gate, and the delay line implementation.
