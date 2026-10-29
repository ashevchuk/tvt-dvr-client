#pragma once

#include <algorithm>

// What a grid looked like before one of its tiles was shown alone, so that it can be put back.
struct TileExpansion {
    static constexpr int SLOTS = 16;

    bool active = false;

    // Shows the camera of `tile` alone, or, if a tile is already expanded, restores the grid it came
    // from. Returns true when the layout changed. `cameras` must hold SLOTS entries.
    bool toggle(int &current_layout, int &current_selected, int *cameras, int tile)
    {
        if (active) {
            current_layout = layout_;
            restore_cameras(current_selected, cameras);
            return true;
        }
        if (current_layout <= 1)
            return false;
        layout_ = current_layout;
        selected_ = current_selected;
        std::copy(cameras, cameras + SLOTS, cameras_);
        cameras[0] = cameras[tile];
        current_layout = 1;
        current_selected = 0;
        active = true;
        return true;
    }

    // For a caller that is about to choose a layout itself: puts back the cameras and the selection.
    void restore_cameras(int &current_selected, int *cameras)
    {
        if (!active)
            return;
        std::copy(cameras_, cameras_ + SLOTS, cameras);
        current_selected = selected_;
        active = false;
    }

    // Keeps the grid as it is now; a later toggle no longer restores anything.
    void forget() { active = false; }

private:
    int layout_ = 0;
    int selected_ = 0;
    int cameras_[SLOTS] = {};
};
