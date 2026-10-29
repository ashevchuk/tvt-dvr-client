#include "app.h"

#include "audio.h"

static const int DOWNLOAD_POLL_FRAMES = 12;

void enter_playback(App &app)
{
    Playback &pb = app.playback;
    stop_all(app.live);
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

void update_app(App &app)
{
    static int frame = 0;
    Grid &grid = app.tab == Tab::Live ? app.live : app.playback.grid;
    if (app.tab == Tab::Playback) {
        update_playback(app.playback, GetFrameTime());
        if (++frame % DOWNLOAD_POLL_FRAMES == 0)
            poll_download(app.playback.download);
    }
    for (int i = 0; i < grid.layout; i++)
        if (grid.streams[i])
            grid.streams[i]->update_texture();
}
