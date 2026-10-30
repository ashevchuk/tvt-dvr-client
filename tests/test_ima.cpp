#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "media/ima_decoder.h"
#include "media/ima_encoder.h"

using dvr::ImaDecoder;
using dvr::ImaEncoder;

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

// Units of the shape the device sends: header 00 01 52 00, then a block with predictor, step index,
// padding and 160 bytes of nibbles. Every nibble is zero, so the decoder only ever steps by the
// smallest amount and the output stays close to the predictor.
static std::vector<uint8_t> make_units(int count, int16_t predictor)
{
    std::vector<uint8_t> out;
    for (int i = 0; i < count; i++) {
        out.insert(out.end(), {0x00, 0x01, 0x52, 0x00});
        out.push_back(static_cast<uint8_t>(predictor & 0xff));
        out.push_back(static_cast<uint8_t>((predictor >> 8) & 0xff));
        out.push_back(0);   // step index
        out.push_back(0);
        out.insert(out.end(), 160, 0);
    }
    return out;
}

int main()
{
    ImaDecoder decoder;
    CHECK(decoder.ok());

    std::vector<int16_t> pcm;
    auto five = make_units(5, 1000);
    CHECK(five.size() == 840);
    decoder.decode(five.data(), five.size(), pcm);
    CHECK(pcm.size() == 5 * ImaDecoder::kSamplesPerBlock);       // 5 * 321, nothing lost to the headers
    CHECK(ImaDecoder::kSamplesPerBlock == 321);
    CHECK(pcm.front() == 1000);                                  // the block header's predictor is the first sample
    for (int16_t s : pcm)
        CHECK(s > 900 && s < 1100);

    // A trailing partial unit (the SDK delivers 4 bytes after the last unit) must be ignored.
    std::vector<int16_t> padded;
    auto with_tail = make_units(5, -500);
    with_tail.insert(with_tail.end(), 4, 0xEE);
    decoder.decode(with_tail.data(), with_tail.size(), padded);
    CHECK(padded.size() == 5 * 321);
    CHECK(padded.front() == -500);

    std::vector<int16_t> none;
    decoder.decode(five.data(), 100, none);                      // shorter than a unit
    CHECK(none.empty());

    // Round trip: a 440 Hz tone encoded in chunks of 1600 samples (the microphone's 200 ms) comes back
    // from the decoder close to the original; the leftover of each chunk carries over to the next.
    ImaEncoder encoder;
    std::vector<int16_t> tone(8000);
    for (size_t i = 0; i < tone.size(); i++)
        tone[i] = static_cast<int16_t>(8000 * sin(2 * M_PI * 440 * i / dvr::kAudioSampleRate));
    std::vector<uint8_t> units;
    for (size_t off = 0; off < tone.size(); off += 1600)
        encoder.encode(tone.data() + off, 1600, units);
    const size_t blocks = tone.size() / ImaDecoder::kSamplesPerBlock;          // 24 whole blocks in 8000 samples
    CHECK(units.size() == blocks * ImaDecoder::kUnitBytes);
    for (size_t off = 0; off < units.size(); off += ImaDecoder::kUnitBytes)
        CHECK(memcmp(units.data() + off, "\x00\x01\x52\x00", 4) == 0);
    std::vector<int16_t> decoded;
    decoder.decode(units.data(), units.size(), decoded);
    CHECK(decoded.size() == blocks * ImaDecoder::kSamplesPerBlock);
    int worst = 0;
    // The first block starts at the smallest step and cannot follow a loud tone; later blocks carry
    // the adapted step index in their header.
    for (size_t i = ImaDecoder::kSamplesPerBlock; i < decoded.size(); i++)
        worst = std::max(worst, std::abs(decoded[i] - tone[i]));
    CHECK(worst < 800);                                          // 10 % of the amplitude; ADPCM is lossy

    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all ima tests passed\n");
    return 0;
}
