#include "audio.h"

int g_audio_inputs;
std::atomic<int> g_audio_camera{-1};
std::atomic<unsigned> g_audio_underruns{0};
std::unique_ptr<AudioPlayer> g_audio;

void audio_callback(void *buffer, unsigned frames)
{
    g_audio->pull(static_cast<int16_t *>(buffer), frames);
}
