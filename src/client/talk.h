#pragma once

#include <future>
#include <memory>
#include <vector>

#include <raylib.h>

#include "device/device.h"
#include "media/ima_encoder.h"
#include "microphone.h"

// Push-to-talk: the microphone is sent to the device while the button is held down. The channel
// is opened on a background thread and the button lights up once it is open. Destroying this waits
// for an opening in progress, so it must be destroyed before the device.
class Talk {
public:
    ~Talk();

    // Call it every frame: draws the button and starts or stops talking when it is pressed or released.
    void button(Rectangle r);

private:
    void begin();
    void end();
    void adopt(std::unique_ptr<dvr::TalkSession> session);
    void on_audio(const int16_t *pcm, size_t count);

    bool grabbed_ = false;
    bool held_ = false;
    std::future<std::unique_ptr<dvr::TalkSession>> opening_;
    std::unique_ptr<dvr::Microphone> microphone_;
    std::unique_ptr<dvr::TalkSession> session_;
    std::unique_ptr<dvr::ImaEncoder> encoder_;
    std::vector<uint8_t> pending_;
};
