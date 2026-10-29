#include <cstdio>

#include "client/double_click.h"

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static void test_two_quick_presses_on_one_target()
{
    DoubleClick d(0.4);
    CHECK(!d.press(3, 10.0));
    CHECK(d.press(3, 10.2));
}

static void test_slow_second_press_starts_a_new_pair()
{
    DoubleClick d(0.4);
    CHECK(!d.press(3, 10.0));
    CHECK(!d.press(3, 10.5));
    CHECK(d.press(3, 10.6));
}

static void test_other_target_does_not_pair()
{
    DoubleClick d(0.4);
    CHECK(!d.press(1, 10.0));
    CHECK(!d.press(2, 10.1));
    CHECK(d.press(2, 10.2));
}

static void test_third_press_does_not_chain()
{
    DoubleClick d(0.4);
    CHECK(!d.press(0, 1.0));
    CHECK(d.press(0, 1.1));
    CHECK(!d.press(0, 1.2));
    CHECK(d.press(0, 1.3));
}

static void test_the_first_press_never_pairs()
{
    DoubleClick d(0.4);
    CHECK(!d.press(0, 0.0));        // a fresh object must not pair with a press at time zero
}

int main()
{
    test_two_quick_presses_on_one_target();
    test_slow_second_press_starts_a_new_pair();
    test_other_target_does_not_pair();
    test_third_press_does_not_chain();
    test_the_first_press_never_pairs();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all double click tests passed\n");
    return 0;
}
