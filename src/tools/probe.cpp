#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <vector>

#include "DVR_NET_SDK.h"

static const unsigned AUDIO_WRAPPER_BYTES = 4;
static const uint16_t WAVE_FORMAT_IMA_ADPCM = 0x11;
static const uint16_t AUDIO_SAMPLE_RATE = 8000;
// The device reports nBlockAlign=1 in its format frame; the real IMA block
// size (4-byte header + 164 data bytes) was determined from the stream itself.
static const uint16_t IMA_BLOCK_BYTES = 168;
static const uint16_t IMA_SAMPLES_PER_BLOCK = (IMA_BLOCK_BYTES - 4) * 2 + 1;
static const size_t MAX_QUEUED_BYTES = 32 << 20;

// Each output gets its own writer thread. A single SDK callback thread writing
// both streams deadlocks against a muxer that waits for the other stream.
class Sink {
public:
    explicit Sink(FILE *file) : file_(file), worker_([this] { run(); }) {}

    ~Sink()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }
        wake_.notify_one();
        worker_.join();
        fclose(file_);
    }

    void push(const uint8_t *data, size_t len)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queued_bytes_ + len > MAX_QUEUED_BYTES) {
            if (!overflowed_)
                fprintf(stderr, "output queue full, dropping data\n");
            overflowed_ = true;
            return;
        }
        queue_.emplace_back(data, data + len);
        queued_bytes_ += len;
        wake_.notify_one();
    }

private:
    void run()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return closed_ || !queue_.empty(); });
            if (queue_.empty())
                return;
            std::vector<uint8_t> chunk = std::move(queue_.front());
            queue_.pop_front();
            queued_bytes_ -= chunk.size();
            lock.unlock();
            fwrite(chunk.data(), 1, chunk.size(), file_);
            fflush(file_);
            lock.lock();
        }
    }

    FILE *file_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::vector<uint8_t>> queue_;
    size_t queued_bytes_ = 0;
    bool closed_ = false;
    bool overflowed_ = false;
    std::thread worker_;
};

static std::unique_ptr<Sink> g_video;
static std::unique_ptr<Sink> g_audio;
static bool g_audio_header_written;
static unsigned g_video_frames, g_key_frames, g_audio_frames, g_other_frames;

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

// Open-ended WAV header: players read until EOF when the data size is 0xFFFFFFFF.
static void push_audio_header()
{
    uint8_t h[60];
    memcpy(h, "RIFF", 4);
    put32(h + 4, 0xFFFFFFFF);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 20);
    put16(h + 20, WAVE_FORMAT_IMA_ADPCM);
    put16(h + 22, 1);
    put32(h + 24, AUDIO_SAMPLE_RATE);
    put32(h + 28, (uint32_t)AUDIO_SAMPLE_RATE * IMA_BLOCK_BYTES / IMA_SAMPLES_PER_BLOCK);
    put16(h + 32, IMA_BLOCK_BYTES);
    put16(h + 34, 4);
    put16(h + 36, 2);
    put16(h + 38, IMA_SAMPLES_PER_BLOCK);
    memcpy(h + 40, "fact", 4);
    put32(h + 44, 4);
    put32(h + 48, 0);
    memcpy(h + 52, "data", 4);
    put32(h + 56, 0xFFFFFFFF);
    g_audio->push(h, sizeof(h));
}

static bool audio_format_supported(const BYTE *f, unsigned len)
{
    if (len < 16)
        return false;
    uint16_t tag = f[0] | (f[1] << 8);
    uint16_t channels = f[2] | (f[3] << 8);
    uint32_t rate = f[4] | (f[5] << 8) | (f[6] << 16) | ((uint32_t)f[7] << 24);
    return tag == WAVE_FORMAT_IMA_ADPCM && channels == 1 && rate == AUDIO_SAMPLE_RATE;
}

static void on_frame(POINTERHANDLE, NET_SDK_FRAME_INFO info, BYTE *data, void *)
{
    switch (info.frameType) {
    case DD_FRAME_TYPE_VIDEO:
        if (g_video_frames == 0)
            fprintf(stderr, "first video frame: %ux%u key=%u len=%u\n", info.width, info.height, info.keyFrame, info.length);
        g_video_frames++;
        g_key_frames += info.keyFrame ? 1 : 0;
        g_video->push(data, info.length);
        break;
    case DD_FRAME_TYPE_AUDIO_FORMAT:
        if (g_audio && !audio_format_supported(data, info.length)) {
            fprintf(stderr, "unsupported audio format, audio disabled\n");
            g_audio.reset();
        }
        break;
    case DD_FRAME_TYPE_AUDIO:
        g_audio_frames++;
        if (!g_audio || info.length <= AUDIO_WRAPPER_BYTES)
            break;
        if (!g_audio_header_written) {
            push_audio_header();
            g_audio_header_written = true;
        }
        g_audio->push(data + AUDIO_WRAPPER_BYTES, info.length - AUDIO_WRAPPER_BYTES);
        break;
    default:
        g_other_frames++;
    }
}

int main(int argc, char **argv)
{
    if (argc < 6) {
        fprintf(stderr, "usage: %s host port channel seconds video.h264|-  [audio.wav]  (DVR_USER, DVR_PASS from env)\n", argv[0]);
        return 2;
    }
    const char *user = getenv("DVR_USER");
    const char *pass = getenv("DVR_PASS");
    if (!user || !pass) {
        fprintf(stderr, "DVR_USER / DVR_PASS not set\n");
        return 2;
    }

    if (!NET_SDK_Init()) {
        fprintf(stderr, "NET_SDK_Init failed\n");
        return 1;
    }

    NET_SDK_DEVICEINFO dev;
    memset(&dev, 0, sizeof(dev));
    LONG uid = NET_SDK_Login(argv[1], (WORD)atoi(argv[2]), (char *)user, (char *)pass, &dev);
    if (uid < 0) {
        fprintf(stderr, "login failed, last error %u\n", NET_SDK_GetLastError());
        return 1;
    }
    fprintf(stderr, "login ok: product=%.28s name=%.36s fw=%.36s channels=%u\n",
            dev.deviceProduct, dev.deviceName, dev.firmwareVersion, dev.videoInputNum);

    FILE *video;
    if (strcmp(argv[5], "-") == 0) {
        // The SDK logs to stdout; keep a private fd for the stream and send fd 1 to stderr.
        video = fdopen(dup(STDOUT_FILENO), "wb");
        dup2(STDERR_FILENO, STDOUT_FILENO);
    } else {
        video = fopen(argv[5], "wb");
    }
    if (!video) {
        perror("video output");
        return 1;
    }
    g_video.reset(new Sink(video));

    if (argc > 6) {
        // O_RDWR keeps open() from blocking on a FIFO until the reader attaches.
        int fd = open(argv[6], O_RDWR | O_CREAT | O_TRUNC, 0644);
        FILE *audio = fd < 0 ? nullptr : fdopen(fd, "wb");
        if (!audio) {
            perror("audio output");
            return 1;
        }
        g_audio.reset(new Sink(audio));
    }

    NET_SDK_CLIENTINFO ci;
    memset(&ci, 0, sizeof(ci));
    ci.lChannel = atoi(argv[3]);
    ci.streamType = NET_SDK_MAIN_STREAM;
    ci.bNoDecode = 1;

    POINTERHANDLE live = NET_SDK_LivePlay(uid, &ci, on_frame, nullptr);
    if (live < 0) {
        fprintf(stderr, "LivePlay failed, last error %u\n", NET_SDK_GetLastError());
        return 1;
    }

    sleep(atoi(argv[4]));

    NET_SDK_StopLivePlay(live);
    NET_SDK_Logout(uid);
    NET_SDK_Cleanup();
    g_video.reset();
    g_audio.reset();
    fprintf(stderr, "video=%u key=%u audio=%u other=%u\n", g_video_frames, g_key_frames, g_audio_frames, g_other_frames);
    return g_video_frames ? 0 : 1;
}
