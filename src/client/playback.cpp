#include "playback.h"

#include "session.h"

static const double DRIFT_LIMIT_SECONDS = 3.0;

static const double DRIFT_HOLDOFF_SECONDS = 4.0;

void load_recorded_days(Playback &pb)
{
    for (time_t day : g_device->recorded_days()) {
        tm local;
        localtime_r(&day, &local);
        pb.recorded_days.insert(day_key(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday));
    }
}

void select_day(Playback &pb, int year, int month, int day)
{
    stop_all(pb.grid);
    pb.day_start = day_start_of(year, month, day);
    pb.segments.reset(pb.day_start);
    pb.position = pb.day_start;
    pb.playing = true;
    pb.view_start = 0;
    pb.view_span = SECONDS_PER_DAY;
    pb.selection_a = pb.selection_b = 0;
    pb.calendar_year = year;
    pb.calendar_month = month;
}

void restart_playback_streams(Playback &pb) { stop_all(pb.grid); }

// Master clock and per-tile sessions. A tile has a session only while the clock is inside one of
// its recorded segments, since a session started in a gap would jump to that channel's next recording.
void update_playback(Playback &pb, float dt)
{
    Grid &grid = pb.grid;
    const int n = grid.layout;
    SegmentList lists[MAX_PLAYBACK_TILES];
    bool loaded = true;
    for (int i = 0; i < n; i++) {
        pb.segments.request(grid.camera[i]);
        lists[i] = pb.segments.get(grid.camera[i]);
        loaded = loaded && lists[i];
    }
    if (!loaded)
        return;

    if (pb.playing)
        pb.position += dt * pb.speed;
    const double day_end = pb.day_start + SECONDS_PER_DAY;
    if (pb.position >= day_end) {
        pb.position = day_end - 1;
        pb.playing = false;
    }

    bool inside_any = false;
    for (int i = 0; i < n; i++)
        inside_any = inside_any || segment_at(*lists[i], pb.position);
    if (pb.playing && !inside_any) {
        time_t next = 0;
        for (int i = 0; i < n; i++) {
            time_t t = next_segment_start(*lists[i], pb.position);
            if (t && (!next || t < next))
                next = t;
        }
        if (next)
            pb.position = next;
        else
            pb.playing = false;
    }

    for (int i = 0; i < n; i++) {
        std::unique_ptr<Stream> &s = grid.streams[i];
        const Segment *seg = segment_at(*lists[i], pb.position);
        if (!seg) {
            s.reset();
            continue;
        }
        if (s && pb.playing && s->age_seconds() > DRIFT_HOLDOFF_SECONDS && s->has_frame() &&
            std::fabs(s->displayed_time() - pb.position) > DRIFT_LIMIT_SECONDS)
            s.reset();
        if (!s) {
            StreamRequest req;
            req.camera = grid.camera[i];
            req.playback = true;
            req.start = static_cast<time_t>(pb.position);
            req.stop = seg->stop;
            s.reset(new Stream(req));
            if (pb.speed != 1)
                s->set_speed(pb.speed);
        }
        if (s->speed() != pb.speed)
            s->set_speed(pb.speed);
        // A paused session is only stopped once it has produced a frame to show.
        if (s->has_frame() && s->paused() == pb.playing)
            s->set_paused(!pb.playing);
        s->set_audio_allowed(pb.playing && pb.speed == 1);
    }
}

static std::string download_directory()
{
    const char *home = getenv("HOME");
    return std::string(home ? home : ".") + "/Videos/dvr";
}

void start_download(Playback &pb)
{
    Download &dl = pb.download;
    if (dl.active || pb.selection_a == 0 || pb.selection_b <= pb.selection_a)
        return;
    int camera = pb.grid.camera[pb.grid.selected];
    std::error_code ec;
    std::filesystem::create_directories(download_directory(), ec);
    if (ec) {
        dl.message = "Cannot create " + download_directory() + ": " + ec.message();
        return;
    }
    tm a, b;
    localtime_r(&pb.selection_a, &a);
    localtime_r(&pb.selection_b, &b);
    char name[96];
    int n = snprintf(name, sizeof(name), "ch%02d_", camera + 1);
    n += strftime(name + n, sizeof(name) - n, "%Y%m%d_%H%M%S-", &a);
    n += strftime(name + n, sizeof(name) - n, "%H%M%S", &b);
    snprintf(name + n, sizeof(name) - n, ".%s", g_device->recording_extension());

    dl.handle = g_device->download(camera, pb.selection_a, pb.selection_b, download_directory() + "/" + name);
    if (!dl.handle) {
        dl.message = "Download failed to start";
        return;
    }
    dl.active = true;
    dl.progress = 0;
    dl.message = "Downloading " + std::string(name);
}

void poll_download(Download &dl)
{
    if (!dl.active)
        return;
    int pos = dl.handle->progress();
    if (pos >= 0 && pos < 100) {
        dl.progress = pos;
        return;
    }
    dl.message = pos == 100 ? dl.message + " - saved" : "Download failed: " + dl.handle->error();
    dl.active = false;
    dl.handle.reset();
}

void cancel_download(Download &dl)
{
    dl.handle.reset();
    dl.active = false;
}
