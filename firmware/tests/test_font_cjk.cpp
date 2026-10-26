// The 12 px font: UTF-8 decoding, column arithmetic and CJK wrapping. Every
// count in the UI is in ASCII columns, so a CJK glyph must cost exactly two and
// a broken byte must never swallow the rest of the string.
#include <string>
#include <vector>

#include "check.hpp"
#include "hg/canvas.hpp"
#include "hg/font.hpp"

using namespace hg;

TEST("font: a CJK glyph takes two columns, ASCII takes one") {
  CHECK_EQ(font::columns(""), 0);
  CHECK_EQ(font::columns("abc"), 3);
  CHECK_EQ(font::columns("中"), 2);
  CHECK_EQ(font::columns("中a"), 3);
  CHECK_EQ(font::columns("提醒: 完成"), 10);  // 2+2+1+1+2+2
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

TEST("font: the table carries GB2312 hanzi and questions the rest") {
  int w = 0, h = 0, row_bytes = 0;
  const uint8_t* rows = nullptr;
  CHECK(font::bitmap(0x4E2D, &w, &h, &row_bytes, &rows));  // 中
  CHECK_EQ(w, 12);
  CHECK_EQ(h, 12);
  CHECK_EQ(row_bytes, 2);
  CHECK(font::bitmap(0x597D, &w, &h, &row_bytes, &rows));  // 好
  CHECK(!font::bitmap(0x1F600, &w, &h, &row_bytes, &rows)); // an emoji: no glyph
  CHECK(font::bitmap('A', &w, &h, &row_bytes, &rows));
  CHECK_EQ(w, 6);
  CHECK_EQ(row_bytes, 1);
}

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
  CHECK_EQ(c.text_width("ab", 1), 6 * 2 - 1);
}

TEST("canvas: wrapping counts CJK as two columns and never splits a glyph") {
  const std::vector<std::string> lines = wrap_text("中文测试中文测试", 6);
  CHECK_EQ(lines.size(), size_t(3));  // 6 columns hold 3 hanzi, so 3 + 3 + 2
  CHECK_EQ(font::columns(lines[0]), 6);
  CHECK_EQ(font::columns(lines[1]), 6);
  CHECK_EQ(font::columns(lines[2]), 4);
  for (const std::string& line : lines) CHECK_EQ(line.size() % 3, size_t(0));

  // A latin word still breaks at its space when the line is long enough.
  const std::vector<std::string> words = wrap_text("alpha beta gamma", 11);
  CHECK_EQ(words.size(), size_t(2));
  CHECK_EQ(words[0], std::string("alpha beta"));

  // Mixed text: "ab中文" is 2 + 4 = 6 columns, exactly one line.
  CHECK_EQ(wrap_text("ab中文", 6).size(), size_t(1));
}
