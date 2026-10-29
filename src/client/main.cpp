#include "app.h"
#include "audio.h"
#include "grid.h"
#include "session.h"
#include "ui.h"

// The device embeds vendor SEI payloads that libavcodec reports as errors on every frame.
static void av_log_without_vendor_sei(void *avcl, int level, const char *fmt, va_list args)
{
    if (strstr(fmt, "SEI type"))
        return;
    av_log_default_callback(avcl, level, fmt, args);
}

static int usage(const char *program)
{
    fprintf(stderr, "usage: %s [--backend native|sdk] host [port]  (DVR_USER, DVR_PASS from env)\n", program);
    return 2;
}

int main(int argc, char **argv)
{
    std::string backend = "native";
    std::vector<std::string> positional;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--backend") && i + 1 < argc)
            backend = argv[++i];
        else
            positional.push_back(argv[i]);
    }
    if (positional.empty() || positional.size() > 2 || (backend != "native" && backend != "sdk"))
        return usage(argv[0]);
    const char *user = getenv("DVR_USER");
    const char *pass = getenv("DVR_PASS");
    if (!user || !pass) {
        fprintf(stderr, "DVR_USER / DVR_PASS not set\n");
        return 2;
    }

    std::string error;
    int port = positional.size() > 1 ? atoi(positional[1].c_str()) : 6036;
    if (backend == "native") {
        g_device = dvr::connect_native(positional[0], port, user, pass, error);
    } else {
#ifdef DVR_WITH_SDK
        g_device = dvr::connect_sdk(positional[0], port, user, pass, error);
#else
        fprintf(stderr, "this build has no SDK backend (build with SDK=1)\n");
        return 2;
#endif
    }
    if (!g_device) {
        fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const dvr::DeviceInfo &info = g_device->info();
    g_audio_inputs = info.audio_inputs;
    fprintf(stderr, "%s backend: name=%s fw=%s channels=%d audio_inputs=%d\n", backend.c_str(), info.name.c_str(),
            info.firmware.c_str(), info.video_channels, info.audio_inputs);

    App app;
    app.cameras = std::min<int>(info.video_channels, MAX_TILES);
    for (int i = 0; i < MAX_TILES; i++)
        app.live.camera[i] = std::min(i, app.cameras - 1);

    av_log_set_callback(av_log_without_vendor_sei);
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1400, 860, "DVR");
    SetWindowMinSize(1000, 780);
    SetTargetFPS(60);

    g_audio.reset(new AudioPlayer());
    InitAudioDevice();
    AudioStream audio_stream = LoadAudioStream(AUDIO_SAMPLE_RATE, 16, 1);
    SetAudioStreamCallback(audio_stream, audio_callback);
    PlayAudioStream(audio_stream);

    rebuild_live_streams(app.live);

    while (!WindowShouldClose()) {
        update_app(app);
        draw_app(app);
    }

    cancel_download(app.playback.download);
    stop_all(app.live);
    stop_all(app.playback.grid);
    UnloadAudioStream(audio_stream);
    CloseAudioDevice();
    g_audio.reset();
    CloseWindow();
    g_device.reset();
    fprintf(stderr, "audio underruns: %u\n", g_audio_underruns.load());
    return 0;
}
