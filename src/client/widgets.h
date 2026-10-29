#pragma once

#include <raylib.h>

#include "text_buffer.h"

// Returns true on the frame the button is clicked.
bool button(Rectangle r, const char *label, bool active = false, bool enabled = true);

// Applies this frame's typing, cursor keys and Ctrl+V to `buffer`; call it for the focused field only.
void edit_text(TextBuffer &buffer, bool digits_only = false);

void draw_text_field(Rectangle r, const TextBuffer &buffer, bool focused, bool masked = false);
