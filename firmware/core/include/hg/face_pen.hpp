// A shared pen for the board faces (hg/robot_face.hpp, hg/emoji_face.hpp):
// every coordinate is in the 240 px design frame the artwork was drawn in,
// and the pen turns it into device pixels. Moved out of robot_face.cpp so the
// pure-emoji face draws with the same primitives instead of a second copy.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "hg/canvas.hpp"

namespace hg {

class FacePen {
 public:
  FacePen(Canvas& c, int x, int y, int size, uint16_t ink, uint16_t bg)
      : c_(c), k_(size / kDesign), ox_(x), oy_(y), ink_(ink), bg_(bg) {}

  int px(float x) const { return static_cast<int>(std::lround(ox_ + x * k_)); }
  int py(float y) const { return static_cast<int>(std::lround(oy_ + y * k_)); }
  int len(float design_len) const { return std::max(1, static_cast<int>(std::lround(design_len * k_))); }
  float design_x(int device_x) const { return (static_cast<float>(device_x) - ox_) / k_; }
  float design_y(int device_y) const { return (static_cast<float>(device_y) - oy_) / k_; }

  Canvas& canvas() { return c_; }
  uint16_t ink() const { return ink_; }
  uint16_t bg() const { return bg_; }

  // A thick segment with round caps, in design units.
  void line(float x0, float y0, float x1, float y1, float width, uint16_t color) {
    const float half = width / 2;
    const int lo_x = px(std::min(x0, x1) - half), hi_x = px(std::max(x0, x1) + half);
    const int lo_y = py(std::min(y0, y1) - half), hi_y = py(std::max(y0, y1) + half);
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    for (int y = lo_y; y <= hi_y; ++y) {
      for (int x = lo_x; x <= hi_x; ++x) {
        const float ddx = design_x(x) - x0, ddy = design_y(y) - y0;
        float t = len2 > 0 ? (ddx * dx + ddy * dy) / len2 : 0;
        t = std::clamp(t, 0.0f, 1.0f);
        const float ex = ddx - t * dx, ey = ddy - t * dy;
        if (ex * ex + ey * ey <= half * half) c_.pixel(x, y, color);
      }
    }
  }

  // A thick arc between two angles (degrees, 0 = right, 90 = down). Round caps.
  void arc(float cx, float cy, float r, float width, float a0, float a1, uint16_t color) {
    const float half = width / 2;
    const float inner = std::max(0.0f, r - half), outer = r + half;
    const int lo_x = px(cx - outer), hi_x = px(cx + outer);
    const int lo_y = py(cy - outer), hi_y = py(cy + outer);
    const float in2 = inner * inner, out2 = outer * outer;
    const bool wrapped = a0 > a1;
    for (int y = lo_y; y <= hi_y; ++y) {
      for (int x = lo_x; x <= hi_x; ++x) {
        const float dx = design_x(x) - cx, dy = design_y(y) - cy;
        const float d2 = dx * dx + dy * dy;
        if (d2 > out2 || d2 < in2) continue;
        float deg = std::atan2(dy, dx) * 57.29578f;
        if (deg < 0) deg += 360;
        const bool inside = wrapped ? (deg >= a0 || deg <= a1) : (deg >= a0 && deg <= a1);
        if (inside) c_.pixel(x, y, color);
      }
    }
    const float r0 = a0 * 0.0174532925f, r1 = a1 * 0.0174532925f;
    disc(cx + r * std::cos(r0), cy + r * std::sin(r0), half, color);
    disc(cx + r * std::cos(r1), cy + r * std::sin(r1), half, color);
  }

  void disc(float cx, float cy, float r, uint16_t color) { c_.fill_circle(px(cx), py(cy), len(r), color); }

  // A half ellipse with a flat top: the laugh's mouth. Widest along the flat top and
  // narrowing to the bottom, the way the design sheet draws it.
  void half_ellipse(float cx, float top, float rx, float ry, uint16_t color) {
    for (int y = py(top); y <= py(top + ry); ++y) {
      const float dy = design_y(y) - top;
      if (dy < 0 || dy > ry) continue;
      const float t = dy / ry;                         // 0 at the flat top, 1 at the bottom
      const float half_w = rx * std::sqrt(std::max(0.0f, 1.0f - t * t));
      const int x0 = px(cx - half_w), x1 = px(cx + half_w);
      for (int x = x0; x <= x1; ++x) c_.pixel(x, y, color);
    }
  }

  // A filled ellipse, centred on (cx, cy): the emoji face's tall oval eyes.
  void fill_ellipse(float cx, float cy, float rx, float ry, uint16_t color) {
    for (int y = py(cy - ry); y <= py(cy + ry); ++y) {
      const float t = (design_y(y) - cy) / ry;
      if (t < -1.0f || t > 1.0f) continue;
      const float half_w = rx * std::sqrt(std::max(0.0f, 1.0f - t * t));
      const int x0 = px(cx - half_w), x1 = px(cx + half_w);
      for (int x = x0; x <= x1; ++x) c_.pixel(x, y, color);
    }
  }

  // A filled rectangle, in design units (a sad eye is cut out of it).
  void rect(float x, float y, float w, float h, uint16_t color) {
    c_.fill_rect(px(x), py(y), len(w), len(h), color);
  }

  void solid_round(float x, float y, float w, float h, float r, uint16_t color) {
    c_.fill_round_rect(px(x), py(y), len(w), len(h), len(r), color);
  }

  // The outline of a rounded rectangle, `stroke` wide, drawn inside the path the way
  // the artwork's own strokes sit: the pixels whose distance to the boundary is
  // between -stroke and 0.
  void outline_round(float x, float y, float w, float h, float r, float stroke, uint16_t color) {
    const float cx = x + w / 2, cy = y + h / 2, hw = w / 2, hh = h / 2;
    const float rc = std::min(r, std::min(hw, hh));
    const int lo_x = px(x - 1), hi_x = px(x + w + 1);
    const int lo_y = py(y - 1), hi_y = py(y + h + 1);
    for (int yy = lo_y; yy <= hi_y; ++yy) {
      for (int xx = lo_x; xx <= hi_x; ++xx) {
        const float qx = std::fabs(design_x(xx) - cx) - (hw - rc);
        const float qy = std::fabs(design_y(yy) - cy) - (hh - rc);
        const float ax = std::max(qx, 0.0f), ay = std::max(qy, 0.0f);
        const float d = std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.0f) - rc;
        if (d <= 0.0f && d > -stroke) c_.pixel(xx, yy, color);
      }
    }
  }

  void triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t color) {
    const int lo_x = px(std::min({x0, x1, x2})), hi_x = px(std::max({x0, x1, x2}));
    const int lo_y = py(std::min({y0, y1, y2})), hi_y = py(std::max({y0, y1, y2}));
    auto side = [](float ax, float ay, float bx, float by, float cx, float cy) {
      return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    };
    for (int y = lo_y; y <= hi_y; ++y) {
      for (int x = lo_x; x <= hi_x; ++x) {
        const float pxd = design_x(x), pyd = design_y(y);
        const float d0 = side(x0, y0, x1, y1, pxd, pyd);
        const float d1 = side(x1, y1, x2, y2, pxd, pyd);
        const float d2 = side(x2, y2, x0, y0, pxd, pyd);
        if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) c_.pixel(x, y, color);
      }
    }
  }

 private:
  Canvas& c_;
  float k_;
  float ox_, oy_;
  uint16_t ink_, bg_;
  static constexpr float kDesign = 240.0f;  // the frame the artwork was drawn in
};

}  // namespace hg
