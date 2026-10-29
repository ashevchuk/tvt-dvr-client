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
// SDK callback -> queue -> decoder thread -> RGBA frame.
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

        std::lock_guard<std::mutex> lock(g_sdk_mutex);
        if (playback) {
            DD_TIME start = to_dd_time(req.start);
            DD_TIME stop = to_dd_time(req.stop);
            LONG channels[1] = {camera};
            handle_ = NET_SDK_PlayBackByTime(g_uid, channels, 1, &start, &stop, nullptr);
            if (handle_ >= 0)
                NET_SDK_SetPlayDataCallBack(handle_, &Stream::on_frame, this);
        } else {
            NET_SDK_CLIENTINFO ci;
            memset(&ci, 0, sizeof(ci));
            ci.lChannel = camera;
            ci.streamType = main_stream ? NET_SDK_MAIN_STREAM : NET_SDK_SUB_STREAM;
            ci.bNoDecode = 1;
            handle_ = NET_SDK_LivePlay(g_uid, &ci, &Stream::on_frame, this);
        }
        if (handle_ < 0)
            fprintf(stderr, "%s camera %d failed, last error %u\n", playback ? "PlayBack" : "LivePlay", camera + 1, NET_SDK_GetLastError());
    }

    ~Stream()
    {
        if (handle_ >= 0) {
            std::lock_guard<std::mutex> lock(g_sdk_mutex);
            if (playback)
                NET_SDK_StopPlayBack(handle_);
            else
                NET_SDK_StopLivePlay(handle_);
        }
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

    bool connected() const { return handle_ >= 0; }
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
        if (control(paused ? NET_SDK_PLAYCTRL_PAUSE : NET_SDK_PLAYCTRL_RESUME, 0))
            paused_ = paused;
    }

    // Speed is 1, 2 or 4; these are the multiples verified against the device.
    void set_speed(int speed)
    {
        bool ok = speed == 1 ? control(NET_SDK_PLAYCTRL_NORMAL, 0)
                             : control(NET_SDK_PLAYCTRL_FF, speed == 2 ? NET_SDK_RPB_SPEED_2X : NET_SDK_RPB_SPEED_4X);
        if (ok)
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
    static void on_frame(POINTERHANDLE, NET_SDK_FRAME_INFO info, BYTE *data, void *user)
    {
        static_cast<Stream *>(user)->handle_frame(info, data);
    }

    bool control(DWORD code, DWORD value)
    {
        if (handle_ < 0)
            return false;
        std::lock_guard<std::mutex> lock(g_sdk_mutex);
        DWORD out = 0;
        return NET_SDK_PlayBackControl(handle_, code, value, &out);
    }

    void handle_frame(const NET_SDK_FRAME_INFO &info, const BYTE *data)
    {
        if (info.frameType == DD_FRAME_TYPE_VIDEO) {
            push_video(info, data);
        } else if (info.frameType == DD_FRAME_TYPE_AUDIO) {
            bool wanted = camera == g_audio_camera && audio_allowed_ && static_cast<uint64_t>(info.time) >= min_display_us_;
            if (wanted && info.length > AUDIO_WRAPPER_BYTES)
                g_audio->feed(data + AUDIO_WRAPPER_BYTES, info.length - AUDIO_WRAPPER_BYTES);
        }
    }

    void push_video(const NET_SDK_FRAME_INFO &info, const BYTE *data)
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (wait_key_ && !info.keyFrame)
            return;
        wait_key_ = false;
        if (queue_.size() >= MAX_QUEUED_PACKETS) {
            queue_.clear();
            if (!info.keyFrame) {
                wait_key_ = true;
                return;
            }
        }
        Packet p;
        p.size = info.length;
        p.time_us = info.time;
        p.data.assign(data, data + info.length);
        p.data.resize(info.length + AV_INPUT_BUFFER_PADDING_SIZE, 0);
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

    POINTERHANDLE handle_ = -1;
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
