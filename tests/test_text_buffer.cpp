#include <cstdio>

#include "client/text_buffer.h"

static int g_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static void test_ascii_editing()
{
    TextBuffer b;
    b.insert("admin");
    CHECK(b.text() == "admin" && b.cursor() == 5);
    b.left();
    b.left();
    b.insert("X");
    CHECK(b.text() == "admXin");
    b.backspace();
    CHECK(b.text() == "admin" && b.cursor() == 3);
    b.erase_forward();
    CHECK(b.text() == "adm" "n");
    b.home();
    b.backspace();
    CHECK(b.text() == "admn");                   // nothing before the cursor
    b.end();
    b.erase_forward();
    CHECK(b.text() == "admn");                   // nothing after the cursor
}

static void test_cursor_stays_in_bounds()
{
    TextBuffer b;
    b.left();
    b.right();
    CHECK(b.cursor() == 0);
    b.set("ab");
    b.right();
    CHECK(b.cursor() == 2);
    b.home();
    b.left();
    CHECK(b.cursor() == 0);
}

static void test_multibyte_characters_move_and_delete_whole()
{
    TextBuffer b;
    b.insert("a\xD1\x97" "b");                   // a, CYRILLIC SMALL LETTER YI (2 bytes), b
    CHECK(b.text().size() == 4 && b.characters() == 3);
    b.left();                                    // before b
    CHECK(b.cursor() == 3);
    b.left();                                    // before the 2-byte letter
    CHECK(b.cursor() == 1);
    b.right();
    CHECK(b.cursor() == 3);
    b.backspace();                               // removes the whole 2-byte letter
    CHECK(b.text() == "ab" && b.cursor() == 1);

    TextBuffer e;
    e.insert("x\xF0\x9F\x98\x80" "y");           // an emoji, 4 bytes
    CHECK(e.characters() == 3 && e.text().size() == 6);
    e.left();
    e.backspace();
    CHECK(e.text() == "xy");
    e.home();
    e.right();
    e.erase_forward();
    CHECK(e.text() == "x");
}

static void test_rejects_control_and_invalid_input()
{
    TextBuffer b;
    b.insert(std::string("a\tb\nc\x7f" "d\x01" "e", 10));
    CHECK(b.text() == "abcde");
    TextBuffer c;
    c.insert("ok\xC3");                          // truncated sequence
    CHECK(c.text() == "ok");
    c.insert("\xC0\xAF");                        // overlong encoding of '/'
    CHECK(c.text() == "ok");
    c.insert("\xED\xA0\x80");                    // a surrogate
    CHECK(c.text() == "ok");
    c.insert("\xF4\x90\x80\x80");                // above U+10FFFF
    CHECK(c.text() == "ok");
    c.insert("\x80");                            // a lone continuation byte
    CHECK(c.text() == "ok");
}

static void test_limit_never_splits_a_character()
{
    TextBuffer b(5);
    b.insert("abcd");
    b.insert("\xD1\x97");                        // would need bytes 5 and 6
    CHECK(b.text() == "abcd");
    b.insert("e");
    CHECK(b.text() == "abcde");
    b.insert("f");
    CHECK(b.text() == "abcde");

    TextBuffer c(4);
    c.set("\xD1\x97\xD1\x97\xD1\x97");           // three 2-byte letters, room for two
    CHECK(c.characters() == 2 && c.text().size() == 4);
}

static void test_mask_has_one_mark_per_character()
{
    TextBuffer b;
    b.insert("p\xD1\x97ss");
    CHECK(b.masked() == "****");
    CHECK(b.masked('#') == "####");
    CHECK(b.cursor_characters() == 4);
    b.left();
    CHECK(b.cursor_characters() == 3);
}

int main()
{
    test_ascii_editing();
    test_cursor_stays_in_bounds();
    test_multibyte_characters_move_and_delete_whole();
    test_rejects_control_and_invalid_input();
    test_limit_never_splits_a_character();
    test_mask_has_one_mark_per_character();
    if (g_failures) {
        fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    printf("all text buffer tests passed\n");
    return 0;
}
