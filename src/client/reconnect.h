#pragma once

#include <algorithm>
#include <chrono>
#include <future>
#include <string>

#include "device/device.h"

// Notices that the connection to the device was lost and reopens it on a background thread,
// waiting longer after each failed attempt. Destroying it waits for an attempt in progress, so it
// must be destroyed before the device.
class Reconnector {
public:
    using Clock = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;

    enum class Event { None, Recovered };

    Reconnector(Milliseconds first_delay = Milliseconds(1000), Milliseconds max_delay = Milliseconds(10000),
                Milliseconds visible_after = Milliseconds(1000))
        : first_delay_(first_delay), max_delay_(max_delay), visible_after_(visible_after)
    {
    }

    // Call it every frame from the thread that owns the device.
    Event update(dvr::Device &device)
    {
        const Clock::time_point now = Clock::now();
        if (attempt_.valid()) {
            if (attempt_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return Event::None;
            if (attempt_.get())
                return recovered();
            failures_++;
            next_attempt_ = now + retry_delay(failures_);
            return Event::None;
        }
        if (!down_) {
            if (device.connected())
                return Event::None;
            down_ = true;
            down_since_ = now;
            failures_ = 0;
            next_attempt_ = now;
        } else if (device.connected()) {
            return recovered();
        }
        if (now >= next_attempt_) {
            attempt_ = std::async(std::launch::async, [&device] {
                std::string error;
                return device.reconnect(error);
            });
        }
        return Event::None;
    }

    // True from the moment a loss is noticed until the connection is back.
    bool down() const { return down_; }
    // True once the connection has been down long enough that the user should be told.
    bool outage() const { return down_ && Clock::now() - down_since_ >= visible_after_; }
    int attempt() const { return failures_ + 1; }

    // The wait before the attempt that follows `failures` failed ones.
    Milliseconds retry_delay(int failures) const
    {
        Milliseconds delay = first_delay_;
        for (int i = 1; i < failures; i++)
            delay = std::min(delay * 2, max_delay_);
        return std::min(delay, max_delay_);
    }

private:
    Event recovered()
    {
        down_ = false;
        failures_ = 0;
        return Event::Recovered;
    }

    Milliseconds first_delay_;
    Milliseconds max_delay_;
    Milliseconds visible_after_;
    bool down_ = false;
    int failures_ = 0;
    Clock::time_point down_since_;
    Clock::time_point next_attempt_;
    std::future<bool> attempt_;
};
