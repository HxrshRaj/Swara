# Objective-C wrappers

`SWRingBuffer` and `SWLimiter` are Objective-C classes around the C11 modules in `engine/c/`. They show how the same
C core is used from Objective-C: NSData in and out, NSError for failures, `nil` from failed initialisers,
manual retain/release (no ARC), and instance variables declared in the interface.

```bash
sudo apt-get install gobjc libgnustep-base-dev gnustep-make
make -C engine objc-test
```

`objc/tests/main.m` has 46 checks, including a two-thread producer/consumer order test through the Objective-C API.
Making a wrapper accept odd-length data on purpose makes tests fail, so the tests do catch that bug.

## Honest limits

- Built and tested with GCC and **GNUstep on Linux**. It has **not** been built with Xcode or run on macOS or iOS.
  The code uses only Foundation and plain C, so it should port, but that is not verified.
- The C ring header is treated as an opaque type inside Objective-C because GCC's Objective-C front end does not implement
  C11 `_Atomic`; the ring itself is still compiled as C.
- GNUstep's Foundation differs from Apple's in places; the tests use only the common subset.
