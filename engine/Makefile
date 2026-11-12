CXX ?= g++
CC ?= gcc
CXXFLAGS = -std=c++17 -O2 -Wall -Iinclude -Ithird_party/kissfft
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

.PHONY: all clean test test-asan test-tsan

all: $(BIN)

$(BIN): $(CPP_OBJS) $(C_OBJS) $(RT_OBJS) | $(BUILD)
	$(CXX) $(CPP_OBJS) $(C_OBJS) $(RT_OBJS) $(STATIC) -lm -o $@

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
