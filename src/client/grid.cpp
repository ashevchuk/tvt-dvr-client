#include "grid.h"

#include "audio.h"

void select_audio(const Grid &grid)
{
    int camera = grid.camera[grid.selected];
    int next = camera < g_audio_inputs ? camera : -1;
    if (next != g_audio_camera) {
        g_audio_camera = next;
        g_audio->clear();
    }
}

// Tiles of the 1- and 4-up layouts are larger than a sub stream; denser grids use the sub stream.
void rebuild_live_streams(Grid &grid)
{
    bool main_stream = grid.layout <= 4;
    for (int i = 0; i < MAX_TILES; i++) {
        std::unique_ptr<Stream> &s = grid.streams[i];
        if (i >= grid.layout) {
            s.reset();
        } else if (!s || s->camera != grid.camera[i] || s->main_stream != main_stream) {
            s.reset();
            StreamRequest req;
            req.camera = grid.camera[i];
            req.main_stream = main_stream;
            s.reset(new Stream(req));
        }
    }
    select_audio(grid);
}

void stop_all(Grid &grid)
{
    for (int i = 0; i < MAX_TILES; i++)
        grid.streams[i].reset();
}
