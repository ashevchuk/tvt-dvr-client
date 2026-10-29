// Exercises the native protocol layer against a real device: login, search, live streams and a
// download. Usage: protocheck HOST [PORT]   (DVR_USER and DVR_PASS from the environment)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <mutex>
#include <thread>

#include "proto/connection.h"

using namespace dvr;

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

int main(int argc, char **argv)
{
    if (argc < 2 || !getenv("DVR_USER") || !getenv("DVR_PASS")) {
        fprintf(stderr, "usage: %s HOST [PORT]  (DVR_USER and DVR_PASS from the environment)\n", argv[0]);
        return 2;
    }
    int port = argc > 2 ? atoi(argv[2]) : 6036;
    std::string error;
    auto conn = Connection::open(argv[1], port, getenv("DVR_USER"), getenv("DVR_PASS"), error);
    if (!conn) {
        fprintf(stderr, "open failed: %s\n", error.c_str());
        return 1;
    }
    const auto &info = conn->info();
    printf("login ok: name=%s firmware=%s channels=%d audio_inputs=%d\n", info.name.c_str(), info.firmware.c_str(),
           info.video_channels, info.audio_inputs);

    wire::Message reply;
    if (!conn->request(wire::search_days(), wire::kCmdSearchReply, reply, 5000)) {
        fprintf(stderr, "day search failed\n");
        return 1;
    }
    auto days = wire::parse_days(reply);
    printf("recorded days: %zu\n", days.size());

    time_t now = time(nullptr);
    tm today;
    localtime_r(&now, &today);
    time_t day_start = local_time(today.tm_year + 1900, today.tm_mon + 1, today.tm_mday, 0, 0, 0);
    if (!conn->request(wire::search_segments(0, day_start, day_start + 86399), wire::kCmdSearchReply, reply, 5000)) {
        fprintf(stderr, "segment search failed\n");
        return 1;
    }
    printf("segments of channel 0 today: %zu\n", wire::parse_segments(reply).size());

    std::mutex m;
    std::map<std::pair<int, int>, int> video;
    conn->set_media_handler([&](const wire::MediaFrame &f) {
        if (f.kind != wire::Kind::Video)
            return;
        std::lock_guard<std::mutex> lock(m);
        video[{f.channel, static_cast<int>(f.stream)}]++;
    });
    conn->send(wire::live_streams(false, 0x1, 0x2));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    conn->send(wire::live_streams(true, 0x9, 0x2));
    std::this_thread::sleep_for(std::chrono::seconds(4));
    conn->send(wire::live_streams(true, 0, 0));
    {
        std::lock_guard<std::mutex> lock(m);
        for (auto &kv : video)
            printf("live channel %d %s stream: %d video frames\n", kv.first.first, kv.first.second == 0x20 ? "main" : "sub", kv.second);
    }

    // A 30-second download acknowledged in batches, as fast as the device sends. It gets its own
    // connection so frames still in flight from the live streams cannot be counted.
    auto downloader = Connection::open(argv[1], port, getenv("DVR_USER"), getenv("DVR_PASS"), error);
    if (!downloader) {
        fprintf(stderr, "second connection failed: %s\n", error.c_str());
        return 1;
    }
    std::atomic<int> frames{0}, messages{0};
    std::atomic<bool> ended{false};
    downloader->set_media_handler([&](const wire::MediaFrame &f) {
        if (f.kind == wire::Kind::End) {
            ended = true;
        } else if (f.kind == wire::Kind::Video || f.kind == wire::Kind::Audio) {
            if (f.kind == wire::Kind::Video)
                frames++;
            if (++messages % 8 == 0)
                downloader->send(wire::play_ack(wire::kDownloadSession, messages));
        }
    });
    auto started = std::chrono::steady_clock::now();
    downloader->send(wire::play_start(wire::kDownloadSession, 0, local_time(2026, 10, 29, 0, 13, 20), local_time(2026, 10, 29, 0, 13, 50), true));
    while (!ended && std::chrono::steady_clock::now() - started < std::chrono::seconds(30))
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    downloader->send(wire::play_stop(wire::kDownloadSession));
    printf("download of 00:13:20-00:13:50: %d video frames in %.1f s, end marker %s\n", frames.load(), seconds, ended ? "seen" : "MISSING");
    return (frames == 763 && ended) ? 0 : 1;
}
