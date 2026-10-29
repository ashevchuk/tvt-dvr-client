#pragma once

#include "playback.h"
#include "reconnect.h"

enum class Tab { Live, Playback };

struct App {
    Tab tab = Tab::Live;
    int cameras = 0;
    Grid live;
    Playback playback;
    bool switch_device = false;
    // Declared last so that an attempt in progress is waited for before the streams are destroyed.
    Reconnector reconnect;
};

void enter_playback(App &app);
void enter_live(App &app);
// Advances playback state and uploads the newest decoded frames to textures.
void update_app(App &app);
