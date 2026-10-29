#include "connection.h"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dvr {

namespace {

constexpr int kConnectTimeoutMs = 5000;
constexpr int kHandshakeTimeoutMs = 5000;
constexpr int kPollMs = 500;
constexpr size_t kReadBytes = 64 << 10;

bool wait_for(int fd, short events, int timeout_ms)
{
    pollfd p = {fd, events, 0};
    int rc;
    do {
        rc = poll(&p, 1, timeout_ms);
    } while (rc < 0 && errno == EINTR);
    return rc > 0 && (p.revents & events);
}

int connect_to(const std::string &host, int port, std::string &error)
{
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *list = nullptr;
    int rc = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &list);
    if (rc != 0) {
        error = std::string("cannot resolve ") + host + ": " + gai_strerror(rc);
        return -1;
    }
    int fd = -1;
    for (addrinfo *ai = list; ai && fd < 0; ai = ai->ai_next) {
        int s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0)
            continue;
        fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
        bool connected = connect(s, ai->ai_addr, ai->ai_addrlen) == 0;
        if (!connected && errno == EINPROGRESS && wait_for(s, POLLOUT, kConnectTimeoutMs)) {
            int err = 0;
            socklen_t len = sizeof(err);
            getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len);
            connected = err == 0;
            errno = err;
        }
        if (connected) {
            fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);
            fd = s;
        } else {
            close(s);
        }
    }
    freeaddrinfo(list);
    if (fd < 0) {
        error = "cannot connect to " + host + ":" + std::to_string(port);
    } else {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    return fd;
}

bool read_greeting(int fd)
{
    uint8_t buf[wire::kGreetingBytes];
    size_t got = 0;
    while (got < sizeof(buf)) {
        if (!wait_for(fd, POLLIN, kHandshakeTimeoutMs))
            return false;
        ssize_t n = recv(fd, buf + got, sizeof(buf) - got, 0);
        if (n <= 0)
            return false;
        got += n;
    }
    return memcmp(buf, "head", 4) == 0;
}

bool send_all(int fd, const uint8_t *data, size_t size)
{
    while (size > 0) {
        ssize_t n = ::send(fd, data, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        data += n;
        size -= n;
    }
    return true;
}

}  // namespace

std::unique_ptr<Connection> Connection::open(const std::string &host, int port, const std::string &user,
                                             const std::string &password, std::string &error)
{
    int fd = connect_to(host, port, error);
    if (fd < 0)
        return nullptr;
    if (!read_greeting(fd)) {
        error = "the device did not send its greeting";
        close(fd);
        return nullptr;
    }
    auto request = wire::login(user, password);
    if (!send_all(fd, request.data(), request.size())) {
        error = "connection lost while logging in";
        close(fd);
        return nullptr;
    }

    wire::Decoder decoder;
    wire::Message reply;
    bool have_reply = false;
    uint8_t buf[4096];
    while (!have_reply) {
        if (!wait_for(fd, POLLIN, kHandshakeTimeoutMs)) {
            error = "no reply to the login request";
            close(fd);
            return nullptr;
        }
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            error = "connection closed while logging in";
            close(fd);
            return nullptr;
        }
        decoder.push(buf, n);
        have_reply = decoder.next(reply);
        if (decoder.failed()) {
            error = "unexpected data from the device";
            close(fd);
            return nullptr;
        }
    }
    wire::DeviceInfo info;
    if (!wire::parse_login_reply(reply, info)) {
        error = "login rejected (wrong user name or password?)";
        close(fd);
        return nullptr;
    }
    return std::unique_ptr<Connection>(new Connection(fd, std::move(decoder), info));
}

Connection::Connection(int fd, wire::Decoder decoder, const wire::DeviceInfo &info)
    : fd_(fd), info_(info), decoder_(std::move(decoder))
{
    reader_ = std::thread([this] { read_loop(); });
}

Connection::~Connection()
{
    alive_ = false;
    shutdown(fd_, SHUT_RDWR);
    reader_.join();
    close(fd_);
}

void Connection::set_media_handler(MediaHandler handler)
{
    std::lock_guard<std::mutex> lock(handler_mutex_);
    media_handler_ = std::move(handler);
}

bool Connection::send(const std::vector<uint8_t> &frame)
{
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (!alive_ || !send_all(fd_, frame.data(), frame.size())) {
        alive_ = false;
        return false;
    }
    return true;
}

bool Connection::request(const std::vector<uint8_t> &frame, uint32_t reply_cmd, wire::Message &reply, int timeout_ms)
{
    std::lock_guard<std::mutex> serial(request_mutex_);
    {
        std::lock_guard<std::mutex> lock(wait_mutex_);
        wait_cmd_ = reply_cmd;
        reply_ready_ = false;
    }
    if (!send(frame))
        return false;
    std::unique_lock<std::mutex> lock(wait_mutex_);
    bool ok = wait_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return reply_ready_ || !alive_; });
    wait_cmd_ = 0;
    if (!ok || !reply_ready_)
        return false;
    reply = std::move(reply_);
    return true;
}

void Connection::dispatch(const wire::Message &message)
{
    if (message.cmd == wire::kCmdMedia) {
        wire::MediaFrame frame;
        if (!wire::parse_media(message, frame))
            return;
        std::lock_guard<std::mutex> lock(handler_mutex_);
        if (media_handler_)
            media_handler_(frame);
        return;
    }
    std::lock_guard<std::mutex> lock(wait_mutex_);
    if (wait_cmd_ != 0 && message.cmd == wait_cmd_) {
        reply_ = message;
        reply_ready_ = true;
        wait_cv_.notify_all();
    }
}

void Connection::read_loop()
{
    std::vector<uint8_t> buf(kReadBytes);
    auto last_sent = std::chrono::steady_clock::now();
    while (alive_) {
        if (std::chrono::steady_clock::now() - last_sent >= std::chrono::seconds(wire::kKeepaliveSeconds)) {
            send(wire::keepalive());
            last_sent = std::chrono::steady_clock::now();
        }
        if (!wait_for(fd_, POLLIN, kPollMs))
            continue;
        ssize_t n = recv(fd_, buf.data(), buf.size(), 0);
        if (n <= 0)
            break;
        decoder_.push(buf.data(), n);
        wire::Message message;
        while (decoder_.next(message))
            dispatch(message);
        if (decoder_.failed())
            break;
    }
    alive_ = false;
    std::lock_guard<std::mutex> lock(wait_mutex_);
    wait_cv_.notify_all();
}

}  // namespace dvr
