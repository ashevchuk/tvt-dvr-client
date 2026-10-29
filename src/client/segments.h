#pragma once

#include "common.h"

struct Segment {
    time_t start;
    time_t stop;
};

typedef std::shared_ptr<const std::vector<Segment>> SegmentList;

SegmentList query_segments(int camera, time_t day_start);
const Segment *segment_at(const std::vector<Segment> &list, double t);
time_t next_segment_start(const std::vector<Segment> &list, double t);

// Recorded segments of one day per camera, loaded in the background on first use.
class SegmentCache {
public:
    void reset(time_t day_start)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        serial_++;
        day_start_ = day_start;
        lists_.clear();
        requested_.clear();
    }

    SegmentList get(int camera) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = lists_.find(camera);
        return it == lists_.end() ? nullptr : it->second;
    }

    void request(int camera)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!requested_.insert(camera).second)
            return;
        unsigned serial = serial_;
        time_t day = day_start_;
        futures_.push_back(std::async(std::launch::async, [this, camera, serial, day] {
            SegmentList list = query_segments(camera, day);
            std::lock_guard<std::mutex> done_lock(mutex_);
            if (serial == serial_)
                lists_[camera] = list;
        }));
    }

private:
    mutable std::mutex mutex_;
    unsigned serial_ = 0;
    time_t day_start_ = 0;
    std::map<int, SegmentList> lists_;
    std::set<int> requested_;
    std::vector<std::future<void>> futures_;
};
