#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "wire.h"

namespace dvr {

// One logged-in TCP connection to the device. A reader thread decodes incoming messages, hands
// media frames to a handler and keeps the connection alive.
class Connection {
public:
    // Called on the reader thread; `data` is only valid during the call.
    using MediaHandler = std::function<void(const wire::MediaFrame &)>;

    static std::unique_ptr<Connection> open(const std::string &host, int port, const std::string &user,
                                            const std::string &password, std::string &error);
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    const wire::DeviceInfo &info() const { return info_; }
    bool alive() const { return alive_; }

    void set_media_handler(MediaHandler handler);
    bool send(const std::vector<uint8_t> &frame);
    // Sends a request and waits for the next message with `reply_cmd`. One request at a time.
    bool request(const std::vector<uint8_t> &frame, uint32_t reply_cmd, wire::Message &reply, int timeout_ms);

private:
    Connection(int fd, wire::Decoder decoder, const wire::DeviceInfo &info);
    void read_loop();
    void dispatch(const wire::Message &message);

    int fd_;
    wire::DeviceInfo info_;
    wire::Decoder decoder_;
    std::atomic<bool> alive_{true};
    std::thread reader_;

    std::mutex send_mutex_;
    std::mutex handler_mutex_;
    MediaHandler media_handler_;

    std::mutex request_mutex_;
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    uint32_t wait_cmd_ = 0;
    bool reply_ready_ = false;
    wire::Message reply_;
};

}  // namespace dvr
