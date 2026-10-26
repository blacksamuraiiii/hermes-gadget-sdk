#include "hg/canvas.hpp"

#include <algorithm>

#include "hg/font.hpp"

namespace hg {

void Canvas::set_clip_rows(int y0, int y1) {
  clip_y0_ = std::max(0, y0);
  clip_y1_ = std::min(h_, y1);
}

void Canvas::pixel(int x, int y, uint16_t color) {
  if (x < 0 || x >= w_ || y < clip_y0_ || y >= clip_y1_ || y >= h_) return;
  px_[y * stride_ + x] = encode(color);
}

void Canvas::fill_rect(int x, int y, int w, int h, uint16_t color) {
  int x0 = std::max(0, x), x1 = std::min(w_, x + w);
  int y0 = std::max(clip_y0_, y), y1 = std::min(std::min(h_, clip_y1_), y + h);
  if (x0 >= x1 || y0 >= y1) return;
  uint16_t c = encode(color);
  for (int yy = y0; yy < y1; ++yy) {
    std::fill(px_ + yy * stride_ + x0, px_ + yy * stride_ + x1, c);
  }
}

void Canvas::rect(int x, int y, int w, int h, uint16_t color) {
  fill_rect(x, y, w, 1, color);
  fill_rect(x, y + h - 1, w, 1, color);
  fill_rect(x, y, 1, h, color);
  fill_rect(x + w - 1, y, 1, h, color);
}

void Canvas::fill_round_rect(int x, int y, int w, int h, int r, uint16_t color) {
  r = std::min(r, std::min(w, h) / 2);
  if (r <= 0) {
    fill_rect(x, y, w, h, color);
    return;
  }
  fill_rect(x, y + r, w, h - 2 * r, color);
  for (int i = 0; i < r; ++i) {
    // Inset of row i from the top/bottom edge for a quarter circle of radius r.
    int dy = r - i;
    int inset = 0;
    while (inset < r && (r - inset) * (r - inset) + dy * dy > r * r) ++inset;
    fill_rect(x + inset, y + i, w - 2 * inset, 1, color);
    fill_rect(x + inset, y + h - 1 - i, w - 2 * inset, 1, color);
  }
}

void Canvas::fill_circle(int cx, int cy, int r, uint16_t color) {
  for (int dy = -r; dy <= r; ++dy) {
    int dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) ++dx;
    fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
  }
}

void Canvas::ring(int cx, int cy, int r, int thickness, uint16_t color) {
  int inner = std::max(0, r - thickness);
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      int d2 = dx * dx + dy * dy;
      if (d2 <= r * r && d2 > inner * inner) pixel(cx + dx, cy + dy, color);
    }
  }
}

void Canvas::arc(int cx, int cy, int r, int thickness, int dir, uint16_t color) {
  int inner = std::max(0, r - thickness);
  for (int dy = -r; dy <= r; ++dy) {
    int ady = dy < 0 ? -dy : dy;
    for (int dx = -r; dx <= r; ++dx) {
      int d2 = dx * dx + dy * dy;
      if (d2 > r * r || d2 <= inner * inner) continue;
      if (dir * dx * 10 < ady * 7) continue;  // outside the +-55 degree cone
      pixel(cx + dx, cy + dy, color);
    }
  }
}

void Canvas::bitmap1(int x, int y, int w, int h, const uint8_t* bits, uint16_t color) {
  const int stride = (w + 7) / 8;
  const uint16_t c = encode(color);
  int y0 = std::max(y, clip_y0_), y1 = std::min(std::min(h_, clip_y1_), y + h);
  for (int yy = y0; yy < y1; ++yy) {
    const uint8_t* row = bits + (yy - y) * stride;
    for (int col = 0; col < w; ++col) {
      int xx = x + col;
      if (xx < 0 || xx >= w_) continue;
      if ((row[col >> 3] >> (7 - (col & 7))) & 1) px_[yy * stride_ + xx] = c;
    }
  }
}

namespace {

// Draws one glyph as a block of 1-bit rows, MSB leftmost in each byte.
void draw_bitmap(Canvas& c, int x, int y, int scale, uint16_t color, int w, int h, int row_bytes,
                 const uint8_t* rows) {
  for (int row = 0; row < h; ++row) {
    const uint8_t* bits = rows + row * row_bytes;
    for (int col = 0; col < w; ++col) {
      if ((bits[col >> 3] >> (7 - (col & 7))) & 1) {
        c.fill_rect(x + col * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

}  // namespace

int Canvas::text(int x, int y, std::string_view s, int scale, uint16_t color) {
  if (scale < 1) scale = 1;
  const font::Metrics& m = font::kMetrics;
  for (size_t i = 0; i < s.size();) {
    const uint32_t cp = font::decode(s, i);
    int w = 0, h = 0, row_bytes = 0;
    const uint8_t* rows = nullptr;
    const bool have = cp != 0 && font::bitmap(cp, &w, &h, &row_bytes, &rows);
    // A wide glyph takes two columns; a code point the font lacks takes a wide cell and
    // shows '?' instead, so a missing glyph never shifts the rest of the line.
    const int advance = (have ? w > m.cell_w : m.wide) ? m.wide_w : m.cell_w;
    if (!have) {
      if (font::bitmap('?', &w, &h, &row_bytes, &rows)) {
        draw_bitmap(*this, x + (advance - w) * scale / 2, y, scale, color, w, h, row_bytes, rows);
      }
      x += advance * scale;
      continue;
    }
    draw_bitmap(*this, x + (advance - w) * scale / 2, y, scale, color, w, h, row_bytes, rows);
    x += advance * scale;
  }
  return x;
}

int Canvas::text_width(std::string_view s, int scale) {
  if (s.empty()) return 0;
  return font::columns(s) * font::kCellWidth * scale - scale;
}

int Canvas::line_height(int scale) { return font::kCellHeight * scale; }

void Canvas::blit(int x, int y, int w, int h, const uint16_t* src) {
  for (int row = 0; row < h; ++row) {
    int yy = y + row;
    if (yy < clip_y0_ || yy >= clip_y1_ || yy >= h_ || yy < 0) continue;
    for (int col = 0; col < w; ++col) {
      int xx = x + col;
      if (xx < 0 || xx >= w_) continue;
      px_[yy * stride_ + xx] = encode(src[row * w + col]);
    }
  }
}

namespace {

// One codepoint of a paragraph, with the columns it occupies.
struct Cell {
  size_t off;
  size_t len;
  uint32_t cp;
  int cols;
};

std::vector<Cell> cells_of(std::string_view s) {
  std::vector<Cell> cells;
  size_t i = 0;
  while (i < s.size()) {
    const size_t start = i;
    const uint32_t cp = font::decode(s, i);
    if (i == start) ++i;  // never loop on a byte the decoder refuses to consume
    const int cols = (cp != 0 && (cp < 0x80 || !font::kMetrics.wide)) ? 1 : 2;
    cells.push_back({start, i - start, cp, cols});
  }
  return cells;
}

int width_of(const std::vector<Cell>& cells, size_t from, size_t to) {
  int width = 0;
  for (size_t k = from; k < to && k < cells.size(); ++k) {
    if (cells[k].cp == 0) continue;  // a broken byte occupies a cell but no room
    width += cells[k].cols;
  }
  return width;
}

}  // namespace

std::vector<std::string> wrap_text(std::string_view text, int max_chars) {
  std::vector<std::string> lines;
  if (max_chars < 1) max_chars = 1;
  size_t i = 0;
  while (i <= text.size()) {
    const size_t nl = text.find('\n', i);
    const std::string_view para =
        text.substr(i, nl == std::string_view::npos ? std::string_view::npos : nl - i);
    const std::vector<Cell> cells = cells_of(para);
    size_t start = 0;
    int width = 0;
    size_t space = std::string_view::npos;  // last break-friendly space in this line
    for (size_t c = 0; c < cells.size(); ++c) {
      const Cell& cell = cells[c];
      const bool blank = cell.cp == ' ';
      if (blank) {
        if (width > 0) space = c;
        continue;  // a space never starts a line
      }
      if (width + cell.cols > max_chars && width > 0) {
        const bool at_space = space != std::string_view::npos && space > start;
        const size_t cut = at_space ? space : c;
        std::string line(para.substr(cells[start].off, cells[cut].off - cells[start].off));
        while (!line.empty() && line.back() == ' ') line.pop_back();
        lines.push_back(line);
        start = at_space ? space + 1 : cut;
        while (start < cells.size() && cells[start].cp == ' ') ++start;
        width = width_of(cells, start, c);
        space = std::string_view::npos;
        for (size_t k = start; k < c; ++k) {
          if (cells[k].cp == ' ' && width_of(cells, start, k) > 0) space = k;
        }
      }
      width += cell.cols;
      if (width > max_chars) width = cell.cols;  // a glyph wider than the line still shows
    }
    std::string tail(start < cells.size() ? std::string(para.substr(cells[start].off)) : std::string());
    while (!tail.empty() && tail.back() == ' ') tail.pop_back();
    lines.push_back(tail);
    if (nl == std::string_view::npos) break;
    i = nl + 1;
  }
  while (!lines.empty() && lines.back().empty()) lines.pop_back();
  return lines;
}

}  // namespace hg

namespace hg::font {

int columns(std::string_view s) {
  int total = 0;
  size_t i = 0;
  while (i < s.size()) {
    const size_t start = i;
    const uint32_t cp = hg::font::decode(s, i);
    if (i == start) ++i;
    total += (cp != 0 && (cp < 0x80 || !hg::font::kMetrics.wide)) ? 1 : 2;
  }
  return total;
}

}  // namespace hg::font
