// The font interface: UTF-8 decoding, column arithmetic and (with HG_FONT_CJK) CJK
// wrapping. Every count in the UI is in ASCII columns; a build carries either the
// 12 px CJK table or the upstream 5x7 Latin-1 one, and the two answer the same.
#include <string>
#include <vector>

#include "check.hpp"
#include "hg/canvas.hpp"
#include "hg/font.hpp"

using namespace hg;

TEST("font: metrics match the table the build carries") {
#if HG_FONT_CJK
  CHECK_EQ(font::kMetrics.glyph_w, 6);
  CHECK_EQ(font::kMetrics.glyph_h, 12);
  CHECK_EQ(font::kMetrics.cell_h, 14);
  CHECK_EQ(font::kMetrics.wide_w, 12);
  CHECK(font::kMetrics.wide);
#else
  CHECK_EQ(font::kMetrics.glyph_w, 5);
  CHECK_EQ(font::kMetrics.glyph_h, 7);
  CHECK_EQ(font::kMetrics.cell_h, 9);
  CHECK_EQ(font::kMetrics.wide_w, 6);
  CHECK(!font::kMetrics.wide);
#endif
}

TEST("font: column arithmetic follows the metrics") {
  CHECK_EQ(font::columns(""), 0);
  CHECK_EQ(font::columns("abc"), 3);
#if HG_FONT_CJK
  CHECK_EQ(font::columns("中"), 2);
  CHECK_EQ(font::columns("中a"), 3);
  CHECK_EQ(font::columns("提醒: 完成"), 10);  // 2+2+1+1+2+2
#else
  // Latin-1 is one column per code point, and a code point the font lacks still
  // costs one cell: it draws as '?' in the same place.
  CHECK_EQ(font::columns("中"), 1);
  CHECK_EQ(font::columns("é"), 1);
  CHECK_EQ(font::columns("aé"), 2);
#endif
}

TEST("font: decoding skips UTF-8 sequences and survives broken bytes") {
  const std::string s = "a\xE4\xB8\xAD" "b";  // a, U+4E2D, b
  size_t i = 0;
  CHECK_EQ(font::decode(s, i), uint32_t('a'));
  CHECK_EQ(font::decode(s, i), uint32_t(0x4E2D));
  CHECK_EQ(font::decode(s, i), uint32_t('b'));
  CHECK_EQ(i, s.size());

  const std::string bad = "\xFFz";  // a byte no sequence can start with
  size_t j = 0;
  CHECK_EQ(font::decode(bad, j), uint32_t(0));
  CHECK_EQ(j, size_t(1));
  CHECK_EQ(font::decode(bad, j), uint32_t('z'));
}

TEST("font: ASCII rows are left-aligned in their byte") {
  int w = 0, h = 0, row_bytes = 0;
  const uint8_t* rows = nullptr;
  CHECK(font::bitmap('!', &w, &h, &row_bytes, &rows));
  CHECK_EQ(w, font::kMetrics.glyph_w);
  CHECK_EQ(h, font::kMetrics.glyph_h);
  CHECK_EQ(row_bytes, 1);
  // '!' is a bar in the middle column, so every row but the gap row has one bit set.
  int lit_rows = 0;
  for (int r = 0; r < h; ++r) {
    if (rows[r] != 0) ++lit_rows;
  }
  CHECK(lit_rows >= 5);
  uint8_t bits = rows[0];
  CHECK_EQ(static_cast<int>((bits >> 7) & 1), 0);  // nothing at the left edge
}

#if HG_FONT_CJK
TEST("font: the table carries GB2312 hanzi and questions the rest") {
  int w = 0, h = 0, row_bytes = 0;
  const uint8_t* rows = nullptr;
  CHECK(font::bitmap(0x4E2D, &w, &h, &row_bytes, &rows));  // 中
  CHECK_EQ(w, 12);
  CHECK_EQ(h, 12);
  CHECK_EQ(row_bytes, 2);
  CHECK(font::bitmap(0x597D, &w, &h, &row_bytes, &rows));  // 好
  CHECK(!font::bitmap(0x1F600, &w, &h, &row_bytes, &rows)); // an emoji: no glyph
}
#else
TEST("font: the 5x7 table carries Latin-1 and questions CJK") {
  int w = 0, h = 0, row_bytes = 0;
  const uint8_t* rows = nullptr;
  CHECK(!font::bitmap(0x4E2D, &w, &h, &row_bytes, &rows));  // 中: no glyph
  CHECK(font::bitmap(0xE9, &w, &h, &row_bytes, &rows));     // é
  CHECK_EQ(w, 5);
  CHECK_EQ(h, 7);
  CHECK_EQ(row_bytes, 1);
  CHECK(!font::bitmap(0x1F600, &w, &h, &row_bytes, &rows)); // an emoji: no glyph
}
#endif

TEST("font: wide glyphs are left-aligned, so a glyph is never cropped") {
  // 一 is one horizontal stroke; it must start at column 0 of its cell. Right-aligned
  // rows would shift every hanzi four pixels right and cut its right edge.
  int w = 0, h = 0, row_bytes = 0;
  const uint8_t* rows = nullptr;
  CHECK(font::bitmap(0x4E00, &w, &h, &row_bytes, &rows));  // 一
  CHECK_EQ(w, 12);
  CHECK_EQ(row_bytes, 2);
  bool seen = false;
  for (int r = 0; r < h; ++r) {
    const int value = (rows[r * 2] << 8) | rows[r * 2 + 1];
    CHECK_EQ(value & 0x0F, 0);  // the low nibble of the second byte stays empty
    if (value != 0 && !seen) {
      CHECK_EQ((value >> 15) & 1, 1);  // the stroke reaches the leftmost pixel
      seen = true;
    }
  }
  CHECK(seen);
}

TEST("canvas: text advances one cell per narrow glyph") {
  std::vector<uint16_t> fb(240 * 240, 0);
  Canvas c(fb.data(), 240, 240, /*swap_bytes=*/false);
  const int x = c.text(0, 0, "ab", 1, 0xFFFF);
  CHECK_EQ(x, 2 * font::kMetrics.cell_w);
  CHECK_EQ(c.text_width("ab", 1), 2 * font::kMetrics.cell_w - 1);
  int lit = 0;
  for (size_t i = 0; i < fb.size(); ++i) {
    if (fb[i] != 0) ++lit;
  }
  CHECK(lit > 10);
}

#if HG_FONT_CJK
TEST("canvas: a CJK glyph draws inside its 12x12 cell") {
  std::vector<uint16_t> fb(240 * 240, 0);
  Canvas c(fb.data(), 240, 240, /*swap_bytes=*/false);
  const int x = c.text(0, 0, "中", 1, 0xFFFF);
  CHECK_EQ(x, 12);
  int lit = 0;
  for (int y = 0; y < 12; ++y) {
    for (int xx = 0; xx < 12; ++xx) {
      if (fb[static_cast<size_t>(y) * 240 + xx] != 0) ++lit;
    }
  }
  CHECK(lit > 10);  // 中 has a good many lit pixels
  CHECK_EQ(c.text_width("中", 1), 12 - 1);
}

TEST("canvas: wrapping counts CJK as two columns and never splits a glyph") {
  const std::vector<std::string> lines = wrap_text("中文测试中文测试", 6);
  CHECK_EQ(lines.size(), size_t(3));  // 6 columns hold 3 hanzi, so 3 + 3 + 2
  CHECK_EQ(font::columns(lines[0]), 6);
  CHECK_EQ(font::columns(lines[1]), 6);
  CHECK_EQ(font::columns(lines[2]), 4);
  for (const std::string& line : lines) CHECK_EQ(line.size() % 3, size_t(0));

  // Mixed text: "ab中文" is 2 + 4 = 6 columns, exactly one line.
  CHECK_EQ(wrap_text("ab中文", 6).size(), size_t(1));
}
#else
TEST("canvas: a code point the font lacks draws '?' in one cell") {
  std::vector<uint16_t> fb(240 * 240, 0);
  Canvas c(fb.data(), 240, 240, /*swap_bytes=*/false);
  const int x = c.text(0, 0, "中", 1, 0xFFFF);
  CHECK_EQ(x, font::kMetrics.cell_w);
  int lit = 0;
  for (size_t i = 0; i < fb.size(); ++i) {
    if (fb[i] != 0) ++lit;
  }
  CHECK(lit > 5);  // the '?' glyph, not a blank cell
}
#endif

TEST("canvas: wrapping still breaks a latin line at its space") {
  const std::vector<std::string> words = wrap_text("alpha beta gamma", 11);
  CHECK_EQ(words.size(), size_t(2));
  CHECK_EQ(words[0], std::string("alpha beta"));
}
