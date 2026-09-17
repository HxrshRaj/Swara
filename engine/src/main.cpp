#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <algorithm>

#include "wav_io.h"
#include "biquad.h"
#include "delay_line.h"
#include "spectral_gate.h"
#include "effect.h"
#include "latency.h"

using namespace swara;

namespace {

struct Args {
    std::string input;
    std::string output;
    std::string effects = "lowpass,delay"; // comma-separated, applied in order
    int bufferSize = 512;
    std::string statsOut;
    std::string benchmarkSizes; // e.g. "64,128,256,512,1024,2048"

    double lpCutoff = 4000.0;
    int lpOrder = 4;
    double hpCutoff = 300.0;
    int hpOrder = 4;

    double delayMs = 300.0;
    float delayFeedback = 0.35f;
    float delayMix = 0.35f;

    int gateFrame = 1024;
    double gateThresholdDb = 10.0;
    double gateFloorDb = -26.0;
    double gateKneeDb = 6.0;
};

std::map<std::string, std::string> parseFlags(int argc, char** argv) {
    std::map<std::string, std::string> flags;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) {
            std::string key = a.substr(2);
            std::string val = "true";
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0) {
                val = argv[++i];
            }
            flags[key] = val;
        }
    }
    return flags;
}

Args parseArgs(int argc, char** argv) {
    Args a;
    auto flags = parseFlags(argc, argv);
    auto get = [&](const char* k, std::string def) {
        auto it = flags.find(k);
        return it != flags.end() ? it->second : def;
    };
    a.input = get("input", "");
    a.output = get("output", "");
    a.effects = get("effects", a.effects);
    a.bufferSize = std::stoi(get("buffer-size", std::to_string(a.bufferSize)));
    a.statsOut = get("stats-out", "");
    a.benchmarkSizes = get("benchmark-sizes", "");

    a.lpCutoff = std::stod(get("lp-cutoff", std::to_string(a.lpCutoff)));
    a.lpOrder = std::stoi(get("lp-order", std::to_string(a.lpOrder)));
    a.hpCutoff = std::stod(get("hp-cutoff", std::to_string(a.hpCutoff)));
    a.hpOrder = std::stoi(get("hp-order", std::to_string(a.hpOrder)));

    a.delayMs = std::stod(get("delay-ms", std::to_string(a.delayMs)));
    a.delayFeedback = std::stof(get("delay-feedback", std::to_string(a.delayFeedback)));
    a.delayMix = std::stof(get("delay-mix", std::to_string(a.delayMix)));

    a.gateFrame = std::stoi(get("gate-frame", std::to_string(a.gateFrame)));
    a.gateThresholdDb = std::stod(get("gate-threshold-db", std::to_string(a.gateThresholdDb)));
    a.gateFloorDb = std::stod(get("gate-floor-db", std::to_string(a.gateFloorDb)));
    a.gateKneeDb = std::stod(get("gate-knee-db", std::to_string(a.gateKneeDb)));
    return a;
}

std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::vector<std::unique_ptr<Effect>> buildChain(const std::vector<std::string>& names, double sampleRate, const Args& a) {
    std::vector<std::unique_ptr<Effect>> chain;
    for (const auto& n : names) {
        if (n == "lowpass") {
            chain.push_back(std::make_unique<ButterworthFilter>(FilterType::LowPass, a.lpCutoff, sampleRate, a.lpOrder));
        } else if (n == "highpass") {
            chain.push_back(std::make_unique<ButterworthFilter>(FilterType::HighPass, a.hpCutoff, sampleRate, a.hpOrder));
        } else if (n == "delay") {
            chain.push_back(std::make_unique<DelayEffect>(a.delayMs, sampleRate, a.delayFeedback, a.delayMix));
        } else if (n == "noisegate") {
            chain.push_back(std::make_unique<SpectralNoiseGate>(a.gateFrame, sampleRate, a.gateThresholdDb, a.gateFloorDb, a.gateKneeDb));
        } else {
            throw std::runtime_error("Unknown effect: " + n);
        }
    }
    return chain;
}

// Processes one channel of audio through a per-effect chain in fixed-size
// buffers, exactly as a real-time block processor would. Returns the
// processed samples and records real measured per-buffer latency
// (wall-clock, buffer-in to buffer-out) into `stats`, keyed by effect name,
// plus a "pipeline_total" series for the whole chain.
std::vector<float> processChannelStreaming(const std::vector<float>& input,
                                            std::vector<std::unique_ptr<Effect>>& chain,
                                            int bufferSize,
                                            std::map<std::string, LatencyStats>& stats) {
    std::vector<float> output(input.size());
    std::vector<float> bufA(bufferSize), bufB(bufferSize);

    size_t pos = 0;
    while (pos < input.size()) {
        int n = (int)std::min((size_t)bufferSize, input.size() - pos);
        std::fill(bufA.begin(), bufA.end(), 0.0f);
        for (int i = 0; i < n; ++i) bufA[i] = input[pos + i];

        Stopwatch totalTimer;
        float* cur = bufA.data();
        float* nxt = bufB.data();
        for (auto& fx : chain) {
            Stopwatch fxTimer;
            fx->process(cur, nxt, bufferSize);
            stats[fx->name()].record(fxTimer.elapsedMicroseconds());
            std::swap(cur, nxt);
        }
        stats["pipeline_total"].record(totalTimer.elapsedMicroseconds());

        for (int i = 0; i < n; ++i) output[pos + i] = cur[i];
        pos += n;
    }
    return output;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

void writeStatsSection(std::ostream& os, const std::string& indent, const std::map<std::string, LatencyStats>& stats) {
    bool first = true;
    for (auto& [name, s] : stats) {
        if (!first) os << ",\n";
        first = false;
        os << indent << "\"" << jsonEscape(name) << "\": {\n"
           << indent << "  \"count\": " << s.count() << ",\n"
           << indent << "  \"min_us\": " << s.min() << ",\n"
           << indent << "  \"max_us\": " << s.max() << ",\n"
           << indent << "  \"mean_us\": " << s.mean() << ",\n"
           << indent << "  \"p50_us\": " << s.percentile(50) << ",\n"
           << indent << "  \"p95_us\": " << s.percentile(95) << ",\n"
           << indent << "  \"p99_us\": " << s.percentile(99) << "\n"
           << indent << "}";
    }
    os << "\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        Args a = parseArgs(argc, argv);
        if (a.input.empty() || a.output.empty()) {
            std::cerr << "Usage: swara_engine --input in.wav --output out.wav --effects lowpass,delay,noisegate "
                         "[--buffer-size 512] [--stats-out stats.json] [--benchmark-sizes 64,128,256,512,1024,2048]\n";
            return 1;
        }

        WavAudio in = readWav(a.input);
        auto effectNames = splitCsv(a.effects);

        WavAudio out;
        out.sampleRate = in.sampleRate;
        out.numChannels = in.numChannels;
        out.channels.resize(in.numChannels);

        std::map<std::string, LatencyStats> mainRunStats;
        for (uint16_t c = 0; c < in.numChannels; ++c) {
            auto chain = buildChain(effectNames, in.sampleRate, a);
            out.channels[c] = processChannelStreaming(in.channels[c], chain, a.bufferSize, mainRunStats);
        }

        writeWav(a.output, out);

        std::map<int, std::map<std::string, LatencyStats>> benchmarkResults;
        if (!a.benchmarkSizes.empty()) {
            for (const auto& tok : splitCsv(a.benchmarkSizes)) {
                int size = std::stoi(tok);
                std::map<std::string, LatencyStats> sizeStats;
                for (uint16_t c = 0; c < in.numChannels; ++c) {
                    auto chain = buildChain(effectNames, in.sampleRate, a);
                    processChannelStreaming(in.channels[c], chain, size, sizeStats);
                }
                benchmarkResults[size] = std::move(sizeStats);
            }
        }

        std::ostringstream json;
        json << "{\n";
        json << "  \"input\": \"" << jsonEscape(a.input) << "\",\n";
        json << "  \"output\": \"" << jsonEscape(a.output) << "\",\n";
        json << "  \"sample_rate\": " << in.sampleRate << ",\n";
        json << "  \"channels\": " << in.numChannels << ",\n";
        json << "  \"num_frames\": " << in.numFrames() << ",\n";
        json << "  \"effects\": \"" << jsonEscape(a.effects) << "\",\n";
        json << "  \"buffer_size\": " << a.bufferSize << ",\n";
        json << "  \"main_run_latency_us\": {\n";
        writeStatsSection(json, "    ", mainRunStats);
        json << "  }";
        if (!benchmarkResults.empty()) {
            json << ",\n  \"benchmark_by_buffer_size\": {\n";
            bool first = true;
            for (auto& [size, stats] : benchmarkResults) {
                if (!first) json << ",\n";
                first = false;
                json << "    \"" << size << "\": {\n";
                writeStatsSection(json, "      ", stats);
                json << "    }";
            }
            json << "\n  }\n";
        } else {
            json << "\n";
        }
        json << "}\n";

        std::cout << json.str();
        if (!a.statsOut.empty()) {
            std::ofstream f(a.statsOut);
            f << json.str();
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
