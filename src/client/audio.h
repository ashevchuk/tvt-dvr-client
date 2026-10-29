#pragma once

#include "common.h"
#include "media/ima_decoder.h"

static const int AUDIO_SAMPLE_RATE = dvr::kAudioSampleRate;

static const size_t MAX_BUFFERED_SAMPLES = AUDIO_SAMPLE_RATE;

// The device delivers audio in 200 ms bursts; playing from an empty buffer would glitch on every burst.
static const size_t PREBUFFER_SAMPLES = AUDIO_SAMPLE_RATE * 3 / 10;

extern int g_audio_inputs;
extern std::atomic<int> g_audio_camera;
extern std::atomic<unsigned> g_audio_underruns;

// Decodes the device's audio and hands PCM to the raylib audio thread.
class AudioPlayer {
public:
    AudioPlayer()
    {
        if (!decoder_.ok()) {
            fprintf(stderr, "cannot open the ADPCM decoder\n");
            exit(1);
        }
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        primed_ = false;
    }

    // `units` is the device's audio data, see dvr::ImaDecoder.
    void feed(const uint8_t *units, size_t size)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        decoded_.clear();
        decoder_.decode(units, size, decoded_);
        samples_.insert(samples_.end(), decoded_.begin(), decoded_.end());
        while (samples_.size() > MAX_BUFFERED_SAMPLES)
            samples_.pop_front();
    }

    void pull(int16_t *out, unsigned frames)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!primed_ && samples_.size() >= PREBUFFER_SAMPLES)
            primed_ = true;
        if (!primed_) {
            memset(out, 0, frames * sizeof(int16_t));
            return;
        }
        unsigned n = std::min<size_t>(frames, samples_.size());
        std::copy(samples_.begin(), samples_.begin() + n, out);
        samples_.erase(samples_.begin(), samples_.begin() + n);
        if (n < frames) {
            memset(out + n, 0, (frames - n) * sizeof(int16_t));
            primed_ = false;
            g_audio_underruns++;
        }
    }

private:
    dvr::ImaDecoder decoder_;
    std::vector<int16_t> decoded_;
    std::mutex mutex_;
    std::deque<int16_t> samples_;
    bool primed_ = false;
};

extern std::unique_ptr<AudioPlayer> g_audio;

void audio_callback(void *buffer, unsigned frames);
