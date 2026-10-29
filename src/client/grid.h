#pragma once

#include "common.h"
#include "stream.h"
#include "tile_expansion.h"

static_assert(TileExpansion::SLOTS == MAX_TILES, "the expansion must remember every tile");

struct Grid {
    int layout = 4;
    int selected = 0;
    int camera[MAX_TILES];
    std::unique_ptr<Stream> streams[MAX_TILES];
    TileExpansion expansion;
};

void select_audio(const Grid &grid);
void rebuild_live_streams(Grid &grid);
void stop_all(Grid &grid);
