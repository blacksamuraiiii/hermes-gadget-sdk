#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hg::font {

// The screen draws 12-pixel-tall cells. ASCII advances 6 pixels, which is the
// layout's unit: one "column". A CJK glyph is 12x12 and takes two columns, so
// every count in the UI stays in ASCII cells and a wide glyph simply costs 2.
constexpr int kGlyphWidth = 6;
constexpr int kGlyphHeight = 12;
constexpr int kCellWidth = 6;       // one column
constexpr int kWideCellWidth = 12;  // two columns
constexpr int kCellHeight = 14;     // glyph plus two pixels of spacing

// Row-major glyph pixels: kGlyphWidth * kGlyphHeight chars, '#' = on.
// Non-printable / non-ASCII bytes map to '?'.
const char* glyph(char c);

// A 12x12 glyph as 12 rows of two bytes, bit 11 leftmost, or nullptr when the
// font carries no glyph for that codepoint.
const uint8_t* wide_bitmap(uint32_t cp);

// Decode one UTF-8 sequence at s[i] and advance i past it. A malformed byte
// yields 0 and advances one byte, so callers can draw '?' and carry on.
inline uint32_t decode(std::string_view s, size_t& i) {
  if (i >= s.size()) return 0;
  const unsigned char c = static_cast<unsigned char>(s[i]);
  if (c < 0x80) {
    ++i;
    return c;
  }
  const size_t len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
  if (len == 0 || i + len > s.size()) {
    ++i;
    return 0;
  }
  uint32_t cp = len == 4 ? c & 0x07u : len == 3 ? c & 0x0Fu : c & 0x1Fu;
  for (size_t k = 1; k < len; ++k) {
    const unsigned char cc = static_cast<unsigned char>(s[i + k]);
    if ((cc & 0xC0) != 0x80) {
      ++i;
      return 0;
    }
    cp = (cp << 6) | (cc & 0x3Fu);
  }
  i += len;
  return cp;
}

// Columns a UTF-8 string occupies: ASCII 1, everything else 2.
int columns(std::string_view s);

}  // namespace hg::font
