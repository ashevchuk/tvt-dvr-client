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

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s host [port]  (DVR_USER, DVR_PASS from env)\n", argv[0]);
        return 2;
    }
    const char *user = getenv("DVR_USER");
    const char *pass = getenv("DVR_PASS");
    if (!user || !pass) {
        fprintf(stderr, "DVR_USER / DVR_PASS not set\n");
        return 2;
    }

    if (!NET_SDK_Init()) {
        fprintf(stderr, "NET_SDK_Init failed\n");
        return 1;
    }
    NET_SDK_DEVICEINFO dev;
    memset(&dev, 0, sizeof(dev));
    g_uid = NET_SDK_Login(argv[1], (WORD)(argc > 2 ? atoi(argv[2]) : 6036), (char *)user, (char *)pass, &dev);
    if (g_uid < 0) {
        fprintf(stderr, "login failed, last error %u\n", NET_SDK_GetLastError());
        NET_SDK_Cleanup();
        return 1;
    }
    g_audio_inputs = dev.audioInputNum;
    fprintf(stderr, "login ok: name=%.36s fw=%.36s channels=%u audio_inputs=%d\n",
            dev.deviceName, dev.firmwareVersion, dev.videoInputNum, g_audio_inputs);

    App app;
    app.cameras = std::min<int>(dev.videoInputNum, MAX_TILES);
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
    NET_SDK_Logout(g_uid);
    NET_SDK_Cleanup();
    fprintf(stderr, "audio underruns: %u\n", g_audio_underruns.load());
    return 0;
}
