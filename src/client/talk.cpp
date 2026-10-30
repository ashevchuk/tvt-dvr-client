#include "talk.h"

#include "media/ima_decoder.h"
#include "session.h"
#include "widgets.h"

// The device was only tested with five units (200 ms) per message.
static const size_t MESSAGE_BYTES = 5 * dvr::ImaDecoder::kUnitBytes;

Talk::~Talk()
{
    end();
    if (opening_.valid())
        opening_.get();
}

void Talk::button(Rectangle r)
{
    bool held = hold_button(r, "Talk", grabbed_, session_ != nullptr);
    if (held != held_) {
        held_ = held;
        if (held)
            begin();
        else
            end();
    }
    if (opening_.valid() && opening_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        adopt(opening_.get());
}

void Talk::begin()
{
    if (!microphone_) {
        std::string error;
        microphone_ = dvr::Microphone::open([this](const int16_t *pcm, size_t count) { on_audio(pcm, count); }, error);
        if (!microphone_) {
            fprintf(stderr, "%s\n", error.c_str());
            return;
        }
    }
    if (!opening_.valid()) {
        dvr::Device &device = *g_device;
        opening_ = std::async(std::launch::async, [&device] { return device.open_talk(); });
    }
}

// A channel that finishes opening after the button was released is closed again at once.
void Talk::adopt(std::unique_ptr<dvr::TalkSession> session)
{
    if (!session) {
        fprintf(stderr, "the device refused the talk channel\n");
        return;
    }
    if (!held_)
        return;
    session_ = std::move(session);
    encoder_ = std::make_unique<dvr::ImaEncoder>();
    pending_.clear();
    if (!microphone_->start()) {
        fprintf(stderr, "cannot start the microphone\n");
        session_.reset();
    }
}

// Stopping the microphone first waits for a running callback, so on_audio never sees a dead session.
void Talk::end()
{
    if (microphone_)
        microphone_->stop();
    session_.reset();
}

void Talk::on_audio(const int16_t *pcm, size_t count)
{
    encoder_->encode(pcm, count, pending_);
    size_t sent = 0;
    for (; sent + MESSAGE_BYTES <= pending_.size(); sent += MESSAGE_BYTES)
        session_->send(pending_.data() + sent, MESSAGE_BYTES);
    pending_.erase(pending_.begin(), pending_.begin() + sent);
}
