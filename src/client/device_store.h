#pragma once

#include <string>
#include <vector>

struct SavedDevice {
    std::string name;
    std::string host;
    int port = 6036;
    std::string user;
    bool remember_password = false;
    std::string password;     // kept only while remember_password is set
};

struct DeviceList {
    std::vector<SavedDevice> devices;
    int last_used = -1;       // index into devices, -1 if none
};

// The file is a list of "key=value" lines: "last=<index>" first, then one "[device]" section per
// device. Values are taken verbatim up to the end of the line, so passwords keep their spaces.
DeviceList parse_device_list(const std::string &text);
std::string serialize_device_list(const DeviceList &list);

// $XDG_CONFIG_HOME/tvt-dvr-client/devices.conf, or ~/.config/... ; empty if neither is known.
std::string default_device_file();

// A missing file is an empty list. Saving writes a new file readable by the owner only and
// renames it over the old one.
DeviceList load_device_list(const std::string &path);
bool save_device_list(const std::string &path, const DeviceList &list, std::string &error);
