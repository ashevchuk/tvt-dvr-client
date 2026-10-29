#include "ui.h"

#include "audio.h"

static const int TAB_HEIGHT = 34;
static const int LIST_WIDTH = 180;
static const int LIST_ROW_HEIGHT = 26;
static const int LIST_TOP = TAB_HEIGHT + 40;
static const int BAR_HEIGHT = 44;
static const int PLAYBACK_PANEL_HEIGHT = 178;
static const int TIMELINE_ROW_HEIGHT = 24;
static const int TIMELINE_LABEL_WIDTH = 60;
static const int LIVE_LAYOUTS[] = {1, 4, 9, 16};
static const int PLAYBACK_LAYOUTS[] = {1, 4};
// Sub streams (352x288) are the main stream's 928x576 field of view squeezed horizontally.
static const float DISPLAY_ASPECT = 928.0f / 576.0f;
static const double MIN_TIMELINE_SPAN = 120;
static const Color BG = {18, 18, 20, 255};
static const Color PANEL = {30, 32, 36, 255};
static const Color BUTTON = {60, 62, 68, 255};
static const Color ROW_HIGHLIGHT = {55, 60, 70, 255};
static const Color ACCENT = {40, 220, 60, 255};
static const Color RECORDED = {40, 150, 60, 255};
static const Color SELECTION = {255, 150, 30, 110};
static const Color CURSOR = {255, 255, 255, 255};

static Rectangle video_area(const App &app)
{
    float bottom = app.tab == Tab::Live ? BAR_HEIGHT : PLAYBACK_PANEL_HEIGHT;
    return {(float)LIST_WIDTH, (float)TAB_HEIGHT, (float)(GetScreenWidth() - LIST_WIDTH),
            GetScreenHeight() - TAB_HEIGHT - bottom};
}

static Rectangle tile_rect(const App &app, const Grid &grid, int index)
{
    Rectangle area = video_area(app);
    int cols = grid.layout == 1 ? 1 : grid.layout == 4 ? 2 : grid.layout == 9 ? 3 : 4;
    float w = area.width / cols;
    float h = area.height / cols;
    return {area.x + (index % cols) * w, area.y + (index / cols) * h, w, h};
}

static bool button(Rectangle r, const char *label, bool active = false, bool enabled = true)
{
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    Color fill = active ? ACCENT : hover ? Color{85, 88, 96, 255} : BUTTON;
    DrawRectangleRec(r, fill);
    Color text = !enabled ? Color{110, 110, 115, 255} : active ? BLACK : RAYWHITE;
    int w = MeasureText(label, 16);
    DrawText(label, (int)(r.x + (r.width - w) / 2), (int)(r.y + (r.height - 16) / 2), 16, text);
    return enabled && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static void draw_tile(const App &app, const Grid &grid, int index, const SegmentCache *segments)
{
    Rectangle r = tile_rect(app, grid, index);
    DrawRectangleRec(r, BLACK);
    const Stream *s = grid.streams[index].get();
    int camera = grid.camera[index] + 1;
    if (s && s->texture().id) {
        float w = std::min(r.width, r.height * DISPLAY_ASPECT);
        float h = w / DISPLAY_ASPECT;
        Rectangle dst = {r.x + (r.width - w) / 2, r.y + (r.height - h) / 2, w, h};
        DrawTexturePro(s->texture(), {0, 0, (float)s->width(), (float)s->height()}, dst, {0, 0}, 0, WHITE);
        if (s->playback)
            DrawText(TextFormat("CH%02d %s", camera, format_clock((time_t)s->displayed_time()).c_str()), (int)r.x + 6, (int)r.y + 4, 14, ACCENT);
        else
            DrawText(TextFormat("CH%02d %dx%d", camera, s->width(), s->height()), (int)r.x + 6, (int)r.y + 4, 14, ACCENT);
    } else {
        const char *state = "no stream";
        if (segments) {
            state = !segments->get(grid.camera[index]) ? "loading" : s ? "waiting" : "no recording";
        } else if (s && s->connected()) {
            state = "waiting";
        }
        DrawText(TextFormat("CH%02d %s", camera, state), (int)r.x + 6, (int)r.y + 4, 14, GRAY);
    }
    DrawRectangleLinesEx(r, index == grid.selected ? 2 : 1, index == grid.selected ? ACCENT : Color{50, 50, 55, 255});
}

static void draw_camera_list(const App &app, Grid &grid, bool &changed)
{
    DrawRectangle(0, TAB_HEIGHT, LIST_WIDTH, GetScreenHeight() - TAB_HEIGHT, PANEL);
    DrawText("Cameras", 12, TAB_HEIGHT + 10, 18, RAYWHITE);
    for (int c = 0; c < app.cameras; c++) {
        int y = LIST_TOP + c * LIST_ROW_HEIGHT;
        Rectangle row = {0, (float)y, (float)LIST_WIDTH, (float)LIST_ROW_HEIGHT};
        bool current = c == grid.camera[grid.selected];
        if (current)
            DrawRectangleRec(row, ROW_HIGHLIGHT);
        DrawText(TextFormat("Camera %02d%s", c + 1, c < g_audio_inputs ? "  [a]" : ""), 14, y + 5, 16, current ? ACCENT : LIGHTGRAY);
        if (CheckCollisionPointRec(GetMousePosition(), row) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            grid.camera[grid.selected] = c;
            changed = true;
        }
    }
}

static void draw_tabs(App &app)
{
    DrawRectangle(0, 0, GetScreenWidth(), TAB_HEIGHT, Color{24, 25, 28, 255});
    if (button({8, 4, 90, 26}, "Live", app.tab == Tab::Live) && app.tab != Tab::Live)
        enter_live(app);
    if (button({104, 4, 90, 26}, "Playback", app.tab == Tab::Playback) && app.tab != Tab::Playback)
        enter_playback(app);
}

static void handle_tile_click(const App &app, Grid &grid)
{
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        return;
    for (int i = 0; i < grid.layout; i++) {
        if (CheckCollisionPointRec(GetMousePosition(), tile_rect(app, grid, i))) {
            grid.selected = i;
            select_audio(grid);
            return;
        }
    }
}

static void draw_live_bar(App &app)
{
    Grid &grid = app.live;
    int y = GetScreenHeight() - BAR_HEIGHT;
    DrawRectangle(LIST_WIDTH, y, GetScreenWidth() - LIST_WIDTH, BAR_HEIGHT, PANEL);
    for (int i = 0; i < 4; i++) {
        Rectangle r = {LIST_WIDTH + 10.0f + i * 50.0f, y + 7.0f, 40.0f, 30.0f};
        if (button(r, TextFormat("%d", LIVE_LAYOUTS[i]), LIVE_LAYOUTS[i] == grid.layout) && LIVE_LAYOUTS[i] != grid.layout) {
            grid.layout = LIVE_LAYOUTS[i];
            grid.selected = std::min(grid.selected, grid.layout - 1);
            rebuild_live_streams(grid);
        }
    }
}

static void draw_calendar(Playback &pb, int top)
{
    const int cell_w = 24, cell_h = 22, left = 8;
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (button({(float)left, (float)top, 24, 22}, "<")) {
        if (--pb.calendar_month < 1) {
            pb.calendar_month = 12;
            pb.calendar_year--;
        }
    }
    if (button({(float)left + 7 * cell_w - 24, (float)top, 24, 22}, ">")) {
        if (++pb.calendar_month > 12) {
            pb.calendar_month = 1;
            pb.calendar_year++;
        }
    }
    const char *title = TextFormat("%s %d", months[pb.calendar_month - 1], pb.calendar_year);
    DrawText(title, left + (7 * cell_w - MeasureText(title, 16)) / 2, top + 3, 16, RAYWHITE);

    static const char *weekdays[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    for (int i = 0; i < 7; i++)
        DrawText(weekdays[i], left + i * cell_w + 3, top + 28, 12, GRAY);

    time_t first = day_start_of(pb.calendar_year, pb.calendar_month, 1);
    tm first_tm;
    localtime_r(&first, &first_tm);
    int offset = (first_tm.tm_wday + 6) % 7;
    for (int day = 1; day <= 31; day++) {
        time_t t = day_start_of(pb.calendar_year, pb.calendar_month, day);
        tm t_tm;
        localtime_r(&t, &t_tm);
        if (t_tm.tm_mon + 1 != pb.calendar_month)
            break;
        int slot = offset + day - 1;
        Rectangle r = {(float)(left + (slot % 7) * cell_w), (float)(top + 44 + (slot / 7) * cell_h), (float)cell_w - 2, (float)cell_h - 2};
        bool recorded = pb.recorded_days.count(day_key(pb.calendar_year, pb.calendar_month, day)) > 0;
        bool current = t == pb.day_start;
        DrawRectangleRec(r, recorded ? RECORDED : Color{45, 47, 52, 255});
        if (current)
            DrawRectangleLinesEx(r, 2, WHITE);
        const char *label = TextFormat("%d", day);
        DrawText(label, (int)(r.x + (r.width - MeasureText(label, 12)) / 2), (int)r.y + 5, 12, recorded ? WHITE : GRAY);
        if (recorded && !current && CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            select_day(pb, pb.calendar_year, pb.calendar_month, day);
    }
}

static double timeline_time(const Playback &pb, float x, float x0, float x1)
{
    return pb.day_start + pb.view_start + (x - x0) / (x1 - x0) * pb.view_span;
}

static float timeline_x(const Playback &pb, double t, float x0, float x1)
{
    return x0 + (float)((t - pb.day_start - pb.view_start) / pb.view_span * (x1 - x0));
}

static void clamp_view(Playback &pb)
{
    pb.view_span = std::clamp(pb.view_span, MIN_TIMELINE_SPAN, (double)SECONDS_PER_DAY);
    pb.view_start = std::clamp(pb.view_start, 0.0, SECONDS_PER_DAY - pb.view_span);
}

static void draw_timeline(Playback &pb, float y0)
{
    Grid &grid = pb.grid;
    const float x0 = LIST_WIDTH + TIMELINE_LABEL_WIDTH;
    const float x1 = (float)GetScreenWidth() - 16;
    const float rows_h = (float)(MAX_PLAYBACK_TILES * TIMELINE_ROW_HEIGHT);
    Rectangle area = {x0, y0, x1 - x0, rows_h};
    Vector2 mouse = GetMousePosition();
    bool over = CheckCollisionPointRec(mouse, area);

    for (int i = 0; i < grid.layout; i++) {
        float y = y0 + i * TIMELINE_ROW_HEIGHT;
        DrawRectangle((int)x0, (int)y, (int)(x1 - x0), TIMELINE_ROW_HEIGHT - 2, Color{40, 42, 46, 255});
        DrawText(TextFormat("CH%02d", grid.camera[i] + 1), LIST_WIDTH + 8, (int)y + 5, 14,
                 i == grid.selected ? ACCENT : LIGHTGRAY);
        SegmentList list = pb.segments.get(grid.camera[i]);
        if (!list)
            continue;
        for (const Segment &s : *list) {
            float a = std::max(x0, timeline_x(pb, s.start, x0, x1));
            float b = std::min(x1, timeline_x(pb, s.stop, x0, x1));
            if (b > a)
                DrawRectangle((int)a, (int)y + 2, std::max(1, (int)(b - a)), TIMELINE_ROW_HEIGHT - 6, RECORDED);
        }
    }

    static const int steps[] = {60, 300, 900, 1800, 3600, 7200, 10800, 21600};
    int step = steps[7];
    for (int s : steps) {
        if (s / pb.view_span * (x1 - x0) >= 70) {
            step = s;
            break;
        }
    }
    double t0 = std::ceil((pb.day_start + pb.view_start) / step) * step;
    for (double t = t0; t < pb.day_start + pb.view_start + pb.view_span; t += step) {
        float x = timeline_x(pb, t, x0, x1);
        DrawLine((int)x, (int)(y0 + rows_h), (int)x, (int)(y0 + rows_h + 4), GRAY);
        std::string label = format_clock((time_t)t).substr(0, 5);
        DrawText(label.c_str(), (int)x - 16, (int)(y0 + rows_h + 6), 12, GRAY);
    }

    if (pb.selection_a && pb.selection_b > pb.selection_a) {
        float a = std::clamp(timeline_x(pb, pb.selection_a, x0, x1), x0, x1);
        float b = std::clamp(timeline_x(pb, pb.selection_b, x0, x1), x0, x1);
        DrawRectangle((int)a, (int)y0, std::max(1, (int)(b - a)), (int)rows_h, SELECTION);
    }
    float cx = timeline_x(pb, pb.position, x0, x1);
    if (cx >= x0 && cx <= x1)
        DrawRectangle((int)cx - 1, (int)y0, 2, (int)rows_h, CURSOR);

    if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        pb.position = std::clamp(timeline_time(pb, mouse.x, x0, x1), (double)pb.day_start, pb.day_start + SECONDS_PER_DAY - 1.0);
        restart_playback_streams(pb);
    }
    if (over && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        pb.selection_a = pb.selection_b = (time_t)timeline_time(pb, mouse.x, x0, x1);
        pb.dragging_selection = true;
    }
    if (pb.dragging_selection) {
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            pb.selection_b = (time_t)std::clamp(timeline_time(pb, mouse.x, x0, x1), (double)pb.day_start, pb.day_start + SECONDS_PER_DAY - 1.0);
        } else {
            pb.dragging_selection = false;
            if (pb.selection_a > pb.selection_b)
                std::swap(pb.selection_a, pb.selection_b);
            if (pb.selection_b - pb.selection_a < 1)
                pb.selection_a = pb.selection_b = 0;
        }
    }
    float wheel = GetMouseWheelMove();
    if (over && wheel != 0) {
        double anchor = timeline_time(pb, mouse.x, x0, x1) - pb.day_start;
        double fraction = (mouse.x - x0) / (x1 - x0);
        pb.view_span *= wheel > 0 ? 0.75 : 1.0 / 0.75;
        pb.view_span = std::clamp(pb.view_span, MIN_TIMELINE_SPAN, (double)SECONDS_PER_DAY);
        pb.view_start = anchor - fraction * pb.view_span;
        clamp_view(pb);
    }
    if (over && IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        pb.view_start -= GetMouseDelta().x / (x1 - x0) * pb.view_span;
        clamp_view(pb);
    }
}

static void draw_playback_panel(App &app)
{
    Playback &pb = app.playback;
    Grid &grid = pb.grid;
    const float y = (float)(GetScreenHeight() - PLAYBACK_PANEL_HEIGHT);
    const float x = LIST_WIDTH + 10.0f;
    DrawRectangle(LIST_WIDTH, (int)y, GetScreenWidth() - LIST_WIDTH, PLAYBACK_PANEL_HEIGHT, PANEL);

    if (button({x, y + 6, 70, 28}, pb.playing ? "Pause" : "Play"))
        pb.playing = !pb.playing;
    static const int speeds[] = {1, 2, 4};
    for (int i = 0; i < 3; i++)
        if (button({x + 80 + i * 44.0f, y + 6, 40, 28}, TextFormat("%dx", speeds[i]), pb.speed == speeds[i]))
            pb.speed = speeds[i];
    for (int i = 0; i < 2; i++) {
        int layout = PLAYBACK_LAYOUTS[i];
        if (button({x + 224 + i * 44.0f, y + 6, 40, 28}, TextFormat("%d", layout), grid.layout == layout) && grid.layout != layout) {
            grid.layout = layout;
            grid.selected = std::min(grid.selected, layout - 1);
            restart_playback_streams(pb);
        }
    }
    DrawText(TextFormat("%s  %s", format_clock((time_t)pb.position).c_str(), pb.playing ? "" : "paused"), (int)x + 324, (int)y + 12, 18, RAYWHITE);

    bool can_download = pb.selection_a != 0 && pb.selection_b > pb.selection_a && !pb.download.active;
    if (button({(float)GetScreenWidth() - 130, y + 6, 114, 28}, "Download", false, can_download))
        start_download(pb);
    std::string status;
    if (pb.selection_a && pb.selection_b > pb.selection_a) {
        status = format_clock(pb.selection_a) + " - " + format_clock(pb.selection_b) + "  ";
    } else {
        status = "right-drag on the timeline to select a period  ";
    }
    if (pb.download.active)
        status += TextFormat("%s %d%%", pb.download.message.c_str(), pb.download.progress);
    else
        status += pb.download.message;
    DrawText(status.c_str(), (int)x + 324 + 164, (int)y + 14, 14, GRAY);

    draw_timeline(pb, y + 44);
}

static void draw_calendar_panel(Playback &pb)
{
    int top = LIST_TOP + MAX_TILES * LIST_ROW_HEIGHT + 12;
    DrawRectangle(0, top - 6, LIST_WIDTH, GetScreenHeight() - top + 6, Color{36, 38, 42, 255});
    draw_calendar(pb, top);
}

void draw_app(App &app)
{
    Grid &grid = app.tab == Tab::Live ? app.live : app.playback.grid;

    BeginDrawing();
    ClearBackground(BG);
    handle_tile_click(app, grid);
    for (int i = 0; i < grid.layout; i++)
        draw_tile(app, grid, i, app.tab == Tab::Playback ? &app.playback.segments : nullptr);

    bool camera_changed = false;
    draw_camera_list(app, grid, camera_changed);
    if (app.tab == Tab::Live) {
        draw_live_bar(app);
        if (camera_changed)
            rebuild_live_streams(app.live);
    } else {
        draw_playback_panel(app);
        draw_calendar_panel(app.playback);
        if (camera_changed) {
            grid.streams[grid.selected].reset();
            select_audio(grid);
        }
    }
    draw_tabs(app);
    EndDrawing();
}
