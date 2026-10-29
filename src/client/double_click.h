#pragma once

// Recognises two presses on the same target in quick succession.
class DoubleClick {
public:
    explicit DoubleClick(double window_seconds = 0.4) : window_(window_seconds) {}

    // `target` identifies what was pressed and `now` is the time in seconds. Returns true on the
    // second press; a third press then starts a new pair.
    bool press(int target, double now)
    {
        const bool second = target == last_target_ && now - last_time_ < window_;
        last_target_ = second ? NO_TARGET : target;
        last_time_ = now;
        return second;
    }

private:
    static constexpr int NO_TARGET = -1;

    double window_;
    int last_target_ = NO_TARGET;
    double last_time_ = 0;
};
