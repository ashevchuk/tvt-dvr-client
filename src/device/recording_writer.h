#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "media/ima_decoder.h"

extern "C" {
struct AVFormatContext;
struct AVStream;
}

namespace dvr {

// Writes the frames of a recording into a Matroska file: the H.264 as it comes, the audio decoded
// to 16-bit PCM, timestamps from the device clock.
class RecordingWriter {
public:
    static std::unique_ptr<RecordingWriter> create(const std::string &path, bool with_audio, std::string &error);
    ~RecordingWriter();

    // Frames before the first key frame are dropped.
    void add_video(const uint8_t *data, size_t size, bool key, int width, int height, uint64_t time_us);
    void add_audio(const uint8_t *units, size_t size, uint64_t time_us);
    // Writes the file's index. False if no video was written or the output failed.
    bool finish(std::string &error);

    uint64_t video_frames() const { return video_frames_; }

private:
    RecordingWriter() = default;
    bool start(const uint8_t *key_frame, size_t size, int width, int height, uint64_t time_us);
    void write(AVStream *stream, std::vector<uint8_t> &data, int64_t pts_us, int64_t duration_us, bool key);

    AVFormatContext *format_ = nullptr;
    AVStream *video_ = nullptr;
    AVStream *audio_ = nullptr;
    std::string path_;
    bool with_audio_ = false;
    bool started_ = false;
    bool failed_ = false;
    bool finished_ = false;
    uint64_t first_us_ = 0;
    int64_t last_video_pts_ = -1;
    int64_t last_audio_pts_ = -1;
    uint64_t video_frames_ = 0;
    ImaDecoder decoder_;
    std::vector<int16_t> pcm_;
};

}  // namespace dvr
