// The robot face (小墨), drawn from primitives. See hg/robot_face.hpp.
//
// The artwork lives in a 240 px frame: head 125x89 at (58, 78) with a 5 px stroke,
// an antenna with a ball on top, rectangular ears, dot eyes at y 110, the mouth at
// y 145, and the moods' extras (heart, tear, thinking dots, Zz, sound waves) beside
// the head. Everything below is in those 240 units and scaled at draw time, so the
// face keeps its proportions on any panel.
#include "hg/robot_face.hpp"

#include <algorithm>
#include <cmath>

namespace hg::robot {
namespace {

constexpr float kDesign = 240.0f;  // the frame the artwork was drawn in

// The design's own colours: white ink, a pink heart, sky-blue tear and waves, and
// the grey of a sleeping Zz.
constexpr uint16_t kInkBright = rgb565(232, 238, 242);
constexpr uint16_t kPink = rgb565(247, 117, 146);
constexpr uint16_t kSky = rgb565(120, 200, 255);
constexpr uint16_t kSkyDim = rgb565(42, 78, 112);
constexpr uint16_t kZz = rgb565(170, 170, 190);       // the sleeping Z, nearer the head
constexpr uint16_t kZzSmall = rgb565(138, 138, 158);  // ...and the smaller one behind it

// The face is drawn larger than the box the hero layout hands us: the design frame is
// 240 px square and the head inside it is only 105 wide, which on a 240 px panel reads
// as a small stamp. Overridable so a preview can compare two sizes.
#ifndef HG_ROBOT_ZOOM
#define HG_ROBOT_ZOOM 2.0f
#endif
constexpr float kZoom = HG_ROBOT_ZOOM;

// A pen over the design frame: every coordinate below is in design units, and the
// pen turns them into device pixels.
class Pen {
 public:
  Pen(Canvas& c, int x, int y, int size, uint16_t ink, uint16_t bg)
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
};

// -- the parts ------------------------------------------------------------------------------------------
//
// Every number below was measured off the design sheet (notes/设计-小墨表情.md,
// emote-demo-240.png), in its 240 px frame:
//   head    x 58..182, y 78..166, 5 px stroke, corners ~18
//   antenna x 115..125, y 52..74 (ball at the top, 5 px stalk)
//   ear     x 63..71 / 170..178, y 116..132 (a block inside the head's side)
//   eyes    centres (98, 110) and (142, 110); 17 px across, 23 px when curved
//   mouth   centred on x 120: smile y 144..154, laugh 145..156, frown 138..148, "o" 138..148

void draw_shell(Pen& p, uint16_t antenna) {
  const uint16_t ink = p.ink();
  p.line(120, 78, 120, 56, 4.5f, ink);             // antenna stalk (longer, always ink)
  p.disc(120, 49, 5.5f, antenna);                 // ...and its ball: the link lamp
  p.solid_round(58, 114, 10, 20, 3, ink);         // near ear, out past the head's edge
  p.solid_round(173, 114, 10, 20, 3, ink);        // far ear
  p.outline_round(68, 78, 105, 89, 14, 4, ink);   // head
}

// One eye: a dot with a highlight, or a curve for the moods that close them.
enum class EyeStyle { Dot, Smile, Closed, Wide };

void draw_eye(Pen& p, float cx, float cy, EyeStyle style) {
  switch (style) {
    case EyeStyle::Dot:
      p.disc(cx, cy, 8.5f, p.ink());
      break;
    case EyeStyle::Wide:
      p.disc(cx, cy, 10, p.ink());
      break;
    case EyeStyle::Smile:  // a happy eye curves up
      p.arc(cx, cy + 5, 9, 5, 200, 340, p.ink());
      break;
    case EyeStyle::Closed:  // asleep: the lids come down
      p.arc(cx, cy + 7.5f, 9, 5, 20, 160, p.ink());
      break;
  }
}

void draw_mouth(Pen& p, Look look, uint8_t level) {
  const float cx = 120;
  switch (look) {
    case Look::Happy: {  // a wide open laugh: flat on top, round underneath
      p.half_ellipse(cx, 145, 16.5f, 11, p.ink());
      break;
    }
    case Look::Sad:  // the corners turn down
      p.arc(cx, 148, 13, 4, 200, 340, p.ink());
      break;
    case Look::Thinking:
    case Look::Sleeping:  // a small round mouth, as the design sheet draws it
      p.canvas().ring(p.px(cx), p.py(143), p.len(4.5f), p.len(3), p.ink());
      break;
    case Look::Listening:
      p.canvas().ring(p.px(cx), p.py(143), p.len(5.5f), p.len(3.5f), p.ink());
      break;
    case Look::Speaking: {
      const float r = 3.5f + static_cast<float>(std::min<uint8_t>(level, 100)) * 0.05f;
      p.canvas().ring(p.px(cx), p.py(143), p.len(r), p.len(3.5f), p.ink());
      break;
    }
    case Look::Neutral:  // a straight mouth at rest
      p.line(cx - 15, 149, cx + 15, 149, 4, p.ink());
      break;
  }
}

void draw_heart(Pen& p, float cx, float cy, float r) {
  p.disc(cx - r * 0.45f, cy - r * 0.35f, r * 0.6f, kPink);
  p.disc(cx + r * 0.45f, cy - r * 0.35f, r * 0.6f, kPink);
  p.triangle(cx - r, cy - r * 0.05f, cx + r, cy - r * 0.05f, cx, cy + r, kPink);
}

void draw_tear(Pen& p, float cx, float cy) {
  p.disc(cx, cy, 4.5f, kSky);
  p.triangle(cx - 4.0f, cy - 0.5f, cx + 4.0f, cy - 0.5f, cx, cy - 10.0f, kSky);
}

// The waves beside the far ear while she listens: lit one after another.
void draw_waves(Pen& p, uint32_t frame) {
  const int lit = 1 + static_cast<int>((frame / 3) % 3);
  for (int i = 0; i < 3; ++i) {
    p.arc(196, 111, 10.0f + 7.0f * static_cast<float>(i), 4, -50, 50, i < lit ? kSky : kSkyDim);
  }
}

// The three thinking dots, up and to the far side, one of them breathing.
void draw_dots(Pen& p, uint32_t frame) {
  const int active = static_cast<int>((frame / 3) % 4);
  const float cx[3] = {202, 190, 178};
  const float cy[3] = {52, 60, 68};
  for (int i = 0; i < 3; ++i) p.disc(cx[i], cy[i], 3.2f + (i == active ? 1.0f : 0.0f), p.ink());
}

// A "Z" of design size `s`, drawn as three strokes; the pair floats up and down.
void draw_zed(Pen& p, float x, float y, float s, uint16_t color) {
  p.line(x, y, x + s, y, 2.5f, color);
  p.line(x + s, y, x, y + s, 2.5f, color);
  p.line(x, y + s, x + s, y + s, 2.5f, color);
}

void draw_zz(Pen& p, uint32_t frame) {
  const float drift = static_cast<float>((frame / 5) % 6) - 2.5f;
  draw_zed(p, 179, 70 + drift, 16, kZz);   // the big one, nearer the head
  draw_zed(p, 197, 81 + drift, 8, kZzSmall);
}

}  // namespace

float zoom() { return kZoom; }

void draw(Canvas& c, int x, int y, int size, Look look, uint32_t frame, uint8_t level, uint16_t ink,
          uint16_t bg, uint16_t antenna) {
  // Grow about the box's centre: the design frame maps to `size * zoom`, so the head
  // fills more of the panel. The canvas clips what leaves the hero band.
  const float grown = static_cast<float>(size) * kZoom;
  const int bx = x + size / 2 - static_cast<int>(grown / 2);
  const int by = y + size / 2 - static_cast<int>(grown / 2);
  Pen p(c, bx, by, static_cast<int>(grown), ink, bg);
  draw_shell(p, antenna);

  const bool blink = look != Look::Happy && look != Look::Sleeping && (frame % 45) < 2;
  const EyeStyle style = [&] {
    if (blink) return EyeStyle::Closed;
    switch (look) {
      case Look::Happy: return EyeStyle::Smile;
      case Look::Sleeping: return EyeStyle::Closed;
      case Look::Listening: return EyeStyle::Wide;
      default: return EyeStyle::Dot;
    }
  }();
  // Sleeping eyes sit lower: the lids have come down (the arc offset carries that).
  const float eye_y = 110.0f;
  draw_eye(p, 98, eye_y, style);
  draw_eye(p, 142, eye_y, style);

  if (look == Look::Sad) {  // the brows that carry the mood: inner ends drop
    p.line(88, 93, 108, 101, 4, p.ink());
    p.line(152, 93, 132, 101, 4, p.ink());
  }
  if (look == Look::Thinking) {  // a straight lid over each eye, the far one lower
    p.line(88, 94, 108, 94, 3, p.ink());
    p.line(132, 97, 152, 97, 3.5f, p.ink());
  }

  draw_mouth(p, look, level);

  switch (look) {
    case Look::Happy: draw_heart(p, 51, 72, 7.5f); break;
    case Look::Sad: draw_tear(p, 94, 133); break;
    case Look::Thinking: draw_dots(p, frame); break;
    case Look::Sleeping: draw_zz(p, frame); break;
    case Look::Listening: draw_waves(p, frame); break;
    default: break;
  }
}

}  // namespace hg::robot
