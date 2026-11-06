#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

// One interface for whatever font a build carries: either the upstream 5x7 table
// with Latin-1, or the 12 px table with CJK glyphs that a board reporting
// `charset=utf-8` needs (see HG_FONT_CJK). Both answer the same questions — metrics,
// the bitmap for a code point, and how many columns a string takes. Every count in
// the UI stays in ASCII cells ("columns"), so a glyph twice as wide costs two.
namespace hg::font {

struct Metrics {
  int glyph_w;  // ink width of a narrow glyph (never wider than cell_w)
  int glyph_h;  // ink height
  int cell_w;   // one column
  int cell_h;   // row pitch: ink plus spacing
  int wide_w;   // two columns when the font has wide glyphs, else cell_w
  bool wide;    // non-ASCII glyphs are drawable
};

#if HG_FONT_CJK
// The CJK build comes in two faces. The 16 px one (movecall-moji) rasterizes in a
// 22 px line box: CJK stays 16 px ink (rows 2..17), Latin is a 22 px Montserrat
// whose cap height (15 px) matches the hanzi (see tools/make_cjk_font.py --pitch).
// HG_FONT_PX=12 keeps the interface for a 12 px face, which no table ships beyond
// the 12 px pair. `HG_FONT_PX=16` is what a build carrying font_ascii_16 sets.
#ifndef HG_FONT_PX
#define HG_FONT_PX 12
#endif
#if HG_FONT_PX == 16
constexpr Metrics kMetrics{8, 22, 8, 22, 16, true};
#else
constexpr Metrics kMetrics{6, 12, 6, 14, 12, true};
#endif
#else
// The upstream 5x7 font: printable ASCII and Latin-1, one column per code point.
constexpr Metrics kMetrics{5, 7, 6, 9, 6, false};
#endif

// Kept as names because the call sites read better with them; they are the metrics.
inline constexpr int kGlyphWidth = kMetrics.glyph_w;
inline constexpr int kGlyphHeight = kMetrics.glyph_h;
inline constexpr int kCellWidth = kMetrics.cell_w;
inline constexpr int kWideCellWidth = kMetrics.wide_w;
inline constexpr int kCellHeight = kMetrics.cell_h;

// One glyph as 1-bit rows, most significant bit leftmost, `row_bytes` per row.
// False means the font has no glyph for `cp`, and the caller draws '?' instead.
bool bitmap(uint32_t cp, int* w, int* h, int* row_bytes, const uint8_t** rows);

// How far the pen moves after drawing `cp`, in pixels at scale 1. A proportional
// Latin face carries its own advance per glyph; CJK takes a whole cell; anything the
// font cannot draw takes the cell width (or two when the face has wide glyphs).
int advance(uint32_t cp);

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

// Columns a UTF-8 string occupies: ASCII 1, a wide glyph 2 (1 when the font has none).
int columns(std::string_view s);

// Bytes taken by the first `cols` columns of `s` (all of it when shorter), so a cut
// never lands inside a UTF-8 sequence.
size_t prefix(std::string_view s, int cols);

// True when this font has its own glyph for `cp`; 0 (a malformed byte) has none.
inline bool drawable(uint32_t cp) {
  int w = 0, h = 0, row_bytes = 0;
  const uint8_t* rows = nullptr;
  return cp != 0 && bitmap(cp, &w, &h, &row_bytes, &rows);
}

}  // namespace hg::font
