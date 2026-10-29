#pragma once

#include "common.h"
#include "stream.h"

struct Grid {
    int layout = 4;
    int selected = 0;
    int camera[MAX_TILES];
    std::unique_ptr<Stream> streams[MAX_TILES];
};

void select_audio(const Grid &grid);
void rebuild_live_streams(Grid &grid);
void stop_all(Grid &grid);
