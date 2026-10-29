#pragma once

#include "playback.h"

enum class Tab { Live, Playback };

struct App {
    Tab tab = Tab::Live;
    int cameras = 0;
    Grid live;
    Playback playback;
};

void enter_playback(App &app);
void enter_live(App &app);
// Advances playback state and uploads the newest decoded frames to textures.
void update_app(App &app);
