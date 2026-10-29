#pragma once

#include <cstddef>
#include <string>

// A line of UTF-8 text with a cursor, edited the way a text field is: the cursor and the deletions
// always move by whole characters. Control characters and invalid UTF-8 are not accepted.
class TextBuffer {
public:
    explicit TextBuffer(size_t max_bytes = 128) : max_bytes_(max_bytes) {}

    const std::string &text() const { return text_; }
    size_t cursor() const { return cursor_; }     // byte offset, always on a character boundary
    bool empty() const { return text_.empty(); }

    void set(const std::string &utf8)
    {
        text_.clear();
        cursor_ = 0;
        insert(utf8);
    }

    void insert(const std::string &utf8)
    {
        size_t i = 0;
        while (i < utf8.size()) {
            size_t len = sequence_length(utf8, i);
            if (len == 0) {
                i++;
                continue;
            }
            std::string unit = utf8.substr(i, len);
            i += len;
            if (len == 1 && (static_cast<unsigned char>(unit[0]) < 0x20 || unit[0] == 0x7f))
                continue;
            if (text_.size() + unit.size() > max_bytes_)
                break;
            text_.insert(cursor_, unit);
            cursor_ += unit.size();
        }
    }

    void backspace()
    {
        if (cursor_ == 0)
            return;
        size_t start = previous(cursor_);
        text_.erase(start, cursor_ - start);
        cursor_ = start;
    }

    void erase_forward()
    {
        if (cursor_ >= text_.size())
            return;
        text_.erase(cursor_, next(cursor_) - cursor_);
    }

    void left() { cursor_ = previous(cursor_); }
    void right() { cursor_ = next(cursor_); }
    void home() { cursor_ = 0; }
    void end() { cursor_ = text_.size(); }

    size_t characters() const { return count(text_.size()); }
    size_t cursor_characters() const { return count(cursor_); }

    // One mask character per character of the text, for a password field.
    std::string masked(char mask = '*') const { return std::string(characters(), mask); }

private:
    static bool is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

    // Length of the valid UTF-8 sequence at s[i], or 0 if there is none.
    static size_t sequence_length(const std::string &s, size_t i)
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size())
            return 0;
        for (size_t k = 1; k < len; k++)
            if (!is_continuation(static_cast<unsigned char>(s[i + k])))
                return 0;
        // Reject overlong forms and values above U+10FFFF or in the surrogate range.
        if (len == 2 && c < 0xC2)
            return 0;
        if (len == 3) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0))
                return 0;
        }
        if (len == 4) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if (c > 0xF4 || (c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 >= 0x90))
                return 0;
        }
        return len;
    }

    size_t previous(size_t pos) const
    {
        if (pos == 0)
            return 0;
        do {
            pos--;
        } while (pos > 0 && is_continuation(static_cast<unsigned char>(text_[pos])));
        return pos;
    }

    size_t next(size_t pos) const
    {
        if (pos >= text_.size())
            return text_.size();
        do {
            pos++;
        } while (pos < text_.size() && is_continuation(static_cast<unsigned char>(text_[pos])));
        return pos;
    }

    size_t count(size_t upto) const
    {
        size_t n = 0;
        for (size_t i = 0; i < upto; i++)
            n += !is_continuation(static_cast<unsigned char>(text_[i]));
        return n;
    }

    std::string text_;
    size_t cursor_ = 0;
    size_t max_bytes_;
};
