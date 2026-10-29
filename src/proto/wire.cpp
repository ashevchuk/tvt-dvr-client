#include "wire.h"

#include <algorithm>
#include <cstring>

namespace dvr::wire {

namespace {

constexpr char kMagic[4] = {'1', '1', '1', '1'};
constexpr char kFragmentMagic[4] = {'P', 'A', 'C', 'K'};
constexpr size_t kMessageHeaderBytes = 16;
constexpr size_t kFragmentHeaderBytes = 28;
constexpr size_t kMaxFrameBytes = 16 << 20;
constexpr size_t kNameFieldBytes = 36;
constexpr uint32_t kAllRecordTypes = 0xFFFFFFFF;
constexpr uint32_t kPlaybackRecordTypes = 0xF;

// Layout of the login reply: the device description starts 52 bytes in.
constexpr size_t kInfoAudioInputs = 53;
constexpr size_t kInfoVideoChannels = 80;
constexpr size_t kInfoName = 140;
constexpr size_t kInfoFirmware = 176;

uint32_t get32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void put32(std::vector<uint8_t> &out, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void put_words(std::vector<uint8_t> &out, std::initializer_list<uint32_t> words)
{
    for (uint32_t w : words)
        put32(out, w);
}

void put_text(std::vector<uint8_t> &out, const std::string &text, size_t field)
{
    for (size_t i = 0; i < field; i++)
        out.push_back(i < text.size() && i + 1 < field ? static_cast<uint8_t>(text[i]) : 0);
}

std::string get_text(const std::vector<uint8_t> &payload, size_t offset, size_t field)
{
    const char *p = reinterpret_cast<const char *>(payload.data() + offset);
    return std::string(p, strnlen(p, field));
}

}  // namespace

void Decoder::push(const uint8_t *data, size_t size)
{
    if (consumed_ > (64 << 10) && consumed_ * 2 > buffer_.size()) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + consumed_);
        consumed_ = 0;
    }
    buffer_.insert(buffer_.end(), data, data + size);
}

bool Decoder::next(Message &out)
{
    while (!failed_) {
        size_t available = buffer_.size() - consumed_;
        if (available < 8)
            return false;
        const uint8_t *p = buffer_.data() + consumed_;
        if (memcmp(p, kMagic, 4) != 0) {
            failed_ = true;
            return false;
        }
        size_t length = get32(p + 4);
        if (length > kMaxFrameBytes) {
            failed_ = true;
            return false;
        }
        if (available < 8 + length)
            return false;
        const uint8_t *body = p + 8;
        consumed_ += 8 + length;

        std::vector<uint8_t> whole;
        if (length >= 4 && memcmp(body, kFragmentMagic, 4) == 0) {
            if (length < kFragmentHeaderBytes) {
                failed_ = true;
                return false;
            }
            uint32_t count = get32(body + 8);
            uint32_t index = get32(body + 16);
            size_t chunk = std::min<size_t>(get32(body + 20), length - kFragmentHeaderBytes);
            if (index == 1)
                fragments_.clear();
            fragments_.insert(fragments_.end(), body + kFragmentHeaderBytes, body + kFragmentHeaderBytes + chunk);
            if (index != count)
                continue;
            whole.swap(fragments_);
            body = whole.data();
            length = whole.size();
        }
        if (length < kMessageHeaderBytes)
            continue;
        size_t payload = std::min<size_t>(get32(body + 12), length - kMessageHeaderBytes);
        out.cmd = get32(body);
        out.a = get32(body + 4);
        out.b = get32(body + 8);
        out.payload.assign(body + kMessageHeaderBytes, body + kMessageHeaderBytes + payload);
        return true;
    }
    return false;
}

std::vector<uint8_t> encode(uint32_t cmd, const std::vector<uint8_t> &payload)
{
    std::vector<uint8_t> out(kMagic, kMagic + 4);
    put32(out, static_cast<uint32_t>(kMessageHeaderBytes + payload.size()));
    put_words(out, {cmd, 0, 4, static_cast<uint32_t>(payload.size())});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<uint8_t> keepalive()
{
    std::vector<uint8_t> out(kMagic, kMagic + 4);
    put32(out, 0);
    return out;
}

std::vector<uint8_t> login(const std::string &user, const std::string &password)
{
    std::vector<uint8_t> payload;
    put_words(payload, {1, 0});
    put_text(payload, user, kNameFieldBytes);
    put_text(payload, password, kNameFieldBytes);
    put_text(payload, "", kNameFieldBytes);
    put_words(payload, {4});
    return encode(kCmdLogin, payload);
}

std::vector<uint8_t> live_streams(bool running, uint32_t main_mask, uint32_t sub_mask)
{
    if (main_mask == 0 && sub_mask == 0)
        return encode(kCmdStreamStop, std::vector<uint8_t>(36, 0));
    std::vector<uint8_t> payload;
    put_words(payload, {0, main_mask, 0, sub_mask, 0, 0, 0, main_mask | sub_mask, 0});
    return encode(running ? kCmdStreamSet : kCmdStreamStart, payload);
}

std::vector<uint8_t> search_days()
{
    return encode(kCmdSearch, std::vector<uint8_t>(44, 0));
}

std::vector<uint8_t> search_segments(int channel, uint32_t start, uint32_t stop)
{
    uint32_t mask = 1u << channel;
    std::vector<uint8_t> payload;
    put_words(payload, {1, 0, mask, 0, mask, 0, start, stop, 0, kAllRecordTypes, 0});
    return encode(kCmdSearch, payload);
}

std::vector<uint8_t> play_start(uint32_t session, int channel, uint32_t start, uint32_t stop, bool download)
{
    uint32_t mask = 1u << channel;
    std::vector<uint8_t> payload;
    put_words(payload, {0xFFFFFFFF, session, mask, 0, mask, 0, start, stop, download ? 1u : 0u, kPlaybackRecordTypes, 0});
    return encode(kCmdPlayStart, payload);
}

std::vector<uint8_t> play_ack(uint32_t session, uint32_t sequence)
{
    std::vector<uint8_t> payload;
    put_words(payload, {0, session, 0, 0, 0, 0, 0, 0, sequence, 0, 0});
    return encode(kCmdPlayAck, payload);
}

std::vector<uint8_t> play_stop(uint32_t session)
{
    std::vector<uint8_t> payload;
    put_words(payload, {0, session, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    return encode(kCmdPlayStop, payload);
}

bool parse_media(const Message &message, MediaFrame &out)
{
    if (message.cmd != kCmdMedia || message.payload.size() < kFrameHeaderBytes)
        return false;
    const uint8_t *p = message.payload.data();
    uint32_t kind = get32(p + 4);
    if (kind != 1 && kind != 2 && kind != 3 && kind != 4 && kind != 9)
        return false;
    out.kind = static_cast<Kind>(kind);
    out.key = get32(p) != 0;
    out.width = static_cast<int>(get32(p + 12));
    out.height = static_cast<int>(get32(p + 16));
    out.channel = static_cast<int>(get32(p + 24));
    out.stream = get32(p + 36) == static_cast<uint32_t>(Stream::Sub) ? Stream::Sub : Stream::Main;
    out.time_us = (static_cast<uint64_t>(get32(p + 48)) << 32) | get32(p + 44);
    out.data = p + kFrameHeaderBytes;
    out.size = std::min<size_t>(get32(p + 8), message.payload.size() - kFrameHeaderBytes);
    return true;
}

bool parse_login_reply(const Message &message, DeviceInfo &out)
{
    if (message.cmd != kCmdLoginOk || message.payload.size() < kInfoFirmware + kNameFieldBytes)
        return false;
    const uint8_t *p = message.payload.data();
    out.video_channels = p[kInfoVideoChannels] | (p[kInfoVideoChannels + 1] << 8);
    out.audio_inputs = p[kInfoAudioInputs];
    out.name = get_text(message.payload, kInfoName, kNameFieldBytes);
    out.firmware = get_text(message.payload, kInfoFirmware, kNameFieldBytes);
    return true;
}

std::vector<uint32_t> parse_days(const Message &message)
{
    std::vector<uint32_t> days;
    for (size_t off = 16; off + 8 <= message.payload.size(); off += 8)
        days.push_back(get32(message.payload.data() + off + 4));
    return days;
}

std::vector<SegmentTimes> parse_segments(const Message &message)
{
    std::vector<SegmentTimes> segments;
    for (size_t off = 16; off + 12 <= message.payload.size(); off += 12)
        segments.push_back({get32(message.payload.data() + off + 4), get32(message.payload.data() + off + 8)});
    return segments;
}

}  // namespace dvr::wire
