// Exercises a Device through the interface the client uses: live streams, a paced playback with a
// pause, searches and a download into a Matroska file.
// Usage: devicecheck HOST DOWNLOAD.mkv   (DVR_USER and DVR_PASS from the environment)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>

#include "device/device.h"

using namespace dvr;
using Clock = std::chrono::steady_clock;

static time_t local_time(int y, int mo, int d, int h, int mi, int s)
{
    tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    t.tm_isdst = -1;
    return mktime(&t);
}

static void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

int main(int argc, char **argv)
{
    if (argc < 3 || !getenv("DVR_USER") || !getenv("DVR_PASS")) {
        fprintf(stderr, "usage: %s HOST DOWNLOAD.mkv  (DVR_USER and DVR_PASS from the environment)\n", argv[0]);
        return 2;
    }
    std::string error;
    std::unique_ptr<Device> device = connect_native(argv[1], 6036, getenv("DVR_USER"), getenv("DVR_PASS"), error);
    if (!device) {
        fprintf(stderr, "connect failed: %s\n", error.c_str());
        return 1;
    }
    const DeviceInfo &info = device->info();
    printf("%s, firmware %s, %d channels, %d audio inputs\n", info.name.c_str(), info.firmware.c_str(),
           info.video_channels, info.audio_inputs);
    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        printf("  %-62s %s\n", what, ok ? "ok" : "FAILED");
        failures += ok ? 0 : 1;
    };

    printf("search\n");
    auto days = device->recorded_days();
    time_t today = local_time(2026, 10, 29, 0, 0, 0);
    auto segments = device->segments(0, today);
    check(days.size() >= 10, "recorded days listed");
    check(segments.size() > 100, "segments of channel 0 listed");

    printf("live: channels 0 (main), 1 (sub), 3 (main) for 4 s\n");
    {
        std::mutex m;
        std::map<int, int> video, audio;
        auto handler = [&](const Frame &f) {
            std::lock_guard<std::mutex> lock(m);
            (f.kind == Frame::Kind::Video ? video : audio)[f.channel]++;
        };
        auto a = device->open_live(0, true, handler);
        auto b = device->open_live(1, false, handler);
        auto c = device->open_live(3, true, handler);
        check(a && b && c && a->ok() && b->ok() && c->ok(), "sessions are ok");
        sleep_ms(4000);
        c.reset();                       // dropping one channel must not disturb the others
        int before, video0, video1, video3, audio0, audio1, audio3;
        {
            std::lock_guard<std::mutex> lock(m);
            before = video[0];
            video3 = video[3];
        }
        sleep_ms(2000);
        {
            std::lock_guard<std::mutex> lock(m);
            video0 = video[0];
            video1 = video[1];
            audio0 = audio[0];
            audio1 = audio[1];
            audio3 = audio[3];
            check(video[3] == video3, "channel 3 delivers nothing after it was removed");
        }
        // Channel 3 runs for 4 s and every stream waits for its first key frame, so it gets a lower bar.
        check(video0 > 80 && video1 > 80 && video3 > 40, "all three channels deliver video");
        check(audio0 > 15 && audio1 > 15 && audio3 > 15, "channels with audio inputs deliver audio");
        check(video0 - before > 40, "channel 0 keeps running after channel 3 was removed");
    }

    printf("playback: channel 0 from 00:13:16, real-time pacing, pause\n");
    {
        std::mutex m;
        std::vector<uint64_t> shown;
        auto session = device->open_playback(0, local_time(2026, 10, 29, 0, 13, 16), local_time(2026, 10, 29, 0, 14, 23), [&](const Frame &f) {
            if (f.kind != Frame::Kind::Video)
                return;
            std::lock_guard<std::mutex> lock(m);
            shown.push_back(f.time_us);
        });
        check(session && session->ok(), "playback session is ok");
        sleep_ms(6000);
        size_t at_six_seconds;
        {
            std::lock_guard<std::mutex> lock(m);
            at_six_seconds = shown.size();
        }
        // 25 frames/s for 6 s; the window of unacknowledged frames lets it run a little ahead.
        check(at_six_seconds > 125 && at_six_seconds < 190, "about real time: 6 s of recording in 6 s");
        session->set_paused(true);
        sleep_ms(1500);                  // the frames already in flight settle
        size_t paused_at;
        {
            std::lock_guard<std::mutex> lock(m);
            paused_at = shown.size();
        }
        sleep_ms(3000);
        size_t paused_end;
        {
            std::lock_guard<std::mutex> lock(m);
            paused_end = shown.size();
        }
        check(paused_end == paused_at, "no frames while paused");
        session->set_paused(false);
        sleep_ms(3000);
        std::lock_guard<std::mutex> lock(m);
        check(shown.size() > paused_end + 40, "frames flow again after resume");
        uint64_t span = shown.back() - shown.front();
        printf("  recording time delivered: %.1f s\n", span / 1e6);
    }

    printf("download: channel 0, 00:13:20 - 00:13:50 -> %s\n", argv[2]);
    {
        auto started = Clock::now();
        auto dl = device->download(0, local_time(2026, 10, 29, 0, 13, 20), local_time(2026, 10, 29, 0, 13, 50), argv[2]);
        check(dl != nullptr, "download started");
        int progress = 0, last_progress = -1;
        while (dl && progress >= 0 && progress < 100 && Clock::now() - started < std::chrono::seconds(60)) {
            progress = dl->progress();
            if (progress != last_progress && progress % 25 == 0) {
                printf("  progress %d%%\n", progress);
                last_progress = progress;
            }
            sleep_ms(100);
        }
        double seconds = std::chrono::duration<double>(Clock::now() - started).count();
        check(progress == 100, "download completes");
        if (dl && progress < 100)
            printf("  error: %s\n", dl->error().c_str());
        printf("  took %.1f s\n", seconds);
    }

    printf("%s\n", failures ? "SOME CHECKS FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
