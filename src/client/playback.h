#pragma once

#include "common.h"
#include "grid.h"
#include "segments.h"

struct Download {
    std::unique_ptr<dvr::Download> handle;
    bool active = false;
    int progress = 0;
    std::string message;
};

struct Playback {
    Grid grid;
    SegmentCache segments;
    std::set<int> recorded_days;
    int calendar_year = 0;
    int calendar_month = 0;
    time_t day_start = 0;
    double position = 0;
    bool playing = true;
    int speed = 1;
    double view_start = 0;
    double view_span = SECONDS_PER_DAY;
    time_t selection_a = 0;
    time_t selection_b = 0;
    bool dragging_selection = false;
    Download download;
};

inline int day_key(int year, int month, int day) { return year * 10000 + month * 100 + day; }

void load_recorded_days(Playback &pb);
void select_day(Playback &pb, int year, int month, int day);
void restart_playback_streams(Playback &pb);
// Sessions are only opened while `device_up`; opening one blocks while the device is unreachable.
void update_playback(Playback &pb, float dt, bool device_up);
void start_download(Playback &pb);
void poll_download(Download &dl);
void cancel_download(Download &dl);
