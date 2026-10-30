#include "microphone.h"

#include "media/ima_decoder.h"

// raylib embeds its own miniaudio, and the AppImage links raylib statically. Making every
// miniaudio function static in this file keeps its symbols out of the link, so the two copies cannot clash.
// The one global variable is declared without MA_API and is renamed instead.
#define MA_API static
#define ma_atomic_global_lock dvr_ma_atomic_global_lock
#define MA_NO_JACK
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include "../../third_party/miniaudio/miniaudio.h"
#pragma GCC diagnostic pop

namespace dvr {

struct Microphone::Impl {
    ma_device device;
    Handler handler;
    bool initialized = false;
};

namespace {

void on_capture(ma_device *device, void *, const void *input, ma_uint32 frames)
{
    auto &handler = *static_cast<Microphone::Handler *>(device->pUserData);
    handler(static_cast<const int16_t *>(input), frames);
}

}  // namespace

std::unique_ptr<Microphone> Microphone::open(Handler handler, std::string &error)
{
    auto impl = std::make_unique<Impl>();
    impl->handler = std::move(handler);
    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_s16;
    config.capture.channels = 1;
    config.sampleRate = kAudioSampleRate;
    config.dataCallback = on_capture;
    config.pUserData = &impl->handler;
    ma_result result = ma_device_init(nullptr, &config, &impl->device);
    if (result != MA_SUCCESS) {
        error = std::string("cannot open the microphone: ") + ma_result_description(result);
        return nullptr;
    }
    impl->initialized = true;
    return std::unique_ptr<Microphone>(new Microphone(std::move(impl)));
}

Microphone::Microphone(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Microphone::~Microphone()
{
    if (impl_->initialized)
        ma_device_uninit(&impl_->device);
}

bool Microphone::start()
{
    return ma_device_start(&impl_->device) == MA_SUCCESS;
}

void Microphone::stop()
{
    ma_device_stop(&impl_->device);
}

}  // namespace dvr
