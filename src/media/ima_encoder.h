#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dvr {

// The inverse of ImaDecoder: turns 8 kHz mono PCM into the DVR's 168-byte units. Samples that do
// not fill a block are kept for the next call. Not libavcodec's encoder: that one only writes
// power-of-two block sizes, the DVR uses 164 bytes.
class ImaEncoder {
public:
    // Appends one unit to `out` for every complete block of samples.
    void encode(const int16_t *pcm, size_t count, std::vector<uint8_t> &out);

private:
    void encode_block(const int16_t *pcm, std::vector<uint8_t> &out);

    std::vector<int16_t> pending_;
    int step_index_ = 0;
};

}  // namespace dvr
