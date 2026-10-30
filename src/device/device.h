#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// What the client needs from a DVR, independent of how the device is reached.
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

// An open talk-back (intercom) channel to the device. Destroying it ends the talk session.
class TalkSession {
public:
    virtual ~TalkSession() = default;
    // Sends whole 168-byte IMA units (see ima_decoder.h). The device plays them as they arrive,
    // so the caller supplies them in real time. False once the connection is lost.
    virtual bool send(const uint8_t *units, size_t size) = 0;
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

    // Opens a talk-back channel; nullptr if the device cannot be reached or refuses it.
    virtual std::unique_ptr<TalkSession> open_talk() = 0;

    // Start of every day with recordings (the device may list a day twice).
    virtual std::vector<time_t> recorded_days() = 0;
    virtual std::vector<Segment> segments(int channel, time_t day_start) = 0;
    virtual std::unique_ptr<Download> download(int channel, time_t start, time_t stop, const std::string &path) = 0;

    // False once the connection kept for searches, or the one carrying live streams, has been lost.
    // Never blocks.
    virtual bool connected() = 0;
    // Reopens the connections that were lost and resumes the live streams that were running. Blocks
    // while it tries; returns false if the device cannot be reached. Playbacks and downloads
    // have their own connections and are not resumed.
    virtual bool reconnect(std::string &error) = 0;
};

std::unique_ptr<Device> connect_native(const std::string &host, int port, const std::string &user,
                                       const std::string &password, std::string &error);

}  // namespace dvr
