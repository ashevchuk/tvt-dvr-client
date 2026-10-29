#include "client/device_store.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int MIN_PORT = 1;
constexpr int MAX_PORT = 65535;

std::string strip_line_ending(std::string line)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
        line.pop_back();
    return line;
}

bool parse_int(const std::string &s, int &out)
{
    char *end = nullptr;
    errno = 0;
    long v = strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0' || errno != 0 || v < 0 || v > MAX_PORT)
        return false;
    out = static_cast<int>(v);
    return true;
}

// Control characters would break the line format; the UI never produces them.
std::string single_line(const std::string &s)
{
    std::string out;
    for (char c : s)
        if (c != '\n' && c != '\r')
            out += c;
    return out;
}

bool make_directory(const std::string &path)
{
    return mkdir(path.c_str(), 0700) == 0 || errno == EEXIST;
}

} // namespace

DeviceList parse_device_list(const std::string &text)
{
    DeviceList list;
    int last = -1;
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
        std::string line = strip_line_ending(raw);
        if (line == "[device]") {
            list.devices.emplace_back();
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (list.devices.empty()) {
            if (key == "last")
                parse_int(value, last);
            continue;
        }
        SavedDevice &d = list.devices.back();
        int number = 0;
        if (key == "name")
            d.name = value;
        else if (key == "host")
            d.host = value;
        else if (key == "port" && parse_int(value, number) && number >= MIN_PORT)
            d.port = number;
        else if (key == "user")
            d.user = value;
        else if (key == "remember_password")
            d.remember_password = value == "1";
        else if (key == "password")
            d.password = value;
    }
    for (SavedDevice &d : list.devices)
        if (!d.remember_password)
            d.password.clear();
    if (last >= 0 && last < static_cast<int>(list.devices.size()))
        list.last_used = last;
    return list;
}

std::string serialize_device_list(const DeviceList &list)
{
    std::string out;
    if (list.last_used >= 0 && list.last_used < static_cast<int>(list.devices.size()))
        out += "last=" + std::to_string(list.last_used) + "\n";
    for (const SavedDevice &d : list.devices) {
        out += "\n[device]\n";
        out += "name=" + single_line(d.name) + "\n";
        out += "host=" + single_line(d.host) + "\n";
        out += "port=" + std::to_string(d.port) + "\n";
        out += "user=" + single_line(d.user) + "\n";
        out += std::string("remember_password=") + (d.remember_password ? "1" : "0") + "\n";
        if (d.remember_password)
            out += "password=" + single_line(d.password) + "\n";
    }
    return out;
}

std::string default_device_file()
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    std::string base;
    if (xdg && *xdg == '/')
        base = xdg;
    else if (home && *home)
        base = std::string(home) + "/.config";
    else
        return "";
    return base + "/tvt-dvr-client/devices.conf";
}

DeviceList load_device_list(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    std::ostringstream text;
    text << file.rdbuf();
    return parse_device_list(text.str());
}

bool save_device_list(const std::string &path, const DeviceList &list, std::string &error)
{
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) {
        error = "invalid path: " + path;
        return false;
    }
    std::string directory = path.substr(0, slash);
    size_t parent_end = directory.rfind('/');
    if (parent_end != std::string::npos && parent_end > 0)
        make_directory(directory.substr(0, parent_end));
    if (!make_directory(directory)) {
        error = "cannot create " + directory + ": " + strerror(errno);
        return false;
    }

    std::string temporary = path + ".tmp";
    int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        error = "cannot write " + temporary + ": " + strerror(errno);
        return false;
    }
    // An old temporary file may have looser permissions than the mode passed to open().
    bool ok = fchmod(fd, 0600) == 0;
    std::string data = serialize_device_list(list);
    for (size_t done = 0; ok && done < data.size();) {
        ssize_t n = write(fd, data.data() + done, data.size() - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            ok = false;
        else
            done += static_cast<size_t>(n);
    }
    ok = ok && fsync(fd) == 0;
    int saved_errno = errno;
    ok = (close(fd) == 0) && ok;
    if (ok && rename(temporary.c_str(), path.c_str()) != 0) {
        saved_errno = errno;
        ok = false;
    }
    if (!ok) {
        unlink(temporary.c_str());
        error = "cannot save " + path + ": " + strerror(saved_errno);
    }
    return ok;
}
