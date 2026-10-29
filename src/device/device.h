#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// What the client needs from a DVR, independent of how the device is reached: through the vendor
// SDK or through the native protocol implementation.
namespace dvr {

struct DeviceInfo {
    std::string name;
    std::string firmware;
    int video_channels = 0;
    int audio_inputs = 0;
};

struct Frame {
    enum class Kind { Video, Audio, End };
    Kind kind = Kind::Video;
    int channel = 0;
    bool key = false;
    uint64_t time_us = 0;       // microseconds since the Unix epoch
    // Video: Annex-B H.264. Audio: a series of 168-byte units, see ima_decoder.h.
    // Only valid during the call.
    const uint8_t *data = nullptr;
    size_t size = 0;
};

using FrameHandler = std::function<void(const Frame &)>;

struct Segment {
    time_t start;
    time_t stop;
};

// A running live stream or playback of one channel. Frames are delivered on a thread of the
// device; destroying the session stops them.
class MediaSession {
public:
    virtual ~MediaSession() = default;
    virtual bool ok() const = 0;
    // Playback only.
    virtual void set_paused(bool paused) = 0;
    virtual void set_speed(int speed) = 0;      // 1, 2 or 4
};

class Download {
public:
    virtual ~Download() = default;
    // 0 to 99 while running, 100 when the file is complete, -1 when it failed.
    virtual int progress() = 0;
    virtual std::string error() const = 0;
};

class Device {
public:
    virtual ~Device() = default;
    virtual const DeviceInfo &info() const = 0;
    // File extension of the recordings that download() writes.
    virtual const char *recording_extension() const = 0;

    virtual std::unique_ptr<MediaSession> open_live(int channel, bool main_stream, FrameHandler handler) = 0;
    // Plays recordings of one channel between two times. The first frames can predate `start`.
    virtual std::unique_ptr<MediaSession> open_playback(int channel, time_t start, time_t stop, FrameHandler handler) = 0;

    // Start of every day with recordings (the device may list a day twice).
    virtual std::vector<time_t> recorded_days() = 0;
    virtual std::vector<Segment> segments(int channel, time_t day_start) = 0;
    virtual std::unique_ptr<Download> download(int channel, time_t start, time_t stop, const std::string &path) = 0;
};

std::unique_ptr<Device> connect_native(const std::string &host, int port, const std::string &user,
                                       const std::string &password, std::string &error);
#ifdef DVR_WITH_SDK
std::unique_ptr<Device> connect_sdk(const std::string &host, int port, const std::string &user,
                                    const std::string &password, std::string &error);
#endif

}  // namespace dvr
