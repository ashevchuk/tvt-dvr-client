#include "app.h"

#include "audio.h"

static const int DOWNLOAD_POLL_FRAMES = 12;

void enter_playback(App &app)
{
    Playback &pb = app.playback;
    stop_all(app.live);
    pb.grid.expansion.forget();
    pb.grid.layout = app.live.layout == 1 ? 1 : 4;
    for (int i = 0; i < MAX_TILES; i++)
        pb.grid.camera[i] = app.live.camera[i];
    pb.grid.selected = std::min(app.live.selected, pb.grid.layout - 1);
    if (pb.recorded_days.empty())
        load_recorded_days(pb);
    if (pb.day_start == 0) {
        time_t now = time(nullptr);
        tm local;
        localtime_r(&now, &local);
        select_day(pb, local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    }
    select_audio(pb.grid);
    app.tab = Tab::Playback;
}

void enter_live(App &app)
{
    stop_all(app.playback.grid);
    app.tab = Tab::Live;
    rebuild_live_streams(app.live);
}

// Streams whose session ended while the device was away are dropped and opened again; live streams
// that were subscribed on the reopened connection resume by themselves. Segment lists fetched
// during the outage may be empty, so they are fetched again.
static void recover_streams(App &app)
{
    for (Grid *grid : {&app.live, &app.playback.grid})
        for (std::unique_ptr<Stream> &s : grid->streams)
            if (s && !s->connected())
                s.reset();
    if (app.tab == Tab::Live)
        rebuild_live_streams(app.live);

    Playback &pb = app.playback;
    if (pb.day_start != 0)
        pb.segments.reset(pb.day_start);
    if (app.tab == Tab::Playback && pb.recorded_days.empty())
        load_recorded_days(pb);
}

void update_app(App &app)
{
    static int frame = 0;
    if (app.reconnect.update(*g_device) == Reconnector::Event::Recovered)
        recover_streams(app);
    Grid &grid = app.tab == Tab::Live ? app.live : app.playback.grid;
    if (app.tab == Tab::Playback) {
        update_playback(app.playback, GetFrameTime(), !app.reconnect.down());
        if (++frame % DOWNLOAD_POLL_FRAMES == 0)
            poll_download(app.playback.download);
    }
    for (int i = 0; i < grid.layout; i++)
        if (grid.streams[i])
            grid.streams[i]->update_texture();
}
