#include "segments.h"

#include "session.h"

SegmentList query_segments(int camera, time_t day_start)
{
    auto list = std::make_shared<std::vector<Segment>>(g_device->segments(camera, day_start));
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
