// The vendor SDK behind the Device interface. Built only with DVR_WITH_SDK.
#ifdef DVR_WITH_SDK

#include <chrono>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>

#include "device.h"
#include "DVR_NET_SDK.h"

namespace dvr {

namespace {

constexpr int kFindRetryMs = 50;
constexpr int kFindTimeoutMs = 10000;
constexpr int kDownloadNetworkError = 200;
constexpr int kSecondsPerDay = 86400;

// The SDK is not documented as thread safe; every call goes through this lock.
std::mutex g_mutex;
LONG g_uid = -1;

DD_TIME to_dd_time(time_t t)
{
    tm local;
    localtime_r(&t, &local);
    DD_TIME d;
    memset(&d, 0, sizeof(d));
    d.year = local.tm_year;
    d.month = local.tm_mon;
    d.mday = local.tm_mday;
    d.hour = local.tm_hour;
    d.minute = local.tm_min;
    d.second = local.tm_sec;
    return d;
}

time_t from_dd_time(const DD_TIME &d)
{
    tm local;
    memset(&local, 0, sizeof(local));
    local.tm_year = d.year;
    local.tm_mon = d.month;
    local.tm_mday = d.mday;
    local.tm_hour = d.hour;
    local.tm_min = d.minute;
    local.tm_sec = d.second;
    local.tm_isdst = -1;
    return mktime(&local);
}

class SdkSession : public MediaSession {
public:
    SdkSession(int channel, bool main_stream, FrameHandler handler) : playback_(false), handler_(std::move(handler))
    {
        NET_SDK_CLIENTINFO ci;
        memset(&ci, 0, sizeof(ci));
        ci.lChannel = channel;
        ci.streamType = main_stream ? NET_SDK_MAIN_STREAM : NET_SDK_SUB_STREAM;
        ci.bNoDecode = 1;
        std::lock_guard<std::mutex> lock(g_mutex);
        handle_ = NET_SDK_LivePlay(g_uid, &ci, &SdkSession::on_frame, this);
    }

    SdkSession(int channel, time_t start, time_t stop, FrameHandler handler) : playback_(true), handler_(std::move(handler))
    {
        DD_TIME from = to_dd_time(start);
        DD_TIME to = to_dd_time(stop);
        LONG channels[1] = {channel};
        std::lock_guard<std::mutex> lock(g_mutex);
        handle_ = NET_SDK_PlayBackByTime(g_uid, channels, 1, &from, &to, nullptr);
        if (handle_ >= 0)
            NET_SDK_SetPlayDataCallBack(handle_, &SdkSession::on_frame, this);
    }

    ~SdkSession() override
    {
        if (handle_ < 0)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (playback_)
            NET_SDK_StopPlayBack(handle_);
        else
            NET_SDK_StopLivePlay(handle_);
    }

    bool ok() const override { return handle_ >= 0; }

    void set_paused(bool paused) override { control(paused ? NET_SDK_PLAYCTRL_PAUSE : NET_SDK_PLAYCTRL_RESUME, 0); }

    void set_speed(int speed) override
    {
        if (speed == 1)
            control(NET_SDK_PLAYCTRL_NORMAL, 0);
        else
            control(NET_SDK_PLAYCTRL_FF, speed == 2 ? NET_SDK_RPB_SPEED_2X : NET_SDK_RPB_SPEED_4X);
    }

private:
    static void on_frame(POINTERHANDLE, NET_SDK_FRAME_INFO info, BYTE *data, void *user)
    {
        Frame frame;
        if (info.frameType == DD_FRAME_TYPE_VIDEO)
            frame.kind = Frame::Kind::Video;
        else if (info.frameType == DD_FRAME_TYPE_AUDIO)
            frame.kind = Frame::Kind::Audio;
        else
            return;
        frame.channel = static_cast<int>(info.channel);
        frame.key = info.keyFrame != 0;
        frame.time_us = static_cast<uint64_t>(info.time);
        frame.data = data;
        frame.size = info.length;
        static_cast<SdkSession *>(user)->handler_(frame);
    }

    void control(DWORD code, DWORD value)
    {
        if (handle_ < 0)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        DWORD out = 0;
        NET_SDK_PlayBackControl(handle_, code, value, &out);
    }

    bool playback_;
    FrameHandler handler_;
    POINTERHANDLE handle_ = -1;
};

class SdkDownload : public Download {
public:
    SdkDownload(int channel, time_t start, time_t stop, const std::string &path)
    {
        DD_TIME from = to_dd_time(start);
        DD_TIME to = to_dd_time(stop);
        std::lock_guard<std::mutex> lock(g_mutex);
        handle_ = NET_SDK_GetFileByTimeEx(g_uid, channel, &from, &to, const_cast<char *>(path.c_str()), FALSE);
        if (handle_ < 0)
            error_ = "the device refused the download, error " + std::to_string(NET_SDK_GetLastError());
    }

    ~SdkDownload() override
    {
        if (handle_ < 0)
            return;
        std::lock_guard<std::mutex> lock(g_mutex);
        NET_SDK_StopGetFile(handle_);
    }

    bool started() const { return handle_ >= 0; }

    int progress() override
    {
        if (handle_ < 0)
            return -1;
        std::lock_guard<std::mutex> lock(g_mutex);
        int pos = NET_SDK_GetDownloadPos(handle_);
        if (pos >= 0 && pos <= 100)
            return pos;
        error_ = pos == kDownloadNetworkError ? "network error" : "the download failed";
        return -1;
    }

    std::string error() const override { return error_; }

private:
    POINTERHANDLE handle_ = -1;
    std::string error_;
};

class SdkDevice : public Device {
public:
    explicit SdkDevice(const DeviceInfo &info) : info_(info) {}

    ~SdkDevice() override
    {
        NET_SDK_Logout(g_uid);
        NET_SDK_Cleanup();
    }

    const DeviceInfo &info() const override { return info_; }
    const char *recording_extension() const override { return "avi"; }

    std::unique_ptr<MediaSession> open_live(int channel, bool main_stream, FrameHandler handler) override
    {
        return std::make_unique<SdkSession>(channel, main_stream, std::move(handler));
    }

    std::unique_ptr<MediaSession> open_playback(int channel, time_t start, time_t stop, FrameHandler handler) override
    {
        return std::make_unique<SdkSession>(channel, start, stop, std::move(handler));
    }

    std::vector<time_t> recorded_days() override
    {
        std::vector<time_t> days;
        std::lock_guard<std::mutex> lock(g_mutex);
        POINTERHANDLE handle = NET_SDK_FindRecDate(g_uid);
        if (handle < 0)
            return days;
        int waited = 0;
        for (;;) {
            DD_DATE d;
            LONG rc = NET_SDK_FindNextRecDate(handle, &d);
            if (rc == NET_SDK_FILE_SUCCESS) {
                tm local;
                memset(&local, 0, sizeof(local));
                local.tm_year = d.year - 1900;
                local.tm_mon = d.month - 1;
                local.tm_mday = d.mday;
                local.tm_isdst = -1;
                days.push_back(mktime(&local));
                waited = 0;
            } else if (rc == NET_SDK_TRY_LATER && waited < kFindTimeoutMs) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kFindRetryMs));
                waited += kFindRetryMs;
            } else {
                break;
            }
        }
        NET_SDK_FindRecDateClose(handle);
        return days;
    }

    // An empty list on failure: the caller shows "no recording" instead of retrying.
    std::vector<Segment> segments(int channel, time_t day_start) override
    {
        std::vector<Segment> list;
        DD_TIME from = to_dd_time(day_start);
        DD_TIME to = to_dd_time(day_start + kSecondsPerDay - 1);
        POINTERHANDLE handle;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            handle = NET_SDK_FindTime(g_uid, channel, &from, &to);
        }
        if (handle < 0)
            return list;
        int waited = 0;
        for (;;) {
            NET_SDK_REC_TIME rec;
            LONG rc;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                rc = NET_SDK_FindNextTime(handle, &rec);
            }
            if (rc == NET_SDK_FILE_SUCCESS) {
                list.push_back({from_dd_time(rec.startTime), from_dd_time(rec.stopTime)});
                waited = 0;
            } else if (rc == NET_SDK_TRY_LATER && waited < kFindTimeoutMs) {
                std::this_thread::sleep_for(std::chrono::milliseconds(kFindRetryMs));
                waited += kFindRetryMs;
            } else {
                break;
            }
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        NET_SDK_FindTimeClose(handle);
        return list;
    }

    std::unique_ptr<Download> download(int channel, time_t start, time_t stop, const std::string &path) override
    {
        auto download = std::make_unique<SdkDownload>(channel, start, stop, path);
        if (!download->started())
            return nullptr;
        return download;
    }

private:
    DeviceInfo info_;
};

}  // namespace

std::unique_ptr<Device> connect_sdk(const std::string &host, int port, const std::string &user,
                                    const std::string &password, std::string &error)
{
    if (!NET_SDK_Init()) {
        error = "NET_SDK_Init failed";
        return nullptr;
    }
    NET_SDK_DEVICEINFO dev;
    memset(&dev, 0, sizeof(dev));
    g_uid = NET_SDK_Login(const_cast<char *>(host.c_str()), static_cast<WORD>(port), const_cast<char *>(user.c_str()),
                          const_cast<char *>(password.c_str()), &dev);
    if (g_uid < 0) {
        error = "login failed, SDK error " + std::to_string(NET_SDK_GetLastError());
        NET_SDK_Cleanup();
        return nullptr;
    }
    DeviceInfo info;
    info.name = std::string(dev.deviceName, strnlen(dev.deviceName, sizeof(dev.deviceName)));
    info.firmware = std::string(dev.firmwareVersion, strnlen(dev.firmwareVersion, sizeof(dev.firmwareVersion)));
    info.video_channels = dev.videoInputNum;
    info.audio_inputs = dev.audioInputNum;
    return std::make_unique<SdkDevice>(info);
}

}  // namespace dvr

#endif  // DVR_WITH_SDK
