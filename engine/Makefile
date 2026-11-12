CXX ?= g++
CC ?= gcc
CXXFLAGS = -std=c++17 -O2 -Wall -pthread -Iinclude -Ithird_party/kissfft
CFLAGS = -O2 -Iinclude -Ithird_party/kissfft
RT_CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Ic/include
BUILD = build

# On Windows (MinGW), static-link the C++ runtime so the exe doesn't depend
# on finding a matching libstdc++/libgcc DLL on PATH at runtime. On Linux
# (e.g. the Docker deploy image) dynamic linking against the image's own
# libstdc++ is normal and simpler, so STATIC defaults to empty there.
ifeq ($(OS),Windows_NT)
    EXE_EXT = .exe
    STATIC ?= -static
else
    EXE_EXT =
    STATIC ?=
endif

BIN = $(BUILD)/swara_engine$(EXE_EXT)

CPP_SRCS = src/main.cpp src/wav_io.cpp src/biquad.cpp src/delay_line.cpp src/spectral_gate.cpp src/limiter.cpp
C_SRCS = third_party/kissfft/kiss_fft.c third_party/kissfft/kiss_fftr.c

CPP_OBJS = $(patsubst src/%.cpp,$(BUILD)/%.o,$(CPP_SRCS))
C_OBJS = $(patsubst third_party/kissfft/%.c,$(BUILD)/kissfft_%.o,$(C_SRCS))
RT_SRCS = c/src/swara_ring.c c/src/swara_limiter.c
RT_OBJS = $(patsubst c/src/%.c,$(BUILD)/rt_%.o,$(RT_SRCS))

.PHONY: all clean test test-asan test-tsan engine-tsan coverage fuzz fuzz-libfuzzer objc-test

all: $(BIN)

$(BIN): $(CPP_OBJS) $(C_OBJS) $(RT_OBJS) | $(BUILD)
	$(CXX) $(CPP_OBJS) $(C_OBJS) $(RT_OBJS) $(STATIC) -pthread -lm -o $@

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Ic/include -c $< -o $@

$(BUILD)/rt_%.o: c/src/%.c | $(BUILD)
	$(CC) $(RT_CFLAGS) -c $< -o $@

$(BUILD)/kissfft_%.o: third_party/kissfft/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

# ---- C11 module tests -------------------------------------------------
# make test          plain build + tests
# make test-asan     AddressSanitizer + UndefinedBehaviorSanitizer
# make test-tsan     ThreadSanitizer (ring buffer producer/consumer stress)
TEST_SRC = c/tests/test_main.c c/src/swara_ring.c c/src/swara_limiter.c
TEST_FLAGS = -std=c11 -g -Wall -Wextra -Wpedantic -Werror -Ic/include -pthread
TEST_BIN = $(BUILD)/c_tests

test: | $(BUILD)
	$(CC) $(TEST_FLAGS) -O2 $(TEST_SRC) -lm -o $(TEST_BIN)
	./$(TEST_BIN)

test-asan: | $(BUILD)
	$(CC) $(TEST_FLAGS) -O1 -fsanitize=address,undefined -fno-sanitize-recover=all $(TEST_SRC) -lm -o $(TEST_BIN)_asan
	./$(TEST_BIN)_asan

test-tsan: | $(BUILD)
	$(CC) $(TEST_FLAGS) -O1 -fsanitize=thread $(TEST_SRC) -lm -o $(TEST_BIN)_tsan
	./$(TEST_BIN)_tsan

# Whole engine under ThreadSanitizer (exercises the threaded pipeline). Output: build_tsan/swara_engine
engine-tsan:
	mkdir -p build_tsan
	$(CC) -std=c11 -O1 -g -fsanitize=thread -Ic/include -c c/src/swara_ring.c -o build_tsan/rt_ring.o
	$(CC) -std=c11 -O1 -g -fsanitize=thread -Ic/include -c c/src/swara_limiter.c -o build_tsan/rt_lim.o
	$(CC) -O1 -g -fsanitize=thread -Iinclude -Ithird_party/kissfft -c third_party/kissfft/kiss_fft.c -o build_tsan/kf.o
	$(CC) -O1 -g -fsanitize=thread -Iinclude -Ithird_party/kissfft -c third_party/kissfft/kiss_fftr.c -o build_tsan/kfr.o
	$(CXX) -std=c++17 -O1 -g -fsanitize=thread -pthread -Iinclude -Ic/include -Ithird_party/kissfft src/main.cpp src/wav_io.cpp src/biquad.cpp src/delay_line.cpp src/spectral_gate.cpp src/limiter.cpp build_tsan/rt_ring.o build_tsan/rt_lim.o build_tsan/kf.o build_tsan/kfr.o -lm -o build_tsan/swara_engine

# Line coverage of the C module (gcov). Prints a per-file summary.
coverage: | $(BUILD)
	mkdir -p $(BUILD)/cov
	cd $(BUILD)/cov && $(CC) -std=c11 -O0 -g --coverage -I../../c/include -pthread ../../c/tests/test_main.c ../../c/src/swara_ring.c ../../c/src/swara_limiter.c -lm -o t && ./t && gcov -b -o . t-swara_ring.gcda t-swara_limiter.gcda | grep -A3 "File '../../c/src"

# Fuzzing: clang libFuzzer if available, otherwise the standalone random driver (gcc or clang).
FUZZ_SECS ?= 30
fuzz: | $(BUILD)
	$(CC) -std=c11 -g -O1 -DFUZZ_STANDALONE -fsanitize=address,undefined -fno-sanitize-recover=all -Ic/include c/tests/fuzz_rt.c c/src/swara_ring.c c/src/swara_limiter.c -lm -o $(BUILD)/fuzz_standalone
	./$(BUILD)/fuzz_standalone 1000000

fuzz-libfuzzer: | $(BUILD)
	clang -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined -Ic/include c/tests/fuzz_rt.c c/src/swara_ring.c c/src/swara_limiter.c -lm -o $(BUILD)/fuzz_lf
	./$(BUILD)/fuzz_lf -max_total_time=$(FUZZ_SECS)

# ---- Objective-C wrappers (built with GNUstep on Linux; also valid on macOS/iOS toolchains) ----
# Needs: gobjc, libgnustep-base-dev, gnustep-make.   make objc-test
# The C11 files are compiled as C; only the .m files are compiled as Objective-C.
OBJC_FLAGS = -std=gnu11 -O1 -g -Wall -Wno-unused-parameter -Wno-expansion-to-defined `gnustep-config --objc-flags` -Ic/include -Iobjc/include
objc-test: | $(BUILD)
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -Ic/include -c c/src/swara_ring.c -o $(BUILD)/objc_ring.o
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror -Ic/include -c c/src/swara_limiter.c -o $(BUILD)/objc_lim.o
	$(CC) $(OBJC_FLAGS) -c objc/src/SWRingBuffer.m -o $(BUILD)/SWRingBuffer.o
	$(CC) $(OBJC_FLAGS) -c objc/src/SWLimiter.m -o $(BUILD)/SWLimiter.o
	$(CC) $(OBJC_FLAGS) -c objc/tests/main.m -o $(BUILD)/objc_main.o
	$(CC) $(BUILD)/objc_ring.o $(BUILD)/objc_lim.o $(BUILD)/SWRingBuffer.o $(BUILD)/SWLimiter.o $(BUILD)/objc_main.o `gnustep-config --base-libs` -pthread -lm -o $(BUILD)/objc_tests
	./$(BUILD)/objc_tests
