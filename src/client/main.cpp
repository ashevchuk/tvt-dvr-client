#include "app.h"
#include "audio.h"
#include "connect_screen.h"
#include "grid.h"
#include "icon.h"
#include "session.h"
#include "ui.h"

// The device embeds vendor SEI payloads that libavcodec reports as errors on every frame.
static void av_log_without_vendor_sei(void *avcl, int level, const char *fmt, va_list args)
{
    if (strstr(fmt, "SEI type"))
        return;
    av_log_default_callback(avcl, level, fmt, args);
}

static std::unique_ptr<App> enter_main()
{
    const dvr::DeviceInfo &info = g_device->info();
    g_audio_inputs = info.audio_inputs;
    fprintf(stderr, "connected: name=%s fw=%s channels=%d audio_inputs=%d\n", info.name.c_str(), info.firmware.c_str(),
            info.video_channels, info.audio_inputs);

    auto app = std::make_unique<App>();
    app->cameras = std::min<int>(info.video_channels, MAX_TILES);
    for (int i = 0; i < MAX_TILES; i++)
        app->live.camera[i] = std::min(i, app->cameras - 1);
    rebuild_live_streams(app->live);
    return app;
}

// Streams, segment queries and the download talk to the device, so they go first.
static void leave_main(std::unique_ptr<App> &app)
{
    cancel_download(app->playback.download);
    app.reset();
    g_device.reset();
    g_audio_camera = -1;
    g_audio->clear();
}

int main(int argc, char **argv)
{
    if (argc > 3) {
        fprintf(stderr, "usage: %s [host [port]]  (DVR_USER, DVR_PASS from env when a host is given)\n", argv[0]);
        return 2;
    }
    if (argc >= 2) {
        const char *user = getenv("DVR_USER");
        const char *pass = getenv("DVR_PASS");
        if (!user || !pass) {
            fprintf(stderr, "DVR_USER / DVR_PASS not set\n");
            return 2;
        }
        std::string error;
        int port = argc > 2 ? atoi(argv[2]) : 6036;
        g_device = dvr::connect_native(argv[1], port, user, pass, error);
        if (!g_device) {
            fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
    }

    av_log_set_callback(av_log_without_vendor_sei);
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1400, 860, "DVR");
    Image icon = LoadImageFromMemory(".png", ICON_PNG, sizeof(ICON_PNG));
    SetWindowIcon(icon);
    UnloadImage(icon);
    SetWindowMinSize(1000, 780);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    g_audio.reset(new AudioPlayer());
    InitAudioDevice();
    AudioStream audio_stream = LoadAudioStream(AUDIO_SAMPLE_RATE, 16, 1);
    SetAudioStreamCallback(audio_stream, audio_callback);
    PlayAudioStream(audio_stream);

    ConnectScreen screen;
    std::unique_ptr<App> app;
    if (g_device)
        app = enter_main();

    bool quit = false;
    while (!quit && !WindowShouldClose()) {
        if (IsKeyPressed(KEY_F11))
            ToggleFullscreen();
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (IsWindowFullscreen())
                ToggleFullscreen();
            else
                quit = true;
        }
        if (app) {
            update_app(*app);
            draw_app(*app);
            if (app->switch_device) {
                leave_main(app);
                enter_connect_screen(screen);
            }
        } else if (std::unique_ptr<dvr::Device> device = update_connect_screen(screen)) {
            g_device = std::move(device);
            app = enter_main();
        }
    }

    if (app)
        leave_main(app);
    UnloadAudioStream(audio_stream);
    CloseAudioDevice();
    g_audio.reset();
    CloseWindow();
    fprintf(stderr, "audio underruns: %u\n", g_audio_underruns.load());
    return 0;
}
