#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace dvr {

// Captures the default microphone as 8 kHz mono 16-bit PCM, the format the DVR's talk channel
// takes. Capturing runs only between start() and stop().
class Microphone {
public:
    // Called on the audio thread with each captured chunk; `pcm` is only valid during the call.
    using Handler = std::function<void(const int16_t *pcm, size_t count)>;

    static std::unique_ptr<Microphone> open(Handler handler, std::string &error);
    ~Microphone();
    Microphone(const Microphone &) = delete;
    Microphone &operator=(const Microphone &) = delete;

    bool start();
    void stop();

private:
    struct Impl;
    explicit Microphone(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace dvr
