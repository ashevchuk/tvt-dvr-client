#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The TVT DVR wire protocol on TCP 6036, without any input or output. See docs/protocol.md.
namespace dvr::wire {

constexpr size_t kGreetingBytes = 64;
constexpr size_t kFrameHeaderBytes = 60;
constexpr int kKeepaliveSeconds = 8;

constexpr uint32_t kCmdLogin = 0x0101;
constexpr uint32_t kCmdStreamStart = 0x0201;
constexpr uint32_t kCmdStreamSet = 0x0202;
constexpr uint32_t kCmdStreamStop = 0x0203;
constexpr uint32_t kCmdSearch = 0x0501;
constexpr uint32_t kCmdPlayStart = 0x0502;
constexpr uint32_t kCmdPlayStop = 0x0507;
constexpr uint32_t kCmdPlayAck = 0x050B;
constexpr uint32_t kCmdLoginOk = 0x00010001;
constexpr uint32_t kCmdSearchReply = 0x00050001;
constexpr uint32_t kCmdMedia = 0x0A000001;

constexpr uint32_t kFirstPlaybackSession = 0x0A;
constexpr uint32_t kDownloadSession = 0x64;

enum class Kind : uint32_t { Video = 1, Audio = 2, VideoFormat = 3, AudioFormat = 4, End = 9 };
enum class Stream : int { Main = 0x20, Sub = 0x40 };

struct Message {
    uint32_t cmd = 0;
    uint32_t a = 0;
    uint32_t b = 0;
    std::vector<uint8_t> payload;
};

// Splits the byte stream after the greeting into messages and reassembles PACK fragments.
class Decoder {
public:
    void push(const uint8_t *data, size_t size);
    // Returns true when a complete message is ready. Keepalives and unusable frames are skipped.
    bool next(Message &out);
    bool failed() const { return failed_; }

private:
    std::vector<uint8_t> buffer_;
    size_t consumed_ = 0;
    std::vector<uint8_t> fragments_;
    bool failed_ = false;
};

std::vector<uint8_t> encode(uint32_t cmd, const std::vector<uint8_t> &payload = {});
std::vector<uint8_t> keepalive();

std::vector<uint8_t> login(const std::string &user, const std::string &password);
// Selects the complete set of live channels; the first call starts streaming, later ones replace
// the set. Both masks empty stops all live streams.
std::vector<uint8_t> live_streams(bool running, uint32_t main_mask, uint32_t sub_mask);
std::vector<uint8_t> search_days();
std::vector<uint8_t> search_segments(int channel, uint32_t start, uint32_t stop);
std::vector<uint8_t> play_start(uint32_t session, int channel, uint32_t start, uint32_t stop, bool download);
std::vector<uint8_t> play_ack(uint32_t session, uint32_t sequence);
std::vector<uint8_t> play_stop(uint32_t session);

struct MediaFrame {
    Kind kind = Kind::Video;
    bool key = false;
    int channel = 0;
    Stream stream = Stream::Main;
    int width = 0;
    int height = 0;
    uint64_t time_us = 0;      // microseconds since the Unix epoch, UTC
    const uint8_t *data = nullptr;
    size_t size = 0;
};

// `data` of the result points into message.payload.
bool parse_media(const Message &message, MediaFrame &out);

struct DeviceInfo {
    std::string name;
    std::string firmware;
    int video_channels = 0;
    int audio_inputs = 0;
};

bool parse_login_reply(const Message &message, DeviceInfo &out);

struct SegmentTimes {
    uint32_t start;
    uint32_t stop;
};

// Unix times of the start of each recorded day.
std::vector<uint32_t> parse_days(const Message &message);
std::vector<SegmentTimes> parse_segments(const Message &message);

}  // namespace dvr::wire
