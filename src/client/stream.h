#pragma once

#include "audio.h"
#include "common.h"
#include "session.h"

static const size_t MAX_QUEUED_PACKETS = 100;

struct Packet {
    std::vector<uint8_t> data;
    size_t size;
    uint64_t time_us;
};

struct StreamRequest {
    int camera = 0;
    bool main_stream = true;
    bool playback = false;
    time_t start = 0;
    time_t stop = 0;
};

// One connection to one camera, live or recorded:
// device callback -> queue -> decoder thread -> RGBA frame.
class Stream {
public:
    const int camera;
    const bool main_stream;
    const bool playback;

    explicit Stream(const StreamRequest &req)
        : camera(req.camera), main_stream(req.main_stream), playback(req.playback),
          min_display_us_(req.playback ? static_cast<uint64_t>(req.start) * 1000000 : 0),
          created_(std::chrono::steady_clock::now())
    {
        const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
        ctx_ = avcodec_alloc_context3(codec);
        ctx_->thread_count = 1;
        ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        if (avcodec_open2(ctx_, codec, nullptr) < 0) {
            fprintf(stderr, "cannot open H.264 decoder\n");
            exit(1);
        }
        decoder_ = std::thread([this] { decode_loop(); });

        auto handler = [this](const dvr::Frame &f) { handle_frame(f); };
        if (playback)
            session_ = g_device->open_playback(camera, req.start, req.stop, handler);
        else
            session_ = g_device->open_live(camera, main_stream, handler);
        if (!session_)
            fprintf(stderr, "cannot open the %s of camera %d\n", playback ? "playback" : "live stream", camera + 1);
    }

    ~Stream()
    {
        session_.reset();       // the device delivers no more frames after this
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        queue_cv_.notify_one();
        decoder_.join();
        sws_freeContext(sws_);
        avcodec_free_context(&ctx_);
        if (texture_.id)
            UnloadTexture(texture_);
    }

    bool connected() const { return session_ && session_->ok(); }
    int width() const { return width_; }
    int height() const { return height_; }
    bool has_frame() const { return displayed_time_us_ != 0; }
    double displayed_time() const { return displayed_time_us_ / 1e6; }
    double age_seconds() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - created_).count();
    }
    bool paused() const { return paused_; }
    int speed() const { return speed_; }
    void set_audio_allowed(bool allowed) { audio_allowed_ = allowed; }

    void set_paused(bool paused)
    {
        if (!session_)
            return;
        session_->set_paused(paused);
        paused_ = paused;
    }

    // Speed is 1, 2 or 4; these are the multiples verified against the device.
    void set_speed(int speed)
    {
        if (!session_)
            return;
        session_->set_speed(speed);
        speed_ = speed;
    }

    // Main thread only: uploads the newest decoded frame, if any.
    void update_texture()
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (!fresh_)
            return;
        fresh_ = false;
        if (!texture_.id || texture_.width != width_ || texture_.height != height_) {
            if (texture_.id)
                UnloadTexture(texture_);
            Image img = {rgba_.data(), width_, height_, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            texture_ = LoadTextureFromImage(img);
        } else {
            UpdateTexture(texture_, rgba_.data());
        }
    }

    const Texture2D &texture() const { return texture_; }

private:
    void handle_frame(const dvr::Frame &f)
    {
        if (f.kind == dvr::Frame::Kind::Video) {
            push_video(f);
        } else if (f.kind == dvr::Frame::Kind::Audio) {
            bool wanted = camera == g_audio_camera && audio_allowed_ && f.time_us >= min_display_us_;
            if (wanted)
                g_audio->feed(f.data, f.size);
        }
    }

    void push_video(const dvr::Frame &f)
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (wait_key_ && !f.key)
            return;
        wait_key_ = false;
        if (queue_.size() >= MAX_QUEUED_PACKETS) {
            queue_.clear();
            if (!f.key) {
                wait_key_ = true;
                return;
            }
        }
        Packet p;
        p.size = f.size;
        p.time_us = f.time_us;
        p.data.assign(f.data, f.data + f.size);
        p.data.resize(f.size + AV_INPUT_BUFFER_PADDING_SIZE, 0);
        queue_.push_back(std::move(p));
        queue_cv_.notify_one();
    }

    void decode_loop()
    {
        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();
        for (;;) {
            Packet p;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_)
                    break;
                p = std::move(queue_.front());
                queue_.pop_front();
            }
            pkt->data = p.data.data();
            pkt->size = static_cast<int>(p.size);
            if (avcodec_send_packet(ctx_, pkt) < 0)
                continue;
            // Frames before the requested start are decoded as references but never shown.
            while (avcodec_receive_frame(ctx_, frame) == 0)
                if (p.time_us >= min_display_us_)
                    publish(frame, p.time_us);
        }
        av_frame_free(&frame);
        av_packet_free(&pkt);
    }

    void publish(const AVFrame *frame, uint64_t time_us)
    {
        sws_ = sws_getCachedContext(sws_, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                    frame->width, frame->height, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
        std::vector<uint8_t> out(static_cast<size_t>(frame->width) * frame->height * 4);
        uint8_t *dst[1] = {out.data()};
        int dst_stride[1] = {frame->width * 4};
        sws_scale(sws_, frame->data, frame->linesize, 0, frame->height, dst, dst_stride);

        std::lock_guard<std::mutex> lock(frame_mutex_);
        rgba_.swap(out);
        width_ = frame->width;
        height_ = frame->height;
        fresh_ = true;
        displayed_time_us_ = time_us;
    }

    std::unique_ptr<dvr::MediaSession> session_;
    AVCodecContext *ctx_;
    SwsContext *sws_ = nullptr;
    std::thread decoder_;
    const uint64_t min_display_us_;
    const std::chrono::steady_clock::time_point created_;
    std::atomic<bool> audio_allowed_{true};
    std::atomic<bool> paused_{false};
    std::atomic<int> speed_{1};

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<Packet> queue_;
    bool stop_ = false;
    bool wait_key_ = true;

    std::mutex frame_mutex_;
    std::vector<uint8_t> rgba_;
    int width_ = 0;
    int height_ = 0;
    bool fresh_ = false;
    std::atomic<uint64_t> displayed_time_us_{0};
    Texture2D texture_ = {};
};
