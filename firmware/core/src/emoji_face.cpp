// The pure-emoji face (小墨), drawn from primitives. See hg/emoji_face.hpp.
//
// The artwork lives in a 240 px frame, authored in notes/设计-小墨纯表情.md
// (emote-pure-ui-v8-oval.png). Every state face shares one base eye — a tall
// oval, 50x100 at (72, 112) and (145, 112) — and the state is carried by the
// accessory: three arcs upper-right while listening, a small waveform under
// the eyes while speaking, three dots while thinking, a yellow ring while
// connecting, XX eyes while disconnected. The mood faces keep their own
// geometry: their eyes ARE the expression.
//
// The mood geometry is ported from the Xiaodou-style GIF designs in
// ~/workspace/xiaodou-emoji/expand/gen.py (the GIFs are the design
// authority). The GIFs draw on the same 240 px frame but with pill eyes at
// (80, 118) and (160, 118), so the accessory geometry is translated into the
// branch frame: eye geometry lands on the branch eye centers, face-centred
// accessories (mouth, "!!!") keep their x and shift y by -6, right-side
// accessories shift x by -15, left-side ones by -8. The GIFs are monochrome
// white; the branch keeps its own accent colours (sky-blue tears, pink
// hearts, blue waveform) from batch 1.
#include "hg/emoji_face.hpp"

#include <algorithm>
#include <cmath>

#include "hg/face_pen.hpp"

namespace hg::emoji {
namespace {

using Pen = FacePen;

// The design's own colours: the blue of sound, a pink heart, a sky-blue tear,
// the yellow of the connecting ring, and the grey of a sleeping Zz.
constexpr uint16_t kWave = rgb565(80, 160, 255);       // listening arcs, speaking bars
constexpr uint16_t kPink = rgb565(247, 117, 146);
constexpr uint16_t kSky = rgb565(120, 200, 255);
constexpr uint16_t kRingYellow = rgb565(255, 205, 0);
constexpr uint16_t kZz = rgb565(170, 170, 190);        // the sleeping Z

// The face is drawn a little larger than the box the hero layout hands us: the
// design frame is 240 px square and the artwork fills most of it, so 1.2 lands
// the connecting ring just inside the box. Overridable so a preview can compare.
#ifndef HG_EMOJI_ZOOM
#define HG_EMOJI_ZOOM 1.2f
#endif
constexpr float kZoom = HG_EMOJI_ZOOM;

constexpr float kPi = 3.14159265f;

// Frame index in the 20-frame GIF cycle (100 ms per frame).
uint32_t gif_frame(uint32_t frame) { return frame % 20; }
float gif_phase(uint32_t frame) {
  return 2.0f * kPi * static_cast<float>(gif_frame(frame)) / 20.0f;
}

// The one base eye every state face shares (v8, user-picked oval).
void draw_base_eyes(Pen& p, float cy = 112.0f) {
  p.fill_ellipse(72, cy, 25, 50, p.ink());
  p.fill_ellipse(145, cy, 25, 50, p.ink());
}

// Closed eyes: the blink, and the shape a sleeping face keeps.
void draw_bar(Pen& p, float cx, float cy) { p.solid_round(cx - 25, cy - 6, 50, 12, 6, p.ink()); }

// Scale a colour's brightness by f (0..1), keeping its hue. Used for the
// listening arcs' brightness wave.
uint16_t dim_rgb(uint8_t r, uint8_t g, uint8_t b, float f) {
  return rgb565(static_cast<uint8_t>(r * f), static_cast<uint8_t>(g * f),
                static_cast<uint8_t>(b * f));
}

// The listening arcs: three curves in the upper-right corner, GIF geometry —
// centre (163, 74) [GIF (178, 80) translated], radii 16/28/40, NE quadrant,
// width 4. A brightness wave travels outward: the inner arc peaks first.
void draw_listening_arcs(Pen& p, uint32_t frame) {
  const float radii[3] = {16.0f, 28.0f, 40.0f};
  const float phase = gif_phase(frame);
  for (int i = 0; i < 3; ++i) {
    const float g = 70.0f + 170.0f * (0.5f + 0.5f * std::sin(phase - i * 2.0f * kPi / 3.0f));
    const float f = g / 240.0f;  // 0.29 (dim) to 1.0 (full)
    p.arc(163, 74, radii[i], 4, 270, 360, dim_rgb(80, 160, 255, f));
  }
}

// The three thinking dots, up and to the far side, one of them breathing.
void draw_thinking_dots(Pen& p, uint32_t frame) {
  const int active = static_cast<int>((frame / 3) % 4);
  const float cx[3] = {160, 180, 202};
  const float cy[3] = {46, 40, 34};
  const float r[3] = {5, 6, 7};
  for (int i = 0; i < 3; ++i) p.disc(cx[i], cy[i], r[i] + (i == active ? 1.0f : 0.0f), p.ink());
}

// A "Z" of design size `s`, drawn as three strokes; the pair floats up and down.
void draw_zed(Pen& p, float x, float y, float s, uint16_t color) {
  p.line(x, y, x + s, y, 2.5f, color);
  p.line(x + s, y, x, y + s, 2.5f, color);
  p.line(x, y + s, x + s, y + s, 2.5f, color);
}

// A maraca/lollipop: a hollow ring on a tube handle. The ring is a full
// circle outline; the handle is a rounded-rect tube outline below it.
void draw_maraca(Pen& p, float cx, float cy_ring) {
  p.arc(cx, cy_ring, 15, 5, 0, 360, p.ink());
  p.outline_round(cx - 7, cy_ring + 12, 14, 40, 7, 5, p.ink());
}

// Happy: GIF happy — the same ∩∩ arcs, plus two maracas shaking side to
// side in opposite phase (they cross over). No heart.
void draw_happy_maracas(Pen& p, uint32_t frame) {
  const float sway = 20.0f * std::sin(gif_phase(frame));
  draw_maraca(p, 64 - sway, 137);
  draw_maraca(p, 102 + sway, 137);
}

// Sleeping: GIF sleepy — two U-shaped closed eyes, a flat T mouth, and a
// single small "z" that appears briefly (frames 13-15 of the 20-frame cycle).
// The U eyes are bottom-half arcs; the T mouth is a horizontal bar (thick
// line) with a stem below it, left of centre.
void draw_sleeping(Pen& p, uint32_t frame) {
  // U eyes: bottom-half arcs, top at y=86, r=22.
  p.arc(72, 86, 22, 8, 0, 180, p.ink());
  p.arc(145, 86, 22, 8, 0, 180, p.ink());
  // T mouth: bar from (85, 134) to (150, 134), width 25; stem at x=82-94.
  p.line(85, 134, 150, 134, 25, p.ink());
  p.solid_round(82, 147, 12, 16, 6, p.ink());
  // "z": visible only frames 13-15, matching the GIF's brief appearance.
  const uint32_t f = gif_frame(frame);
  if (f >= 13 && f < 16) draw_zed(p, 197, 111, 20, kZz);
}

void draw_tear(Pen& p, float cx, float cy) {
  p.disc(cx, cy, 4.5f, kSky);
  p.triangle(cx - 4.0f, cy - 0.5f, cx + 4.0f, cy - 0.5f, cx, cy - 10.0f, kSky);
}

// The "?" (silly): a hook on top, a short stem, and a dot. Drawn from
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

// ---- GIF-ported mood geometry (gen.py) ----------------------------------

// A wavy mouth: a sine-wave polyline, w wide, centred at (cx, cy).
void draw_wavy_mouth(Pen& p, uint32_t frame, float cx, float cy, float w) {
  const float phase = gif_phase(frame);
  const float amp = 3.0f, lam = 20.0f;
  float px = cx - w / 2;
  float py = cy + amp * std::sin(2.0f * kPi * (px - (cx - w / 2)) / lam + phase);
  for (float x = cx - w / 2 + 2; x <= cx + w / 2; x += 2) {
    const float fy = cy + amp * std::sin(2.0f * kPi * (x - (cx - w / 2)) / lam + phase);
    p.line(px, py, x, fy, 4, p.ink());
    px = x;
    py = fy;
  }
}

// A four-pointed star (the angry mark), drawn as a triangle fan from its
// centre. s is the tip-to-centre distance.
void draw_star4(Pen& p, float cx, float cy, float s, uint16_t color) {
  const float o = 0.28f * s;
  const float vx[8] = {0, o, s, o, 0, -o, -s, -o};
  const float vy[8] = {-s, -o, 0, o, s, o, 0, -o};
  for (int i = 0; i < 8; ++i) {
    const int j = (i + 1) % 8;
    p.triangle(cx, cy, cx + vx[i], cy + vy[i], cx + vx[j], cy + vy[j], color);
  }
}

// A classic heart eye, GIF geometry: lobes r=0.42s at (cx +/- 0.4s,
// cy - 0.28s), triangle (cx -/+ 0.8s, cy - 0.08s) -> (cx, cy + 0.82s).
void draw_heart_eye(Pen& p, float cx, float cy, float s, uint16_t color) {
  const float lr = 0.42f * s;
  p.disc(cx - 0.4f * s, cy - 0.28f * s, lr, color);
  p.disc(cx + 0.4f * s, cy - 0.28f * s, lr, color);
  p.triangle(cx - 0.8f * s, cy - 0.08f * s, cx + 0.8f * s, cy - 0.08f * s, cx,
             cy + 0.82f * s, color);
}

// A ">" or "<" eye: two lines meeting at the vertex. dir=+1 draws ">" (the
// vertex on the right), dir=-1 draws "<" (the vertex on the left).
void draw_chevron_eye(Pen& p, float cx, float cy, float half, int dir, float width) {
  const float vx = cx + static_cast<float>(dir) * half;
  p.line(vx, cy, cx - static_cast<float>(dir) * half, cy - half, width, p.ink());
  p.line(vx, cy, cx - static_cast<float>(dir) * half, cy + half, width, p.ink());
}

// Sad: drooping half-discs with the flat top slanting UP toward the nose
// (inner end higher, outer lower — Otto-measured), a wavy mouth, and TWO
// tears per eye streaming down, staggered. No brows.
void draw_sad(Pen& p, uint32_t frame) {
  // Left disc, centre (72, 114), r=22. Black cut polygon, vertices relative
  // to the GIF's (80, 120): (-24,-30),(24,-30),(22,-14),(-22,0).
  p.disc(72, 114, 22, p.ink());
  p.triangle(48, 84, 96, 84, 94, 100, p.bg());
  p.triangle(48, 84, 94, 100, 50, 114, p.bg());
  // Right disc, centre (145, 114), r=22. GIF cut polygon mirrored:
  // (-24,-30),(24,-30),(22,0),(-22,-14).
  p.disc(145, 114, 22, p.ink());
  p.triangle(121, 84, 169, 84, 167, 114, p.bg());
  p.triangle(121, 84, 167, 114, 123, 100, p.bg());
  // Wavy mouth at (120, 182), w=40.
  draw_wavy_mouth(p, frame, 120, 182, 40);
  // Two tears per eye, staggered 5 frames: t=(f+k*5)%10, y=126+3.0*t,
  // at x=48 (left) and x=169 (right).
  const uint32_t f = gif_frame(frame);
  for (int k = 0; k < 2; ++k) {
    const int t = static_cast<int>((f + k * 5) % 10);
    const float y = 126.0f + 3.0f * static_cast<float>(t);
    draw_tear(p, 48, y);
    draw_tear(p, 169, y);
  }
}

// Speaking: six Apple-style volume bars under the eyes, centred at (120,
// 194). GIF heights [0.22, 0.61, 1.0, 0.49, 0.77, 0.31] * 52, bar width 6,
// xs [88,101,114,127,140,153]. Batch 2: the height follows the mic level;
// with level 0 the fixed GIF heights are used.
void draw_speaking_bars(Pen& p, uint32_t frame, uint8_t level) {
  constexpr float kBase[6] = {0.22f, 0.61f, 1.0f, 0.49f, 0.77f, 0.31f};
  constexpr float kXs[6] = {88, 101, 114, 127, 140, 153};
  const float phase = gif_phase(frame);
  for (int i = 0; i < 6; ++i) {
    const float pulse = 0.6f + 0.4f * std::sin(phase + i * 1.1f);
    float h;
    if (level == 0) {
      h = 52.0f * kBase[i] * pulse;
    } else {
      // Louder mic -> taller bars, with a smaller dance on top.
      h = 52.0f * kBase[i] * (static_cast<float>(level) / 100.0f) *
          (0.85f + 0.15f * std::sin(phase + i * 1.1f));
    }
    const float hh = std::max(5.0f, h);
    p.solid_round(kXs[i] - 3, 194 - hh / 2, 6, hh, 3, kWave);
  }
}

// Laughing: ">" and "<" eyes + a big open mouth (bottom half-ellipse, 92
// wide, height 30-56 pulsing).
void draw_laughing(Pen& p, uint32_t frame) {
  draw_chevron_eye(p, 72, 112, 15, +1, 6);
  draw_chevron_eye(p, 145, 112, 15, -1, 6);
  const float h = 30.0f + 26.0f * (0.5f + 0.5f * std::sin(gif_phase(frame)));
  p.half_ellipse(120, 162, 46, h, p.ink());
}

// Loving: two big heart eyes, s=20, gentle pulse + bob.
void draw_loving(Pen& p, uint32_t frame) {
  const float phase = gif_phase(frame);
  const float s = 20.0f * (1.0f + 0.08f * std::sin(phase));
  const float bob = 4.0f * std::cos(phase);
  draw_heart_eye(p, 72, 112 + bob, s, kPink);
  draw_heart_eye(p, 145, 112 + bob, s, kPink);
}

// Winking: left eye open, right eye a smooth triangle-wave blink
// (open -> closed-line -> open, no static holds).
void draw_winking(Pen& p, uint32_t frame) {
  p.fill_ellipse(72, 112, 25, 50, p.ink());
  const uint32_t f = gif_frame(frame);
  const float op = std::fabs(static_cast<float>(static_cast<int>(f) - 10)) /
                   10.0f;  // 1 at f=0, 0 at f=10, 1 at f=20
  const float ry = std::max(2.0f, 50.0f * op);
  p.fill_ellipse(145, 112, 25, ry, p.ink());
}

// Angry: slanted half-disc eyes (steeper cut than Sad, inner end LOWER),
// a frown mouth, and a four-pointed star anger mark upper-right.
void draw_angry(Pen& p, uint32_t frame) {
  // Left disc, centre (72, 112), r=18. GIF cut polygon, vertices relative
  // to (80, 118): (-22,-28),(22,-28),(18,8),(-18,-14).
  p.disc(72, 112, 18, p.ink());
  p.triangle(50, 84, 94, 84, 90, 120, p.bg());
  p.triangle(50, 84, 90, 120, 54, 98, p.bg());
  // Right disc, centre (145, 112), r=18. GIF cut polygon mirrored:
  // (-22,-28),(22,-28),(18,-14),(-18,8).
  p.disc(145, 112, 18, p.ink());
  p.triangle(123, 84, 167, 84, 163, 98, p.bg());
  p.triangle(123, 84, 163, 98, 127, 120, p.bg());
  // Frown mouth: a cap arc at (120, 180), r=13.
  p.arc(120, 180, 13, 4, 180, 360, p.ink());
  // Anger mark: four-pointed star at (193, 42+bob), s=11*sc.
  const float phase = gif_phase(frame);
  const float sc = 1.0f + 0.12f * std::sin(phase);
  const float bob = 4.0f * std::cos(phase);
  draw_star4(p, 193, 42 + bob, 11.0f * sc, p.ink());
}

// Shocked: two SOLID white circles (r=20, pulsing), "!!!" above, and a
// sweat drop upper-right. Not hollow rings, not black-centre donuts.
void draw_shocked(Pen& p, uint32_t frame) {
  const float phase = gif_phase(frame);
  const float r = 20.0f * (1.0f + 0.08f * std::sin(phase));
  p.disc(72, 112, r, p.ink());
  p.disc(145, 112, r, p.ink());
  // "!!!": three short vertical bars at x=100/120/140, y=30-44, bobbing.
  const float bob = 2.0f * std::sin(phase + 1.0f);
  for (int i = 0; i < 3; ++i) {
    const float x = 100.0f + i * 20.0f;
    p.line(x, 30 + bob, x, 44 + bob, 4, p.ink());
  }
  // Sweat drop upper-right.
  draw_tear(p, 181, 62);
}

// Embarrassed: two squashed horizontal oval eyes + blush marks (three short
// diagonal lines under each eye). No sweat drop.
void draw_embarrassed(Pen& p, uint32_t frame) {
  const float phase = gif_phase(frame);
  const float h = 14.0f + 2.0f * std::sin(phase);
  p.solid_round(72 - 14, 112 - h / 2, 28, h, h / 2, p.ink());
  p.solid_round(145 - 14, 112 - h / 2, 28, h, h / 2, p.ink());
  // Blush: three short diagonal lines under each eye, bobbing.
  const float bob = 1.5f * std::cos(phase);
  for (int e = 0; e < 2; ++e) {
    const float ex = (e == 0) ? 72.0f : 145.0f;
    for (int k = 0; k < 3; ++k) {
      const float x0 = ex - 14 + k * 9;
      p.line(x0, 146 + bob, x0 + 6, 154 + bob, 3, p.ink());
    }
  }
}

// Silly: left standard eye + right "<" eye + "?" upper-right. Silly
// replaces Confused: the two differ only in the right eye, and the "<"
// reads better.
void draw_silly(Pen& p, uint32_t frame) {
  p.fill_ellipse(72, 112, 25, 50, p.ink());
  const float op = 4.0f * std::sin(gif_phase(frame));
  // "<": vertex at (131, 112), arms to (159, 98+op) and (159, 126-op).
  p.line(159, 98 + op, 131, 112, 6, p.ink());
  p.line(131, 112, 159, 126 - op, 6, p.ink());
  draw_question(p);
}

}  // namespace

float zoom() { return kZoom; }

void draw(Canvas& c, int x, int y, int size, Look look, uint32_t frame, uint8_t level, uint16_t ink,
          uint16_t bg) {
  // Grow about the box's centre: the design frame maps to `size * zoom`, so the
  // face fills more of the panel. The canvas clips what leaves the hero band.
  const float grown = static_cast<float>(size) * kZoom;
  const int bx = x + size / 2 - static_cast<int>(grown / 2);
  const int by = y + size / 2 - static_cast<int>(grown / 2);
  Pen p(c, bx, by, static_cast<int>(grown), ink, bg);

  // A blink is two frames of forty-five: the open eyes close. Happy keeps its
  // arcs, sleeping is already shut, and XX eyes have nothing to close; Laughing
  // and Loving are exempt too — their eyes are the expression ("><" and
  // hearts), not open eyes that can close.
  const bool blink = look != Look::Happy && look != Look::Sleeping && look != Look::Disconnected &&
                     look != Look::Laughing && look != Look::Loving && (frame % 45) < 2;

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
      draw_listening_arcs(p, frame);
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
      draw_speaking_bars(p, frame, level);
      break;
    case Look::Happy:  // GIF happy: ∩∩ arcs + two shaking maracas (no heart)
      p.arc(72, 112, 41, 10, 180, 360, p.ink());
      p.arc(168, 112, 41, 10, 180, 360, p.ink());
      draw_happy_maracas(p, frame);
      break;
    case Look::Sad:  // GIF sad v7: drooping half-discs, wavy mouth, two tears per eye
      if (blink) {
        draw_bar(p, 72, 114);
        draw_bar(p, 145, 114);
      } else {
        draw_sad(p, frame);
      }
      break;
    case Look::Sleeping:  // GIF sleepy: U eyes, T mouth, a brief "z"
      draw_sleeping(p, frame);
      break;
    case Look::Silly:  // left eye open, right "<" eye, and a "?"
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 112);
      } else {
        draw_silly(p, frame);
      }
      break;
    case Look::Laughing:  // "><" eyes and a big open mouth; never blinks away
      draw_laughing(p, frame);
      break;
    case Look::Loving:  // two big heart eyes; never blinks away
      draw_loving(p, frame);
      break;
    case Look::Angry:
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 112);
      } else {
        draw_angry(p, frame);
      }
      break;
    case Look::Shocked:
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 112);
      } else {
        draw_shocked(p, frame);
      }
      break;
    case Look::Embarrassed:
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 112);
      } else {
        draw_embarrassed(p, frame);
      }
      break;
    case Look::Winking:
      if (blink) {
        draw_bar(p, 72, 112);
        draw_bar(p, 145, 112);
      } else {
        draw_winking(p, frame);
      }
      break;
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
  }
}

}  // namespace hg::emoji
