#include "segments.h"

#include "session.h"

// An empty list on failure: the tile then reads "no recording" instead of retrying every frame.
SegmentList query_segments(int camera, time_t day_start)
{
    auto list = std::make_shared<std::vector<Segment>>();
    DD_TIME from = to_dd_time(day_start);
    DD_TIME to = to_dd_time(day_start + SECONDS_PER_DAY - 1);
    POINTERHANDLE handle;
    {
        std::lock_guard<std::mutex> lock(g_sdk_mutex);
        handle = NET_SDK_FindTime(g_uid, camera, &from, &to);
    }
    if (handle < 0) {
        fprintf(stderr, "FindTime camera %d failed, last error %u\n", camera + 1, NET_SDK_GetLastError());
        return list;
    }
    int waited = 0;
    for (;;) {
        NET_SDK_REC_TIME rec;
        LONG rc;
        {
            std::lock_guard<std::mutex> lock(g_sdk_mutex);
            rc = NET_SDK_FindNextTime(handle, &rec);
        }
        if (rc == NET_SDK_FILE_SUCCESS) {
            list->push_back({from_dd_time(rec.startTime), from_dd_time(rec.stopTime)});
            waited = 0;
        } else if (rc == NET_SDK_NOMOREFILE || rc == NET_SDK_FILE_NOFIND) {
            break;
        } else if (rc == NET_SDK_TRY_LATER && waited < FIND_TIMEOUT_MS) {
            std::this_thread::sleep_for(std::chrono::milliseconds(FIND_RETRY_MS));
            waited += FIND_RETRY_MS;
        } else {
            fprintf(stderr, "FindNextTime camera %d: unexpected result %d\n", camera + 1, (int)rc);
            break;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_sdk_mutex);
        NET_SDK_FindTimeClose(handle);
    }
    std::sort(list->begin(), list->end(), [](const Segment &a, const Segment &b) { return a.start < b.start; });
    return list;
}

const Segment *segment_at(const std::vector<Segment> &list, double t)
{
    for (const Segment &s : list)
        if (t >= s.start && t < s.stop)
            return &s;
    return nullptr;
}

time_t next_segment_start(const std::vector<Segment> &list, double t)
{
    for (const Segment &s : list)
        if (s.start > t)
            return s.start;
    return 0;
}
