#include <cstdio>
#include <cstring>

#include "proto/wire.h"

using namespace dvr::wire;

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static void put32(std::vector<uint8_t> &v, uint32_t x)
{
    for (int i = 0; i < 4; i++)
        v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

static std::vector<uint8_t> frame(const std::vector<uint8_t> &body)
{
    std::vector<uint8_t> out = {'1', '1', '1', '1'};
    put32(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

static std::vector<uint8_t> fragment(uint32_t count, uint32_t total, uint32_t index, const std::vector<uint8_t> &chunk)
{
    std::vector<uint8_t> body = {'P', 'A', 'C', 'K'};
    for (uint32_t w : {1u, count, total, index, static_cast<uint32_t>(chunk.size()), 0u})
        put32(body, w);
    body.insert(body.end(), chunk.begin(), chunk.end());
    return frame(body);
}

static std::vector<uint8_t> message_body(uint32_t cmd, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> body;
    for (uint32_t w : {cmd, 0xffffffffu, 4u, static_cast<uint32_t>(payload.size())})
        put32(body, w);
    body.insert(body.end(), payload.begin(), payload.end());
    return body;
}

static void test_encode_decode_roundtrip()
{
    std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
    Decoder d;
    auto bytes = encode(0x1234, payload);
    d.push(bytes.data(), bytes.size());
    Message m;
    CHECK(d.next(m));
    CHECK(m.cmd == 0x1234 && m.payload == payload && m.b == 4);
    CHECK(!d.next(m));
}

static void test_byte_by_byte_and_keepalive()
{
    Decoder d;
    auto ka = keepalive();
    auto msg = encode(0x0201, std::vector<uint8_t>(36, 7));
    std::vector<uint8_t> stream = ka;
    stream.insert(stream.end(), msg.begin(), msg.end());
    stream.insert(stream.end(), ka.begin(), ka.end());
    int messages = 0;
    for (uint8_t byte : stream) {
        d.push(&byte, 1);
        Message m;
        while (d.next(m)) {
            CHECK(m.cmd == 0x0201 && m.payload.size() == 36);
            messages++;
        }
    }
    CHECK(messages == 1);
    CHECK(!d.failed());
}

static void test_fragment_reassembly()
{
    std::vector<uint8_t> payload(25000);
    for (size_t i = 0; i < payload.size(); i++)
        payload[i] = static_cast<uint8_t>(i * 7);
    auto whole = message_body(kCmdMedia, payload);
    std::vector<uint8_t> stream;
    size_t chunk = 10240, index = 1, count = (whole.size() + chunk - 1) / chunk;
    for (size_t off = 0; off < whole.size(); off += chunk, index++) {
        std::vector<uint8_t> part(whole.begin() + off, whole.begin() + std::min(whole.size(), off + chunk));
        auto f = fragment(static_cast<uint32_t>(count), static_cast<uint32_t>(whole.size()), static_cast<uint32_t>(index), part);
        stream.insert(stream.end(), f.begin(), f.end());
    }
    Decoder d;
    d.push(stream.data(), stream.size());
    Message m;
    CHECK(count == 3);
    CHECK(d.next(m));
    CHECK(m.cmd == kCmdMedia && m.payload == payload);
    CHECK(!d.next(m));
}

static void test_fragments_interleaved_with_plain_messages()
{
    auto a = message_body(0xaa, {1, 2});
    auto b = message_body(0xbb, {3});
    auto stream = fragment(2, 0, 1, std::vector<uint8_t>(a.begin(), a.begin() + 10));
    auto plain = frame(b);
    auto second = fragment(2, 0, 2, std::vector<uint8_t>(a.begin() + 10, a.end()));
    stream.insert(stream.end(), plain.begin(), plain.end());
    stream.insert(stream.end(), second.begin(), second.end());
    Decoder d;
    d.push(stream.data(), stream.size());
    Message m;
    CHECK(d.next(m) && m.cmd == 0xbb);
    CHECK(d.next(m) && m.cmd == 0xaa && m.payload.size() == 2);
}

static void test_garbage_marks_decoder_failed()
{
    Decoder d;
    const uint8_t junk[] = {'X', 'Y', 'Z', 'W', 0, 0, 0, 0};
    d.push(junk, sizeof(junk));
    Message m;
    CHECK(!d.next(m));
    CHECK(d.failed());
}

static void test_login_layout()
{
    auto bytes = login("admin", "secret");
    CHECK(bytes.size() == 144);
    CHECK(memcmp(bytes.data(), "1111", 4) == 0);
    CHECK(bytes[4] == 136);
    CHECK(bytes[8] == 0x01 && bytes[9] == 0x01);
    CHECK(bytes[20] == 120);
    CHECK(memcmp(&bytes[32], "admin", 6) == 0);
    CHECK(memcmp(&bytes[68], "secret", 7) == 0);
    CHECK(bytes[140] == 4);
}

static void test_live_stream_requests()
{
    auto start = live_streams(false, 0x1, 0);
    CHECK(start.size() == 8 + 16 + 36);
    CHECK(start[8] == 0x01 && start[9] == 0x02);
    CHECK(start[28] == 1 && start[36] == 0 && start[52] == 1);   // main mask, sub mask, union
    auto set = live_streams(true, 0x1, 0x6);
    CHECK(set[8] == 0x02 && set[9] == 0x02);
    CHECK(set[28] == 1 && set[36] == 6 && set[52] == 7);
    auto stop = live_streams(true, 0, 0);
    CHECK(stop[8] == 0x03 && stop[9] == 0x02);
}

static void test_playback_requests()
{
    auto start = play_start(0x0b, 5, 1000, 2000, false);
    CHECK(start.size() == 8 + 16 + 44);
    CHECK(start[9] == 0x05 && start[8] == 0x02);
    CHECK(start[24] == 0xff && start[28] == 0x0b);        // w0 = ffffffff, w1 = session
    CHECK(start[32] == 0x20 && start[40] == 0x20);        // channel mask in w2 and w4
    CHECK(start[48] == (1000 & 0xff) && start[52] == (2000 & 0xff));
    CHECK(start[56] == 0);                                // not a download
    CHECK(play_start(0x64, 0, 1, 2, true)[56] == 1);
    auto ack = play_ack(0x0a, 300);
    CHECK(ack[8] == 0x0b && ack[9] == 0x05 && ack[28] == 0x0a);
    CHECK(ack[24 + 32] == (300 & 0xff) && ack[24 + 33] == (300 >> 8));
    CHECK(play_stop(0x0c)[8] == 0x07);
}

// A real video-format frame header captured from a device, with a made-up payload after it.
static void test_real_media_header()
{
    const uint32_t words[15] = {0x00000000, 0x00000003, 0x00000028, 0x000003a0, 0x00000240, 0x01c82af0, 0x00000002,
                                0x121c0300, 0x00000000, 0x00000040, 0x00000000, 0x2ac8f07d, 0x00065efc, 0x500bc85e,
                                0x00000108};
    std::vector<uint8_t> payload;
    for (uint32_t w : words)
        put32(payload, w);
    payload.resize(kFrameHeaderBytes + 40, 0xAB);
    Message m;
    m.cmd = kCmdMedia;
    m.payload = payload;
    MediaFrame f;
    CHECK(parse_media(m, f));
    CHECK(f.kind == Kind::VideoFormat && !f.key);
    CHECK(f.width == 928 && f.height == 576);
    CHECK(f.channel == 2 && f.stream == Stream::Sub);
    CHECK(f.time_us == 0x00065efc2ac8f07dULL);
    CHECK(f.size == 40 && f.data[0] == 0xAB);
    CHECK(f.time_us / 1000000 > 1790000000ULL && f.time_us / 1000000 < 1800000000ULL);   // October 2026
}

static void test_media_rejects_short_or_unknown()
{
    Message m;
    m.cmd = kCmdMedia;
    m.payload.assign(kFrameHeaderBytes - 1, 0);
    MediaFrame f;
    CHECK(!parse_media(m, f));
    m.payload.assign(kFrameHeaderBytes, 0);
    CHECK(!parse_media(m, f));                 // kind 0 is not a known kind
    m.cmd = 0x1234;
    m.payload[4] = 1;
    CHECK(!parse_media(m, f));
}

static void test_login_reply_and_search_replies()
{
    Message m;
    m.cmd = kCmdLoginOk;
    m.payload.assign(348, 0);
    m.payload[53] = 4;
    m.payload[80] = 16;
    memcpy(&m.payload[140], "EDVR0", 5);
    memcpy(&m.payload[176], "3.3.0.P-3531-00", 15);
    DeviceInfo info;
    CHECK(parse_login_reply(m, info));
    CHECK(info.video_channels == 16 && info.audio_inputs == 4);
    CHECK(info.name == "EDVR0" && info.firmware == "3.3.0.P-3531-00");
    m.payload.resize(100);
    CHECK(!parse_login_reply(m, info));

    Message days;
    days.payload.assign(16, 0);
    for (uint32_t day : {1000u, 2000u}) {
        put32(days.payload, 0x07ea0a10);
        put32(days.payload, day);
    }
    auto parsed = parse_days(days);
    CHECK(parsed.size() == 2 && parsed[0] == 1000 && parsed[1] == 2000);

    Message segs;
    segs.payload.assign(16, 0);
    for (uint32_t i = 0; i < 3; i++) {
        put32(segs.payload, 0);
        put32(segs.payload, 100 + i * 10);
        put32(segs.payload, 105 + i * 10);
    }
    auto s = parse_segments(segs);
    CHECK(s.size() == 3 && s[2].start == 120 && s[2].stop == 125);
}

int main()
{
    test_encode_decode_roundtrip();
    test_byte_by_byte_and_keepalive();
    test_fragment_reassembly();
    test_fragments_interleaved_with_plain_messages();
    test_garbage_marks_decoder_failed();
    test_login_layout();
    test_live_stream_requests();
    test_playback_requests();
    test_real_media_header();
    test_media_rejects_short_or_unknown();
    test_login_reply_and_search_replies();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all wire tests passed\n");
    return 0;
}
