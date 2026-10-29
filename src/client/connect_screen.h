#pragma once

#include <future>
#include <memory>
#include <string>

#include "device/device.h"
#include "device_store.h"
#include "double_click.h"
#include "text_buffer.h"

struct ConnectOutcome {
    std::unique_ptr<dvr::Device> device;
    std::string error;
};

// The screen that lists the saved devices and connects to one of them.
struct ConnectScreen {
    enum Field { NAME, HOST, PORT, USER, PASSWORD, FIELD_COUNT };

    ConnectScreen();

    DeviceList saved;
    std::string file;
    int selected = -1;                // index into saved.devices, -1 while a new device is being entered
    TextBuffer fields[FIELD_COUNT];
    bool remember_password = false;
    int focus = HOST;
    float list_scroll = 0;            // pixels the device list is scrolled down
    DoubleClick row_click;
    double delete_armed_until = 0;    // Delete needs a second click before this time (GetTime())

    std::future<ConnectOutcome> attempt;
    bool connecting = false;
    SavedDevice attempted;            // stored if the attempt succeeds
    std::string message;
    bool message_is_error = false;
};

// Reloads the form from the selected device; call it each time the screen is shown again.
void enter_connect_screen(ConnectScreen &screen);

// Handles input and draws one frame. Returns the device once a connection succeeds.
std::unique_ptr<dvr::Device> update_connect_screen(ConnectScreen &screen);
