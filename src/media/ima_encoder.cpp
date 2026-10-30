#include "ima_encoder.h"

#include <algorithm>
#include <cstdlib>

#include "ima_decoder.h"

namespace dvr {

namespace {

constexpr int kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
constexpr int kIndexAdjust[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

}  // namespace

void ImaEncoder::encode(const int16_t *pcm, size_t count, std::vector<uint8_t> &out)
{
    pending_.insert(pending_.end(), pcm, pcm + count);
    size_t used = 0;
    const size_t block = ImaDecoder::kSamplesPerBlock;
    for (; used + block <= pending_.size(); used += block)
        encode_block(pending_.data() + used, out);
    pending_.erase(pending_.begin(), pending_.begin() + used);
}

// Block layout: predictor (s16 LE) = the first sample, step index, one zero byte, then the nibbles
// of the remaining samples, the earlier sample of each pair in the low nibble.
void ImaEncoder::encode_block(const int16_t *pcm, std::vector<uint8_t> &out)
{
    out.insert(out.end(), {0x00, 0x01, 0x52, 0x00});
    int predictor = pcm[0];
    out.push_back(predictor & 0xff);
    out.push_back((predictor >> 8) & 0xff);
    out.push_back(step_index_);
    out.push_back(0);

    uint8_t pair = 0;
    for (int i = 1; i < ImaDecoder::kSamplesPerBlock; i++) {
        int step = kStepTable[step_index_];
        int diff = pcm[i] - predictor;
        int nibble = 0;
        if (diff < 0) {
            nibble = 8;
            diff = -diff;
        }
        int delta = step >> 3;
        for (int bit = 4; bit >= 1; bit >>= 1) {
            if (diff >= step) {
                nibble |= bit;
                diff -= step;
                delta += step;
            }
            step >>= 1;
        }
        predictor = std::clamp(predictor + (nibble & 8 ? -delta : delta), -32768, 32767);
        step_index_ = std::clamp(step_index_ + kIndexAdjust[nibble & 7], 0, 88);
        if (i % 2 == 1)
            pair = nibble;
        else
            out.push_back(pair | (nibble << 4));
    }
}

}  // namespace dvr
