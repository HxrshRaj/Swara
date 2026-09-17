#include "wav_io.h"
#include <fstream>
#include <stdexcept>
#include <cstring>

namespace swara {

namespace {

uint32_t readU32LE(std::ifstream& f) {
    unsigned char b[4];
    f.read(reinterpret_cast<char*>(b), 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint16_t readU16LE(std::ifstream& f) {
    unsigned char b[2];
    f.read(reinterpret_cast<char*>(b), 2);
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

void writeU32LE(std::ofstream& f, uint32_t v) {
    unsigned char b[4] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
                            (unsigned char)((v >> 16) & 0xFF), (unsigned char)((v >> 24) & 0xFF) };
    f.write(reinterpret_cast<char*>(b), 4);
}

void writeU16LE(std::ofstream& f, uint16_t v) {
    unsigned char b[2] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF) };
    f.write(reinterpret_cast<char*>(b), 2);
}

} // namespace

WavAudio readWav(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open WAV file: " + path);

    char riff[4];
    f.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) throw std::runtime_error("Not a RIFF file: " + path);
    readU32LE(f); // chunk size, ignored
    char wave[4];
    f.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) throw std::runtime_error("Not a WAVE file: " + path);

    WavAudio audio;
    uint16_t audioFormat = 1;
    bool haveFmt = false;
    std::vector<char> dataBytes;

    while (f && !f.eof()) {
        char chunkId[4];
        f.read(chunkId, 4);
        if (f.gcount() < 4) break;
        uint32_t chunkSize = readU32LE(f);
        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            std::streampos chunkStart = f.tellg();
            audioFormat = readU16LE(f);
            audio.numChannels = readU16LE(f);
            audio.sampleRate = readU32LE(f);
            readU32LE(f); // byte rate
            readU16LE(f); // block align
            audio.bitsPerSample = readU16LE(f);
            f.seekg(chunkStart + std::streamoff(chunkSize));
            haveFmt = true;
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            dataBytes.resize(chunkSize);
            f.read(dataBytes.data(), chunkSize);
        } else {
            f.seekg(chunkSize, std::ios::cur);
        }
        if (chunkSize % 2 == 1) f.seekg(1, std::ios::cur); // pad byte
    }

    if (!haveFmt) throw std::runtime_error("WAV missing fmt chunk: " + path);
    if (audioFormat != 1) throw std::runtime_error("Only PCM WAV is supported (format=" + std::to_string(audioFormat) + ")");
    if (audio.numChannels == 0) throw std::runtime_error("WAV has zero channels");

    int bytesPerSample = audio.bitsPerSample / 8;
    size_t frameSize = (size_t)bytesPerSample * audio.numChannels;
    size_t numFrames = frameSize > 0 ? dataBytes.size() / frameSize : 0;

    audio.channels.assign(audio.numChannels, std::vector<float>(numFrames));

    const unsigned char* p = reinterpret_cast<const unsigned char*>(dataBytes.data());
    for (size_t n = 0; n < numFrames; ++n) {
        for (uint16_t c = 0; c < audio.numChannels; ++c) {
            const unsigned char* sp = p + (n * frameSize) + (size_t)c * bytesPerSample;
            float sample = 0.0f;
            if (audio.bitsPerSample == 16) {
                int16_t v = (int16_t)(sp[0] | (sp[1] << 8));
                sample = v / 32768.0f;
            } else if (audio.bitsPerSample == 24) {
                int32_t v = (int32_t)(sp[0] | (sp[1] << 8) | (sp[2] << 16));
                if (v & 0x800000) v |= (int32_t)0xFF000000; // sign extend
                sample = v / 8388608.0f;
            } else if (audio.bitsPerSample == 8) {
                sample = (sp[0] - 128) / 128.0f;
            } else if (audio.bitsPerSample == 32) {
                int32_t v = (int32_t)(sp[0] | (sp[1] << 8) | (sp[2] << 16) | (sp[3] << 24));
                sample = v / 2147483648.0f;
            } else {
                throw std::runtime_error("Unsupported bits per sample: " + std::to_string(audio.bitsPerSample));
            }
            audio.channels[c][n] = sample;
        }
    }

    return audio;
}

void writeWav(const std::string& path, const WavAudio& audio) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open WAV file for writing: " + path);

    uint16_t numChannels = audio.numChannels;
    uint32_t sampleRate = audio.sampleRate;
    uint16_t bitsPerSample = 16;
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    uint32_t byteRate = sampleRate * blockAlign;
    size_t numFrames = audio.numFrames();
    uint32_t dataSize = (uint32_t)(numFrames * blockAlign);

    f.write("RIFF", 4);
    writeU32LE(f, 36 + dataSize);
    f.write("WAVE", 4);

    f.write("fmt ", 4);
    writeU32LE(f, 16);
    writeU16LE(f, 1); // PCM
    writeU16LE(f, numChannels);
    writeU32LE(f, sampleRate);
    writeU32LE(f, byteRate);
    writeU16LE(f, blockAlign);
    writeU16LE(f, bitsPerSample);

    f.write("data", 4);
    writeU32LE(f, dataSize);

    for (size_t n = 0; n < numFrames; ++n) {
        for (uint16_t c = 0; c < numChannels; ++c) {
            float s = audio.channels[c][n];
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            int16_t v = (int16_t)(s * 32767.0f);
            unsigned char b[2] = { (unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF) };
            f.write(reinterpret_cast<char*>(b), 2);
        }
    }
}

} // namespace swara
