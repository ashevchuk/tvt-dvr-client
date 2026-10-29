#include "connect_screen.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include <raylib.h>

#include "theme.h"
#include "widgets.h"

namespace {

const int MAX_NAME_BYTES = 64;
const int MAX_HOST_BYTES = 253;
const int MAX_PORT_DIGITS = 5;
const int MAX_USER_BYTES = 64;
const int MAX_PASSWORD_BYTES = 128;
const int MAX_PORT = 65535;

const int LIST_WIDTH = 300;
const int ROW_HEIGHT = 44;
const int LIST_TOP = 56;
const float FORM_X = 340;
const float FORM_TOP = 90;
const float FIELD_HEIGHT = 32;
const float FIELD_STRIDE = 62;
const float FORM_MAX_WIDTH = 520;
const float PORT_FIELD_WIDTH = 120;
const float CHECKBOX_Y = 410;
const float BUTTON_Y = 500;
const Color ERROR_COLOR = {255, 90, 80, 255};

const char *const FIELD_LABELS[ConnectScreen::FIELD_COUNT] = {"Name", "Host", "Port", "User", "Password"};

Rectangle field_rect(int field)
{
    float width = std::min(FORM_MAX_WIDTH, GetScreenWidth() - FORM_X - 40);
    if (field == ConnectScreen::PORT)
        width = PORT_FIELD_WIDTH;
    return {FORM_X, FORM_TOP + field * FIELD_STRIDE + 20, width, FIELD_HEIGHT};
}

void show_message(ConnectScreen &s, const std::string &text, bool is_error)
{
    s.message = text;
    s.message_is_error = is_error;
}

void fill_form(ConnectScreen &s, int index)
{
    const SavedDevice blank;
    s.selected = index;
    const SavedDevice &d = index >= 0 ? s.saved.devices[index] : blank;
    s.fields[ConnectScreen::NAME].set(d.name);
    s.fields[ConnectScreen::HOST].set(d.host);
    s.fields[ConnectScreen::PORT].set(std::to_string(d.port));
    s.fields[ConnectScreen::USER].set(d.user);
    s.fields[ConnectScreen::PASSWORD].set(d.remember_password ? d.password : "");
    s.remember_password = d.remember_password;
    s.focus = index >= 0 && !d.remember_password ? ConnectScreen::PASSWORD : ConnectScreen::HOST;
}

bool reject(ConnectScreen &s, int field, const char *text)
{
    s.focus = field;
    show_message(s, text, true);
    return false;
}

void start_connect(ConnectScreen &s)
{
    const std::string host = s.fields[ConnectScreen::HOST].text();
    const std::string user = s.fields[ConnectScreen::USER].text();
    const std::string password = s.fields[ConnectScreen::PASSWORD].text();
    const int port = atoi(s.fields[ConnectScreen::PORT].text().c_str());
    if (host.empty())
        reject(s, ConnectScreen::HOST, "Enter the host");
    else if (port < 1 || port > MAX_PORT)
        reject(s, ConnectScreen::PORT, "The port must be between 1 and 65535");
    else if (user.empty())
        reject(s, ConnectScreen::USER, "Enter the user name");
    else {
        SavedDevice d;
        d.name = s.fields[ConnectScreen::NAME].empty() ? host : s.fields[ConnectScreen::NAME].text();
        d.host = host;
        d.port = port;
        d.user = user;
        d.remember_password = s.remember_password;
        if (d.remember_password)
            d.password = password;
        s.attempted = d;
        show_message(s, "Connecting to " + host + ":" + std::to_string(port) + "...", false);
        s.connecting = true;
        s.attempt = std::async(std::launch::async, [host, port, user, password] {
            ConnectOutcome outcome;
            outcome.device = dvr::connect_native(host, port, user, password, outcome.error);
            return outcome;
        });
    }
}

void persist(ConnectScreen &s)
{
    std::string error;
    if (!s.file.empty() && !save_device_list(s.file, s.saved, error))
        fprintf(stderr, "settings not saved: %s\n", error.c_str());
}

std::unique_ptr<dvr::Device> finish_connect(ConnectScreen &s)
{
    ConnectOutcome outcome = s.attempt.get();
    s.connecting = false;
    SavedDevice entry = std::move(s.attempted);
    s.attempted = SavedDevice();
    if (!outcome.device) {
        show_message(s, outcome.error, true);
        return nullptr;
    }

    int index = s.selected;
    if (index >= 0 && index < static_cast<int>(s.saved.devices.size())) {
        s.saved.devices[index] = entry;
    } else {
        s.saved.devices.push_back(entry);
        index = static_cast<int>(s.saved.devices.size()) - 1;
    }
    s.selected = index;
    s.saved.last_used = index;
    persist(s);
    s.fields[ConnectScreen::PASSWORD].set("");
    show_message(s, "", false);
    return std::move(outcome.device);
}

void delete_selected(ConnectScreen &s)
{
    int index = s.selected;
    s.saved.devices.erase(s.saved.devices.begin() + index);
    if (s.saved.last_used == index)
        s.saved.last_used = -1;
    else if (s.saved.last_used > index)
        s.saved.last_used--;
    persist(s);
    fill_form(s, -1);
    show_message(s, "", false);
}

void handle_keys(ConnectScreen &s)
{
    if (s.connecting) {
        while (GetCharPressed() != 0) {
        }
        return;
    }
    if (IsKeyPressed(KEY_TAB)) {
        bool backwards = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
        s.focus = (s.focus + (backwards ? ConnectScreen::FIELD_COUNT - 1 : 1)) % ConnectScreen::FIELD_COUNT;
    }
    edit_text(s.fields[s.focus], s.focus == ConnectScreen::PORT);
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
        start_connect(s);
}

void draw_list(ConnectScreen &s)
{
    DrawRectangle(0, 0, LIST_WIDTH, GetScreenHeight(), PANEL);
    DrawText("Devices", 16, 20, 20, RAYWHITE);
    if (s.saved.devices.empty())
        DrawText("No saved devices", 16, LIST_TOP + 12, 16, GRAY);

    for (int i = 0; i < static_cast<int>(s.saved.devices.size()); i++) {
        const SavedDevice &d = s.saved.devices[i];
        Rectangle row = {0, (float)(LIST_TOP + i * ROW_HEIGHT), (float)LIST_WIDTH, (float)ROW_HEIGHT};
        if (i == s.selected)
            DrawRectangleRec(row, ROW_HIGHLIGHT);
        if (i == s.saved.last_used)
            DrawRectangle(0, (int)row.y, 4, ROW_HEIGHT, ACCENT);
        BeginScissorMode(16, (int)row.y, LIST_WIDTH - 24, ROW_HEIGHT);
        DrawText(d.name.c_str(), 16, (int)row.y + 6, 17, i == s.selected ? ACCENT : LIGHTGRAY);
        DrawText(TextFormat("%s@%s:%d", d.user.c_str(), d.host.c_str(), d.port), 16, (int)row.y + 26, 13, GRAY);
        EndScissorMode();
        if (!s.connecting && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), row))
            fill_form(s, i);
    }

    if (button({16, (float)GetScreenHeight() - 50, LIST_WIDTH - 32.0f, 34}, "New device", false, !s.connecting)) {
        fill_form(s, -1);
        show_message(s, "", false);
    }
}

void draw_form(ConnectScreen &s)
{
    DrawText(s.selected >= 0 ? "Edit device" : "New device", (int)FORM_X, 30, 22, RAYWHITE);

    for (int f = 0; f < ConnectScreen::FIELD_COUNT; f++) {
        Rectangle r = field_rect(f);
        DrawText(FIELD_LABELS[f], (int)r.x, (int)r.y - 18, 14, GRAY);
        draw_text_field(r, s.fields[f], f == s.focus, f == ConnectScreen::PASSWORD);
        if (!s.connecting && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), r))
            s.focus = f;
    }

    Rectangle box = {FORM_X, CHECKBOX_Y, 20, 20};
    DrawRectangleRec(box, Color{40, 42, 46, 255});
    DrawRectangleLinesEx(box, 1, Color{70, 72, 78, 255});
    if (s.remember_password)
        DrawRectangle((int)box.x + 4, (int)box.y + 4, 12, 12, ACCENT);
    DrawText("Remember password", (int)FORM_X + 30, (int)CHECKBOX_Y + 2, 16, RAYWHITE);
    Rectangle toggle = {FORM_X, CHECKBOX_Y, 220, 20};
    if (!s.connecting && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), toggle))
        s.remember_password = !s.remember_password;
    if (s.remember_password && !s.file.empty()) {
        DrawText("The password is stored unencrypted, readable only by you, in:", (int)FORM_X, (int)CHECKBOX_Y + 30, 13, ORANGE);
        DrawText(s.file.c_str(), (int)FORM_X, (int)CHECKBOX_Y + 48, 13, ORANGE);
    }

    if (button({FORM_X, BUTTON_Y, 140, 38}, "Connect", true, !s.connecting))
        start_connect(s);
    if (button({FORM_X + 150, BUTTON_Y, 110, 38}, "Delete", false, !s.connecting && s.selected >= 0))
        delete_selected(s);
    if (!s.message.empty())
        DrawText(s.message.c_str(), (int)FORM_X, (int)BUTTON_Y + 54, 16, s.message_is_error ? ERROR_COLOR : LIGHTGRAY);
}

} // namespace

ConnectScreen::ConnectScreen()
{
    fields[NAME] = TextBuffer(MAX_NAME_BYTES);
    fields[HOST] = TextBuffer(MAX_HOST_BYTES);
    fields[PORT] = TextBuffer(MAX_PORT_DIGITS);
    fields[USER] = TextBuffer(MAX_USER_BYTES);
    fields[PASSWORD] = TextBuffer(MAX_PASSWORD_BYTES);
    file = default_device_file();
    if (file.empty())
        show_message(*this, "No config directory found: devices will not be saved", true);
    else
        saved = load_device_list(file);
    fill_form(*this, saved.last_used);
}

void enter_connect_screen(ConnectScreen &screen)
{
    fill_form(screen, screen.selected);
    screen.message.clear();
}

std::unique_ptr<dvr::Device> update_connect_screen(ConnectScreen &screen)
{
    std::unique_ptr<dvr::Device> device;
    if (screen.connecting && screen.attempt.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        device = finish_connect(screen);
    handle_keys(screen);

    BeginDrawing();
    ClearBackground(BG);
    draw_list(screen);
    draw_form(screen);
    EndDrawing();
    return device;
}
