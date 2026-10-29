#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>
#include <vector>

#include "DVR_NET_SDK.h"

static const int FIND_RETRY_MS = 50;
static const int FIND_TIMEOUT_MS = 10000;
static const unsigned long long NO_TIME = ~0ULL;

static const unsigned long long TIME_JUMP_US = 2000000;

static std::string format_time(unsigned long long us)
{
    time_t t = us / 1000000;
    char buf[32];
    strftime(buf, sizeof(buf), "%F %T", localtime(&t));
    return buf;
}

static unsigned g_video_frames, g_key_frames, g_audio_frames, g_other_frames;
static unsigned long long g_first_time = NO_TIME, g_last_time;
static FILE *g_video_out;

static DD_TIME make_time(int year, int month, int day, int hour, int minute, int second)
{
    DD_TIME t;
    memset(&t, 0, sizeof(t));
    t.year = year - 1900;
    t.month = month - 1;
    t.mday = day;
    t.hour = hour;
    t.minute = minute;
    t.second = second;
    return t;
}

static void print_time(const char *label, const DD_TIME &t)
{
    printf("%s%04d-%02d-%02d %02d:%02d:%02d", label, t.year + 1900, t.month + 1, t.mday, t.hour, t.minute, t.second);
}

static int seconds_of_day(const DD_TIME &t) { return t.hour * 3600 + t.minute * 60 + t.second; }

// Calls next() until the device reports the end of the list. TRY_LATER means the
// search is still running; giving up on it would silently truncate the result.
template <typename Fn>
static bool drain(Fn next, const char *what)
{
    int waited = 0;
    for (;;) {
        LONG rc = next();
        if (rc == NET_SDK_FILE_SUCCESS) {
            waited = 0;
            continue;
        }
        if (rc == NET_SDK_NOMOREFILE || rc == NET_SDK_FILE_NOFIND)
            return true;
        if (rc == NET_SDK_TRY_LATER && waited < FIND_TIMEOUT_MS) {
            usleep(FIND_RETRY_MS * 1000);
            waited += FIND_RETRY_MS;
            continue;
        }
        fprintf(stderr, "%s: unexpected result %d (last error %u)\n", what, (int)rc, NET_SDK_GetLastError());
        return false;
    }
}

static void on_play_frame(POINTERHANDLE, NET_SDK_FRAME_INFO info, BYTE *data, void *)
{
    if (info.frameType == DD_FRAME_TYPE_VIDEO) {
        g_video_frames++;
        g_key_frames += info.keyFrame ? 1 : 0;
        if (g_first_time == NO_TIME)
            g_first_time = info.time;
        else if (static_cast<unsigned long long>(info.time) > g_last_time + TIME_JUMP_US)
            printf("time jump: %s -> %s\n", format_time(g_last_time).c_str(), format_time(info.time).c_str());
        g_last_time = info.time;
        fwrite(data, 1, info.length, g_video_out);
    } else if (info.frameType == DD_FRAME_TYPE_AUDIO) {
        g_audio_frames++;
    } else {
        g_other_frames++;
    }
}

static int run(LONG uid, int argc, char **argv);

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s host port channel [YYYY-MM-DD] [capture_seconds] [out.h264] [HH:MM:SS HH:MM:SS]  (DVR_USER, DVR_PASS from env)\n", argv[0]);
        return 2;
    }
    const char *user = getenv("DVR_USER");
    const char *pass = getenv("DVR_PASS");
    if (!user || !pass) {
        fprintf(stderr, "DVR_USER / DVR_PASS not set\n");
        return 2;
    }

    if (!NET_SDK_Init())
        return 1;
    NET_SDK_DEVICEINFO dev;
    memset(&dev, 0, sizeof(dev));
    LONG uid = NET_SDK_Login(argv[1], (WORD)atoi(argv[2]), (char *)user, (char *)pass, &dev);
    if (uid < 0) {
        fprintf(stderr, "login failed, last error %u\n", NET_SDK_GetLastError());
        NET_SDK_Cleanup();
        return 1;
    }

    // Returning from main with SDK threads alive crashes the process, so every path ends here.
    int rc = run(uid, argc, argv);
    NET_SDK_Logout(uid);
    NET_SDK_Cleanup();
    return rc;
}

static int run(LONG uid, int argc, char **argv)
{
    int channel = atoi(argv[3]);
    std::vector<DD_DATE> dates;
    POINTERHANDLE dh = NET_SDK_FindRecDate(uid);
    if (dh < 0) {
        fprintf(stderr, "FindRecDate failed, last error %u\n", NET_SDK_GetLastError());
        return 1;
    }
    DD_DATE d;
    bool ok = drain([&] {
        LONG rc = NET_SDK_FindNextRecDate(dh, &d);
        if (rc == NET_SDK_FILE_SUCCESS)
            dates.push_back(d);
        return rc;
    }, "FindNextRecDate");
    NET_SDK_FindRecDateClose(dh);
    if (!ok)
        return 1;
    printf("dates with recordings: %zu\n", dates.size());
    for (const DD_DATE &x : dates)
        printf("  %04d-%02d-%02d\n", x.year, x.month, x.mday);
    if (dates.empty())
        return 1;

    DD_DATE day = dates.back();
    if (argc > 4 && argv[4][0] && sscanf(argv[4], "%hu-%hhu-%hhu", &day.year, &day.month, &day.mday) != 3) {
        fprintf(stderr, "bad date '%s'\n", argv[4]);
        return 2;
    }

    DD_TIME from = make_time(day.year, day.month, day.mday, 0, 0, 0);
    DD_TIME to = make_time(day.year, day.month, day.mday, 23, 59, 59);
    std::vector<NET_SDK_REC_TIME> segments;
    POINTERHANDLE th = NET_SDK_FindTime(uid, channel, &from, &to);
    if (th < 0) {
        fprintf(stderr, "FindTime failed, last error %u\n", NET_SDK_GetLastError());
        return 1;
    }
    NET_SDK_REC_TIME seg;
    ok = drain([&] {
        LONG rc = NET_SDK_FindNextTime(th, &seg);
        if (rc == NET_SDK_FILE_SUCCESS)
            segments.push_back(seg);
        return rc;
    }, "FindNextTime");
    NET_SDK_FindTimeClose(th);
    if (!ok)
        return 1;

    printf("channel %d, %04d-%02d-%02d: %zu segments\n", channel, day.year, day.month, day.mday, segments.size());
    int recorded = 0;
    for (size_t i = 0; i < segments.size(); i++) {
        print_time("  ", segments[i].startTime);
        print_time(" .. ", segments[i].stopTime);
        printf("  (ch=%u)\n", segments[i].dwChannel);
        recorded += seconds_of_day(segments[i].stopTime) - seconds_of_day(segments[i].startTime);
    }
    printf("recorded total: %d s\n", recorded);
    if (segments.empty())
        return 1;

    int capture_seconds = argc > 5 ? atoi(argv[5]) : 10;
    const char *out_path = argc > 6 ? argv[6] : "/dev/null";
    g_video_out = fopen(out_path, "wb");
    if (!g_video_out) {
        perror("output");
        return 1;
    }

    DD_TIME start = segments.front().startTime;
    DD_TIME stop = segments.front().stopTime;
    int sh, sm, ss, eh, em, es;
    if (argc > 8 && sscanf(argv[7], "%d:%d:%d", &sh, &sm, &ss) == 3 && sscanf(argv[8], "%d:%d:%d", &eh, &em, &es) == 3) {
        start = make_time(day.year, day.month, day.mday, sh, sm, ss);
        stop = make_time(day.year, day.month, day.mday, eh, em, es);
    }
    LONG channels[1] = {channel};
    POINTERHANDLE ph = NET_SDK_PlayBackByTime(uid, channels, 1, &start, &stop, nullptr);
    if (ph < 0) {
        fprintf(stderr, "PlayBackByTime failed, last error %u\n", NET_SDK_GetLastError());
        return 1;
    }
    if (!NET_SDK_SetPlayDataCallBack(ph, on_play_frame, nullptr))
        fprintf(stderr, "SetPlayDataCallBack failed, last error %u\n", NET_SDK_GetLastError());
    print_time("playback from ", start);
    print_time(" to ", stop);
    printf(", capturing %d s\n", capture_seconds);

    sleep(capture_seconds);

    NET_SDK_StopPlayBack(ph);
    fclose(g_video_out);

    printf("video=%u key=%u audio=%u other=%u\n", g_video_frames, g_key_frames, g_audio_frames, g_other_frames);
    if (g_first_time != NO_TIME) {
        time_t t0 = g_first_time / 1000000, t1 = g_last_time / 1000000;
        char b0[32], b1[32];
        strftime(b0, sizeof(b0), "%F %T", localtime(&t0));
        strftime(b1, sizeof(b1), "%F %T", localtime(&t1));
        printf("frame time: %s .. %s (%.1f s of recording in %d s)\n", b0, b1, (g_last_time - g_first_time) / 1e6, capture_seconds);
    }
    return g_video_frames ? 0 : 1;
}
