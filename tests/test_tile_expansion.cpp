#include <cstdio>

#include "client/tile_expansion.h"

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

struct Grid {
    int layout = 9;
    int selected = 3;
    int camera[TileExpansion::SLOTS];
    TileExpansion expansion;

    Grid()
    {
        for (int i = 0; i < TileExpansion::SLOTS; i++)
            camera[i] = i;
    }
    bool toggle(int tile) { return expansion.toggle(layout, selected, camera, tile); }
};

static void test_expand_then_restore()
{
    Grid g;
    CHECK(g.toggle(5));
    CHECK(g.layout == 1 && g.selected == 0 && g.camera[0] == 5);
    CHECK(g.expansion.active);
    CHECK(g.camera[1] == 1 && g.camera[5] == 5);      // the other slots are untouched

    CHECK(g.toggle(0));
    CHECK(g.layout == 9 && g.selected == 3 && !g.expansion.active);
    for (int i = 0; i < TileExpansion::SLOTS; i++)
        CHECK(g.camera[i] == i);
}

static void test_single_tile_layout_does_not_expand()
{
    Grid g;
    g.layout = 1;
    CHECK(!g.toggle(0));
    CHECK(g.layout == 1 && g.camera[0] == 0 && !g.expansion.active);
}

static void test_expanding_the_first_tile_restores_too()
{
    Grid g;
    g.layout = 4;
    CHECK(g.toggle(0));
    CHECK(g.layout == 1 && g.camera[0] == 0);
    CHECK(g.toggle(0));
    CHECK(g.layout == 4 && g.selected == 3);
}

static void test_choosing_a_layout_puts_the_cameras_back()
{
    Grid g;
    g.toggle(7);
    g.expansion.restore_cameras(g.selected, g.camera);
    CHECK(!g.expansion.active && g.selected == 3 && g.camera[0] == 0 && g.camera[7] == 7);
    g.layout = 4;
    CHECK(g.toggle(0) && g.layout == 1);              // a fresh toggle expands again from the new layout
    CHECK(g.expansion.active);
    g.toggle(0);
    CHECK(g.layout == 4);

    Grid h;
    h.expansion.restore_cameras(h.selected, h.camera);
    CHECK(h.selected == 3 && h.camera[0] == 0);       // nothing to restore, nothing changes
}

static void test_forget_keeps_the_expanded_grid()
{
    Grid g;
    g.toggle(2);
    g.camera[0] = 11;                                 // the user picked another camera for the single tile
    g.expansion.forget();
    CHECK(!g.expansion.active && g.layout == 1 && g.camera[0] == 11);
    CHECK(!g.toggle(0));                              // layout 1 and nothing remembered
    CHECK(g.layout == 1 && g.camera[0] == 11);
}

int main()
{
    test_expand_then_restore();
    test_single_tile_layout_does_not_expand();
    test_expanding_the_first_tile_restores_too();
    test_choosing_a_layout_puts_the_cameras_back();
    test_forget_keeps_the_expanded_grid();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all tile expansion tests passed\n");
    return 0;
}
