#pragma once

#include "common.h"

static const unsigned AUDIO_WRAPPER_BYTES = 4;

static const int AUDIO_SAMPLE_RATE = 8000;

// The device reports nBlockAlign=1 in its format frame; the real IMA block
// size (4-byte header + 164 data bytes) was determined from the stream itself.
static const int IMA_BLOCK_BYTES = 168;

static const size_t MAX_BUFFERED_SAMPLES = AUDIO_SAMPLE_RATE;

// The device delivers audio in 200 ms bursts; playing from an empty buffer would glitch on every burst.
static const size_t PREBUFFER_SAMPLES = AUDIO_SAMPLE_RATE * 3 / 10;

extern int g_audio_inputs;
extern std::atomic<int> g_audio_camera;
extern std::atomic<unsigned> g_audio_underruns;

// Decodes the device's IMA ADPCM audio and hands PCM to the raylib audio thread.
class AudioPlayer {
public:
    AudioPlayer()
    {
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_ADPCM_IMA_WAV);
        ctx_ = avcodec_alloc_context3(codec);
        ctx_->sample_rate = AUDIO_SAMPLE_RATE;
        ctx_->block_align = IMA_BLOCK_BYTES;
        ctx_->bits_per_coded_sample = 4;
        av_channel_layout_default(&ctx_->ch_layout, 1);
        if (avcodec_open2(ctx_, codec, nullptr) < 0) {
            fprintf(stderr, "cannot open ADPCM decoder\n");
            exit(1);
        }
        pkt_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        block_.resize(IMA_BLOCK_BYTES + AV_INPUT_BUFFER_PADDING_SIZE);
    }

    ~AudioPlayer()
    {
        av_frame_free(&frame_);
        av_packet_free(&pkt_);
        avcodec_free_context(&ctx_);
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
        primed_ = false;
    }

    // data is the device payload after the 4-byte wrapper: whole 168-byte blocks.
    void feed(const uint8_t *data, size_t len)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t off = 0; off + IMA_BLOCK_BYTES <= len; off += IMA_BLOCK_BYTES) {
            memcpy(block_.data(), data + off, IMA_BLOCK_BYTES);
            pkt_->data = block_.data();
            pkt_->size = IMA_BLOCK_BYTES;
            if (avcodec_send_packet(ctx_, pkt_) < 0)
                continue;
            while (avcodec_receive_frame(ctx_, frame_) == 0) {
                const int16_t *pcm = reinterpret_cast<const int16_t *>(frame_->data[0]);
                samples_.insert(samples_.end(), pcm, pcm + frame_->nb_samples);
            }
        }
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
    AVCodecContext *ctx_;
    AVPacket *pkt_;
    AVFrame *frame_;
    std::vector<uint8_t> block_;
    std::mutex mutex_;
    std::deque<int16_t> samples_;
    bool primed_ = false;
};

extern std::unique_ptr<AudioPlayer> g_audio;

void audio_callback(void *buffer, unsigned frames);
