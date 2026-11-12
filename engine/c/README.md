# swara_rt: real-time-safe C11 primitives

Two small modules used by the Swara engine, written in plain C11 and called from the C++ engine through `extern "C"`.

| Module | What it is |
|---|---|
| `swara_ring` | Lock-free single-producer / single-consumer ring buffer of floats, built on C11 atomics (`stdatomic.h`) with acquire/release ordering. |
| `swara_limiter` | Peak limiter with separate attack and release smoothing and a hard output ceiling. Used by the engine as the `limiter` effect. |

## Rules the code follows

- No `malloc`, no `free`: the caller supplies all storage, so no allocation can happen on an audio thread.
- No recursion; every loop is bounded by a length argument.
- Every public function validates its arguments (null pointers, zero or non-power-of-two sizes, NaN or out-of-range parameters) and returns a `swara_status` code.
- Fixed-width and `size_t` types; unsigned counters that wrap on purpose (the ring uses free-running head and tail indices).
- Compiled with `-std=c11 -Wall -Wextra -Wpedantic -Werror`.

These rules are inspired by common safety-critical C practice. The code is **not** certified, and it does not claim compliance with MISRA C, DO-178C or any other standard.

## Ring buffer design

`head` is written only by the producer and `tail` only by the consumer, so no locks or compare-and-swap are needed. The producer publishes data with a release store of `head`; the consumer reads `head` with an acquire load, so it sees the samples written before the store. The capacity must be a power of two so wrapping is a mask, and one slot is kept empty, so a buffer of capacity N holds N-1 samples. Writes and reads are all-or-nothing: if there is not enough space or data, nothing is transferred and an error is returned.

## Limiter design

For each sample the limiter computes the gain that would bring that sample to the ceiling, then moves its current gain toward that target with a one-pole smoother: fast (attack) when it must reduce gain, slow (release) when it recovers. Smoothing can lag a sharp peak, so the final sample is also clamped to the ceiling. The output therefore never exceeds the threshold, and signals below it pass through unchanged. State is carried across calls, so the result does not depend on the block size (tested).

## Build and test

```bash
cd engine
make test          # plain build and tests
make test-asan     # AddressSanitizer + UndefinedBehaviorSanitizer
make test-tsan     # ThreadSanitizer (two-thread ring buffer stress)
```

`c/tests/bench_ring.c` measures two-thread ring throughput; run it with the command shown in `.github/workflows/ci.yml`.
