#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "device.h"
#include "proto/connection.h"
#include "recording_writer.h"

namespace dvr {

namespace {

constexpr int kRequestTimeoutMs = 8000;
constexpr int kPacerTickMs = 5;
constexpr uint32_t kDownloadAckEvery = 8;
constexpr uint32_t kSecondsPerDay = 86400;

Frame to_frame(const wire::MediaFrame &f)
{
    Frame out;
    out.kind = f.kind == wire::Kind::Audio ? Frame::Kind::Audio
               : f.kind == wire::Kind::End ? Frame::Kind::End
                                           : Frame::Kind::Video;
    out.channel = f.channel;
    out.key = f.key;
    out.time_us = f.time_us;
    out.data = f.data;
    out.size = f.size;
    return out;
}

bool is_media(const wire::MediaFrame &f)
{
    return f.kind == wire::Kind::Video || f.kind == wire::Kind::Audio;
}

using Connector = std::function<std::unique_ptr<Connection>(std::string &)>;

// The live channels of a device share one connection; each stream asks for its channel and the
// hub keeps the device's channel set in step and routes the frames.
class LiveHub {
public:
    explicit LiveHub(Connector connector) : connector_(std::move(connector)) {}

    int add(int channel, bool main_stream, FrameHandler handler)
    {
        std::unique_ptr<Connection> fresh;
        if (!usable()) {
            std::string error;
            fresh = connector_(error);
            if (!fresh)
                return -1;
            fresh->set_media_handler([this](const wire::MediaFrame &f) { route(f); });
        }
        // Destroyed after the lock is released: a dead connection joins its reader thread, which may
        // be waiting for mutex_ inside route().
        std::unique_ptr<Connection> stale;
        std::lock_guard<std::mutex> lock(mutex_);
        if (fresh) {
            stale = std::move(connection_);
            connection_ = std::move(fresh);
            running_ = false;
        }
        int id = next_id_++;
        subscribers_.push_back({id, channel, main_stream, std::move(handler)});
        apply();
        return id;
    }

    // Replaces a lost connection and resubscribes the streams that were running on it. Does
    // nothing while nobody is subscribed, since add() connects on demand.
    bool ensure(std::string &error)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (subscribers_.empty() || !connection_ || connection_->alive())
                return true;
        }
        std::unique_ptr<Connection> fresh = connector_(error);
        if (!fresh)
            return false;
        fresh->set_media_handler([this](const wire::MediaFrame &f) { route(f); });
        std::unique_ptr<Connection> unused;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (connection_ && connection_->alive()) {
                unused = std::move(fresh);      // add() restored it in the meantime
            } else {
                unused = std::move(connection_);
                connection_ = std::move(fresh);
                running_ = false;
                apply();
            }
        }
        return true;
    }

    void remove(int id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(std::remove_if(subscribers_.begin(), subscribers_.end(), [id](const Subscriber &s) { return s.id == id; }),
                           subscribers_.end());
        apply();
    }

    bool alive()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_ && connection_->alive();
    }

    // False while streams are subscribed on a connection that was lost.
    bool healthy()
    {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        return !lock.owns_lock() || subscribers_.empty() || (connection_ && connection_->alive());
    }

private:
    struct Subscriber {
        int id;
        int channel;
        bool main_stream;
        FrameHandler handler;
    };

    bool usable()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_ && connection_->alive();
    }

    // Requires mutex_.
    void apply()
    {
        if (!connection_)
            return;
        uint32_t main_mask = 0, sub_mask = 0;
        for (const Subscriber &s : subscribers_)
            (s.main_stream ? main_mask : sub_mask) |= 1u << s.channel;
        bool any = main_mask || sub_mask;
        if (!any && !running_)
            return;
        connection_->send(wire::live_streams(running_, main_mask, sub_mask));
        running_ = any;
    }

    void route(const wire::MediaFrame &f)
    {
        if (!is_media(f))
            return;
        Frame frame = to_frame(f);
        bool audio_delivered = false;
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Subscriber &s : subscribers_) {
            if (s.channel != f.channel)
                continue;
            if (f.kind == wire::Kind::Video && s.main_stream != (f.stream == wire::Stream::Main))
                continue;
            // The device tags all audio as the main stream; one copy per channel is enough.
            if (f.kind == wire::Kind::Audio) {
                if (audio_delivered)
                    continue;
                audio_delivered = true;
            }
            s.handler(frame);
        }
    }

    Connector connector_;
    std::mutex mutex_;
    std::unique_ptr<Connection> connection_;
    std::vector<Subscriber> subscribers_;
    int next_id_ = 1;
    bool running_ = false;
};

class NativeLive : public MediaSession {
public:
    NativeLive(LiveHub &hub, int channel, bool main_stream, FrameHandler handler)
        : hub_(hub), id_(hub.add(channel, main_stream, std::move(handler)))
    {
    }
    ~NativeLive() override
    {
        if (id_ >= 0)
            hub_.remove(id_);
    }
    bool ok() const override { return id_ >= 0 && hub_.alive(); }
    void set_paused(bool) override {}
    void set_speed(int) override {}

private:
    LiveHub &hub_;
    int id_;
};

// One recorded stream on its own connection. The device sends only what has been acknowledged
// (about 24 messages ahead), so acknowledging at the pace of the recording plays it in real time,
// faster acknowledgements play it faster and none pause it.
class NativePlayback : public MediaSession {
public:
    NativePlayback(std::unique_ptr<Connection> connection, int channel, time_t start, time_t stop, FrameHandler handler)
        : connection_(std::move(connection)), handler_(std::move(handler)), position_us_(static_cast<double>(start) * 1e6)
    {
        connection_->set_media_handler([this](const wire::MediaFrame &f) { on_frame(f); });
        connection_->send(wire::play_start(wire::kFirstPlaybackSession, channel, static_cast<uint32_t>(start),
                                           static_cast<uint32_t>(stop), false));
        pacer_ = std::thread([this] { pace(); });
    }

    ~NativePlayback() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        pacer_.join();
        connection_->send(wire::play_stop(wire::kFirstPlaybackSession));
        connection_.reset();     // joins the reader thread: no frame handler runs after this
    }

    bool ok() const override { return connection_->alive(); }

    void set_paused(bool paused) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        paused_ = paused;
    }

    void set_speed(int speed) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        speed_ = speed;
    }

private:
    struct Pending {
        uint32_t sequence;
        uint64_t time_us;
    };

    void on_frame(const wire::MediaFrame &f)
    {
        if (f.kind == wire::Kind::End) {
            handler_(to_frame(f));
            return;
        }
        if (!is_media(f))
            return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back({++sequence_, f.time_us});
        }
        handler_(to_frame(f));
    }

    void pace()
    {
        auto last = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stopping_) {
            cv_.wait_for(lock, std::chrono::milliseconds(kPacerTickMs));
            auto now = std::chrono::steady_clock::now();
            double elapsed_us = std::chrono::duration<double, std::micro>(now - last).count();
            last = now;
            if (!paused_)
                position_us_ += elapsed_us * speed_;
            uint32_t due = 0;
            while (!paused_ && !pending_.empty() && pending_.front().time_us <= position_us_) {
                due = pending_.front().sequence;
                pending_.pop_front();
            }
            if (due) {
                lock.unlock();
                connection_->send(wire::play_ack(wire::kFirstPlaybackSession, due));
                lock.lock();
            }
        }
    }

    std::unique_ptr<Connection> connection_;
    FrameHandler handler_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Pending> pending_;
    uint32_t sequence_ = 0;
    double position_us_;
    bool paused_ = false;
    int speed_ = 1;
    bool stopping_ = false;
    std::thread pacer_;
};

// A playback acknowledged without pacing, written to a file.
class NativeDownload : public Download {
public:
    NativeDownload(std::unique_ptr<Connection> connection, std::unique_ptr<RecordingWriter> writer, int channel,
                   time_t start, time_t stop)
        : connection_(std::move(connection)), writer_(std::move(writer)),
          start_us_(static_cast<uint64_t>(start) * 1000000), length_us_(std::max<uint64_t>(1, static_cast<uint64_t>(stop - start) * 1000000))
    {
        connection_->set_media_handler([this](const wire::MediaFrame &f) { on_frame(f); });
        connection_->send(wire::play_start(wire::kDownloadSession, channel, static_cast<uint32_t>(start),
                                           static_cast<uint32_t>(stop), true));
    }

    ~NativeDownload() override
    {
        connection_->send(wire::play_stop(wire::kDownloadSession));
        connection_.reset();     // no frame handler runs after this
        std::string ignored;
        writer_->finish(ignored);
    }

    int progress() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == State::Running && !connection_->alive()) {
            state_ = State::Failed;
            error_ = "the connection to the device was lost";
        }
        return state_ == State::Done ? 100 : state_ == State::Failed ? -1 : percent_;
    }

    std::string error() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_;
    }

private:
    enum class State { Running, Done, Failed };

    void on_frame(const wire::MediaFrame &f)
    {
        if (f.kind == wire::Kind::End) {
            std::string error;
            bool ok = writer_->finish(error);
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = ok ? State::Done : State::Failed;
            error_ = error;
            return;
        }
        if (!is_media(f))
            return;
        if (f.kind == wire::Kind::Video)
            writer_->add_video(f.data, f.size, f.key, f.width, f.height, f.time_us);
        else
            writer_->add_audio(f.data, f.size, f.time_us);
        if (++messages_ % kDownloadAckEvery == 0)
            connection_->send(wire::play_ack(wire::kDownloadSession, messages_));
        if (f.kind == wire::Kind::Video && f.time_us > start_us_) {
            std::lock_guard<std::mutex> lock(mutex_);
            percent_ = static_cast<int>(std::min<uint64_t>(99, (f.time_us - start_us_) * 100 / length_us_));
        }
    }

    std::unique_ptr<Connection> connection_;
    std::unique_ptr<RecordingWriter> writer_;
    const uint64_t start_us_;
    const uint64_t length_us_;
    mutable std::mutex mutex_;
    State state_ = State::Running;
    int percent_ = 0;
    std::string error_;
    uint32_t messages_ = 0;
};

class NativeDevice : public Device {
public:
    NativeDevice(std::string host, int port, std::string user, std::string password, std::unique_ptr<Connection> control)
        : host_(std::move(host)), port_(port), user_(std::move(user)), password_(std::move(password)),
          control_(std::move(control)), info_(to_info(control_->info())),
          live_([this](std::string &error) { return open_connection(error); })
    {
    }

    const DeviceInfo &info() const override { return info_; }
    const char *recording_extension() const override { return "mkv"; }

    std::unique_ptr<MediaSession> open_live(int channel, bool main_stream, FrameHandler handler) override
    {
        return std::make_unique<NativeLive>(live_, channel, main_stream, std::move(handler));
    }

    std::unique_ptr<MediaSession> open_playback(int channel, time_t start, time_t stop, FrameHandler handler) override
    {
        std::string error;
        auto connection = open_connection(error);
        if (!connection)
            return nullptr;
        return std::make_unique<NativePlayback>(std::move(connection), channel, start, stop, std::move(handler));
    }

    std::vector<time_t> recorded_days() override
    {
        wire::Message reply;
        std::vector<time_t> days;
        if (search(wire::search_days(), reply))
            for (uint32_t d : wire::parse_days(reply))
                days.push_back(d);
        return days;
    }

    std::vector<Segment> segments(int channel, time_t day_start) override
    {
        wire::Message reply;
        std::vector<Segment> segments;
        uint32_t start = static_cast<uint32_t>(day_start);
        if (search(wire::search_segments(channel, start, start + kSecondsPerDay - 1), reply))
            for (const wire::SegmentTimes &s : wire::parse_segments(reply))
                segments.push_back({static_cast<time_t>(s.start), static_cast<time_t>(s.stop)});
        return segments;
    }

    std::unique_ptr<Download> download(int channel, time_t start, time_t stop, const std::string &path) override
    {
        std::string error;
        auto writer = RecordingWriter::create(path, channel < info_.audio_inputs, error);
        if (!writer)
            return nullptr;
        auto connection = open_connection(error);
        if (!connection)
            return nullptr;
        return std::make_unique<NativeDownload>(std::move(connection), std::move(writer), channel, start, stop);
    }

    bool connected() override
    {
        if (!live_.healthy())
            return false;
        // A search in progress holds the lock and proves the connection works.
        std::unique_lock<std::mutex> lock(control_mutex_, std::try_to_lock);
        return !lock.owns_lock() || control_->alive();
    }

    bool reconnect(std::string &error) override
    {
        {
            std::lock_guard<std::mutex> lock(control_mutex_);
            if (!control_->alive()) {
                auto fresh = open_connection(error);
                if (!fresh)
                    return false;
                control_ = std::move(fresh);
            }
        }
        return live_.ensure(error);
    }

private:
    static DeviceInfo to_info(const wire::DeviceInfo &w)
    {
        return {w.name, w.firmware, w.video_channels, w.audio_inputs};
    }

    std::unique_ptr<Connection> open_connection(std::string &error)
    {
        return Connection::open(host_, port_, user_, password_, error);
    }

    // Searches go over one shared connection, which is reopened if it was lost.
    bool search(const std::vector<uint8_t> &request, wire::Message &reply)
    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        if (!control_->alive()) {
            std::string error;
            auto fresh = open_connection(error);
            if (!fresh)
                return false;
            control_ = std::move(fresh);
        }
        return control_->request(request, wire::kCmdSearchReply, reply, kRequestTimeoutMs);
    }

    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    std::mutex control_mutex_;
    std::unique_ptr<Connection> control_;
    DeviceInfo info_;
    LiveHub live_;
};

}  // namespace

std::unique_ptr<Device> connect_native(const std::string &host, int port, const std::string &user,
                                       const std::string &password, std::string &error)
{
    auto control = Connection::open(host, port, user, password, error);
    if (!control)
        return nullptr;
    return std::make_unique<NativeDevice>(host, port, user, password, std::move(control));
}

}  // namespace dvr
