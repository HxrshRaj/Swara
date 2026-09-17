CXX ?= g++
CC ?= gcc
CXXFLAGS = -std=c++17 -O2 -Wall -Iinclude -Ithird_party/kissfft
CFLAGS = -O2 -Iinclude -Ithird_party/kissfft
BUILD = build
BIN = $(BUILD)/swara_engine.exe

CPP_SRCS = src/main.cpp src/wav_io.cpp src/biquad.cpp src/delay_line.cpp src/spectral_gate.cpp
C_SRCS = third_party/kissfft/kiss_fft.c third_party/kissfft/kiss_fftr.c

CPP_OBJS = $(patsubst src/%.cpp,$(BUILD)/%.o,$(CPP_SRCS))
C_OBJS = $(patsubst third_party/kissfft/%.c,$(BUILD)/kissfft_%.o,$(C_SRCS))

.PHONY: all clean test

all: $(BIN)

$(BIN): $(CPP_OBJS) $(C_OBJS) | $(BUILD)
	$(CXX) $(CPP_OBJS) $(C_OBJS) -static -o $@

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/kissfft_%.o: third_party/kissfft/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
