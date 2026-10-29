#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" {
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
}

namespace dvr {

constexpr int kAudioSampleRate = 8000;

// Decodes the DVR's audio: a series of 168-byte units, each a 4-byte header followed by one
// 164-byte IMA ADPCM block (321 samples, 8 kHz mono). A trailing partial unit is ignored.
class ImaDecoder {
public:
    ImaDecoder();
    ~ImaDecoder();
    ImaDecoder(const ImaDecoder &) = delete;
    ImaDecoder &operator=(const ImaDecoder &) = delete;

    bool ok() const { return ctx_ != nullptr; }
    // Appends the decoded samples to `out`.
    void decode(const uint8_t *units, size_t size, std::vector<int16_t> &out);

    static constexpr size_t kUnitBytes = 168;
    static constexpr size_t kHeaderBytes = 4;
    static constexpr size_t kBlockBytes = kUnitBytes - kHeaderBytes;
    static constexpr int kSamplesPerBlock = (kBlockBytes - 4) * 2 + 1;

private:
    AVCodecContext *ctx_ = nullptr;
    AVPacket *packet_ = nullptr;
    AVFrame *frame_ = nullptr;
    std::vector<uint8_t> block_;
};

}  // namespace dvr
