// The pure-emoji face (小墨), drawn from primitives. See hg/emoji_face.hpp.
//
// The artwork lives in a 240 px frame, authored in notes/设计-小墨纯表情.md
// (emote-pure-ui-v8-oval.png). Every state face shares one base eye — a tall
// oval, 50x100 at (72, 112) and (145, 112) — and the state is carried by the
// accessory: three arcs upper-right while listening, a small waveform under the
// eyes while speaking, three dots while thinking, a yellow ring while
// connecting, XX eyes while disconnected. The mood faces keep their own
// geometry: their eyes ARE the expression.
#include "hg/emoji_face.hpp"

#include <algorithm>
#include <cmath>

#include "hg/face_pen.hpp"

namespace hg::emoji {
namespace {

using Pen = FacePen;

// The design's own colours: the blue of sound, a pink heart, a sky-blue tear,
// the yellow of the connecting ring, and the grey of a sleeping Zz.
constexpr uint16_t kWave = rgb565(80, 160, 255);       // listening arcs, speaking waveform
constexpr uint16_t kPink = rgb565(247, 117, 146);
constexpr uint16_t kSky = rgb565(120, 200, 255);
constexpr uint16_t kRingYellow = rgb565(255, 205, 0);
constexpr uint16_t kZz = rgb565(170, 170, 190);        // the sleeping Z, nearer the head
constexpr uint16_t kZzSmall = rgb565(138, 138, 158);   // ...and the smaller one behind it

// The face is drawn a little larger than the box the hero layout hands us: the
// design frame is 240 px square and the artwork fills most of it, so 1.2 lands
// the connecting ring just inside the box. Overridable so a preview can compare.
#ifndef HG_EMOJI_ZOOM
#define HG_EMOJI_ZOOM 1.2f
#endif
constexpr float kZoom = HG_EMOJI_ZOOM;

// The one base eye every state face shares (v8, user-picked oval).
void draw_base_eyes(Pen& p, float cy = 112.0f) {
  p.fill_ellipse(72, cy, 25, 50, p.ink());
  p.fill_ellipse(145, cy, 25, 50, p.ink());
}

// Closed eyes: the blink, and the shape a sleeping face keeps.
void draw_bar(Pen& p, float cx, float cy) { p.solid_round(cx - 25, cy - 6, 50, 12, 6, p.ink()); }

// The listening arcs: three curves upper-right, slanting to the corner (a wifi
// fan turned ~45 deg), the ear that says she is listening.
void draw_listening_arcs(Pen& p) {
  for (float r : {28.0f, 42.0f, 56.0f}) p.arc(172, 112, r, 5, 270, 360, kWave);
}

// The three thinking dots, up and to the far side, one of them breathing.
void draw_thinking_dots(Pen& p, uint32_t frame) {
  const int active = static_cast<int>((frame / 3) % 4);
  const float cx[3] = {160, 180, 202};
  const float cy[3] = {46, 40, 34};
  const float r[3] = {5, 6, 7};
  for (int i = 0; i < 3; ++i) p.disc(cx[i], cy[i], r[i] + (i == active ? 1.0f : 0.0f), p.ink());
}

// The speaking waveform: a small squiggle under the eyes, ~50 px wide, not a
// big mouth. Batch 1 draws it fixed; batch 2 lets its height follow `level`.
void draw_waveform(Pen& p) {
  float px = 90, py = 205;
  for (int x = 91; x <= 140; ++x) {
    const float y = 205 + 7 * std::sin((static_cast<float>(x) - 90) / 50 * 3 * 2 * 3.14159265f);
    p.line(px, py, static_cast<float>(x), y, 4, kWave);
    px = static_cast<float>(x);
    py = y;
  }
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

void draw_heart(Pen& p, float cx, float cy, float r) {
  p.disc(cx - r * 0.45f, cy - r * 0.35f, r * 0.6f, kPink);
  p.disc(cx + r * 0.45f, cy - r * 0.35f, r * 0.6f, kPink);
  p.triangle(cx - r, cy - r * 0.05f, cx + r, cy - r * 0.05f, cx, cy + r, kPink);
}

void draw_tear(Pen& p, float cx, float cy) {
  p.disc(cx, cy, 4.5f, kSky);
  p.triangle(cx - 4.0f, cy - 0.5f, cx + 4.0f, cy - 0.5f, cx, cy - 10.0f, kSky);
}

// The confused "?": a hook on top, a short stem, and a dot. Drawn from
// primitives because the face carries no font.
void draw_question(Pen& p) {
  p.arc(214, 30, 9, 4, 180, 30, p.ink());
  p.line(214, 40, 214, 50, 4, p.ink());
  p.disc(214, 57, 2.5f, p.ink());
}

// The connecting ring: a 300 deg arc with a 60 deg gap, turning about the face
// (one turn every 2 s at 18 deg per 100 ms frame), the universal loading sign.
void draw_ring(Pen& p, uint32_t frame) {
  const int rot = static_cast<int>((frame * 18) % 360);
  p.arc(120, 118, 88, 5, static_cast<float>((300 + rot) % 360),
        static_cast<float>((240 + rot) % 360), kRingYellow);
}

}  // namespace

float zoom() { return kZoom; }

void draw(Canvas& c, int x, int y, int size, Look look, uint32_t frame, uint8_t level, uint16_t ink,
          uint16_t bg) {
  (void)level;  // batch 2: the speaking waveform follows it
  // Grow about the box's centre: the design frame maps to `size * zoom`, so the
  // face fills more of the panel. The canvas clips what leaves the hero band.
  const float grown = static_cast<float>(size) * kZoom;
  const int bx = x + size / 2 - static_cast<int>(grown / 2);
  const int by = y + size / 2 - static_cast<int>(grown / 2);
  Pen p(c, bx, by, static_cast<int>(grown), ink, bg);

  // A blink is two frames of forty-five: the open eyes close. Happy keeps its
  // arcs, sleeping is already shut, and XX eyes have nothing to close.
  const bool blink = look != Look::Happy && look != Look::Sleeping && look != Look::Disconnected &&
                     (frame % 45) < 2;

  switch (look) {
    case Look::Neutral:
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 112);
      } else {
        draw_base_eyes(p);
      }
      break;
    case Look::Listening:
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 112);
      } else {
        draw_base_eyes(p);
      }
      draw_listening_arcs(p);
      break;
    case Look::Thinking:
      if (blink) {
        draw_bar(p, 72, 104);
        draw_bar(p, 145, 104);
      } else {
        draw_base_eyes(p, 104);  // eyes up: she is thinking
      }
      draw_thinking_dots(p, frame);
      break;
    case Look::Speaking:
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 112);
      } else {
        draw_base_eyes(p);
      }
      draw_waveform(p);
      break;
    case Look::Happy:  // the arcs are the eyes; a small pink heart beside them
      p.arc(72, 112, 41, 10, 180, 360, p.ink());
      p.arc(168, 112, 41, 10, 180, 360, p.ink());
      draw_heart(p, 200, 66, 7.5f);
      break;
    case Look::Sad: {  // drooping half-discs: the bottom half is cut out of the bg
      if (blink) {
        draw_bar(p, 72, 134);
        draw_bar(p, 168, 134);
      } else {
        p.fill_ellipse(72, 134, 35, 35, p.ink());
        p.fill_ellipse(168, 134, 35, 35, p.ink());
        p.rect(37, 134, 70, 35, bg);
        p.rect(133, 134, 70, 35, bg);
        p.line(37, 112, 107, 100, 6, p.ink());   // the brows that carry the mood
        p.line(133, 100, 203, 112, 6, p.ink());
      }
      draw_tear(p, 196, 162);
      break;
    }
    case Look::Sleeping:  // closed eyes, all the time, and the drifting Zz
      p.solid_round(42, 128, 60, 12, 6, p.ink());
      p.solid_round(138, 128, 60, 12, 6, p.ink());
      draw_zz(p, frame);
      break;
    case Look::Confused: {  // one eye open, one squashed, a lid line and a "?"
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 128);
      } else {
        p.fill_ellipse(72, 112, 25, 50, p.ink());
        p.fill_ellipse(145, 128, 25, 29, p.ink());
        p.line(120, 112, 170, 106, 6, p.ink());
      }
      draw_question(p);
      break;
    }
    case Look::Connecting:
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 112);
      } else {
        draw_base_eyes(p);
      }
      draw_ring(p, frame);
      break;
    case Look::Disconnected:  // XX eyes: the robot face's sign for "broken / offline"
      p.line(72 - 22, 96, 72 + 22, 140, 10, p.ink());
      p.line(72 + 22, 96, 72 - 22, 140, 10, p.ink());
      p.line(168 - 22, 96, 168 + 22, 140, 10, p.ink());
      p.line(168 + 22, 96, 168 - 22, 140, 10, p.ink());
      break;
    case Look::Laughing:
    case Look::Loving:
    case Look::Surprised:
    case Look::Winking:
      // Phase 2 moods: they need host mood, so they fall back to Neutral until
      // then. The renderer knows them; the app never picks them yet.
      draw_base_eyes(p);
      break;
  }
}

}  // namespace hg::emoji
