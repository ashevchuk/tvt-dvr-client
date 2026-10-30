#include "widgets.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>

#include "theme.h"

static const int BUTTON_FONT = 16;
static const int FIELD_FONT = 18;
static const int FIELD_PADDING = 8;
static const double CURSOR_BLINK_SECONDS = 1.0;

static void draw_button(Rectangle r, const char *label, Color fill, Color text)
{
    DrawRectangleRec(r, fill);
    int w = MeasureText(label, BUTTON_FONT);
    DrawText(label, (int)(r.x + (r.width - w) / 2), (int)(r.y + (r.height - BUTTON_FONT) / 2), BUTTON_FONT, text);
}

bool button(Rectangle r, const char *label, bool active, bool enabled)
{
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    Color fill = active ? ACCENT : hover ? Color{85, 88, 96, 255} : BUTTON;
    Color text = !enabled ? Color{110, 110, 115, 255} : active ? BLACK : RAYWHITE;
    draw_button(r, label, fill, text);
    return enabled && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool hold_button(Rectangle r, const char *label, bool &grabbed, bool lit)
{
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        grabbed = false;
    if (grabbed && !lit)
        draw_button(r, label, PENDING, BLACK);
    else if (button(r, label, lit))
        grabbed = true;
    return grabbed;
}

static bool key_typed(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

static std::string only_digits(const std::string &s)
{
    std::string out;
    std::copy_if(s.begin(), s.end(), std::back_inserter(out), [](char c) { return c >= '0' && c <= '9'; });
    return out;
}

void edit_text(TextBuffer &buffer, bool digits_only)
{
    std::string typed;
    for (int codepoint = GetCharPressed(); codepoint != 0; codepoint = GetCharPressed()) {
        int size = 0;
        const char *utf8 = CodepointToUTF8(codepoint, &size);
        typed.append(utf8, size);
    }
    buffer.insert(digits_only ? only_digits(typed) : typed);

    if (key_typed(KEY_BACKSPACE))
        buffer.backspace();
    if (key_typed(KEY_DELETE))
        buffer.erase_forward();
    if (key_typed(KEY_LEFT))
        buffer.left();
    if (key_typed(KEY_RIGHT))
        buffer.right();
    if (IsKeyPressed(KEY_HOME))
        buffer.home();
    if (IsKeyPressed(KEY_END))
        buffer.end();

    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrl && IsKeyPressed(KEY_V)) {
        const char *clipboard = GetClipboardText();
        if (clipboard) {
            std::string pasted(clipboard);
            pasted = pasted.substr(0, pasted.find_first_of("\r\n"));
            buffer.insert(digits_only ? only_digits(pasted) : pasted);
        }
    }
}

void draw_text_field(Rectangle r, const TextBuffer &buffer, bool focused, bool masked)
{
    DrawRectangleRec(r, Color{40, 42, 46, 255});
    DrawRectangleLinesEx(r, focused ? 2 : 1, focused ? ACCENT : Color{70, 72, 78, 255});

    std::string shown = masked ? buffer.masked() : buffer.text();
    size_t cursor = masked ? buffer.cursor_characters() : buffer.cursor();
    int cursor_x = MeasureText(shown.substr(0, cursor).c_str(), FIELD_FONT);
    int inner_width = (int)r.width - 2 * FIELD_PADDING;
    int scroll = std::max(0, cursor_x - inner_width + 2);

    BeginScissorMode((int)r.x + FIELD_PADDING, (int)r.y, inner_width, (int)r.height);
    DrawText(shown.c_str(), (int)r.x + FIELD_PADDING - scroll, (int)(r.y + (r.height - FIELD_FONT) / 2), FIELD_FONT, RAYWHITE);
    if (focused && std::fmod(GetTime(), CURSOR_BLINK_SECONDS) < CURSOR_BLINK_SECONDS / 2)
        DrawRectangle((int)r.x + FIELD_PADDING + cursor_x - scroll, (int)r.y + 5, 2, (int)r.height - 10, RAYWHITE);
    EndScissorMode();
}
