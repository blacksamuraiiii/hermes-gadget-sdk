// The pure-emoji face: 小墨's eyes-only faces, drawn from primitives. These tests
// pin what each look puts on screen, because a face that silently stops drawing
// (an empty arc, a colour that never lands) is the failure that reaches the board.
#include <cstdint>
#include <vector>

#include "check.hpp"
#include "hg/canvas.hpp"
#include "hg/emoji_face.hpp"

namespace {

constexpr int kCanvas = 240;  // a moji-sized panel
constexpr int kBox = 150;     // the hero box it hands the face (the panel's usable square)
constexpr int kBoxX = 45, kBoxY = 30;
constexpr uint16_t kInk = hg::rgb565(232, 238, 242);
constexpr uint16_t kBg = hg::rgb565(10, 14, 20);

using Frame = std::vector<uint16_t>;

Frame render(hg::emoji::Look look, uint32_t frame = 10, uint8_t level = 0) {
  Frame fb(static_cast<size_t>(kCanvas) * kCanvas, kBg);
  hg::Canvas c(fb.data(), kCanvas, kCanvas, false);
  hg::emoji::draw(c, kBoxX, kBoxY, kBox, look, frame, level, kInk, kBg);
  return fb;
}

int count(const Frame& fb, uint16_t colour) {
  int n = 0;
  for (uint16_t p : fb) n += p == colour ? 1 : 0;
  return n;
}

// Count a colour in a canvas rectangle [x0, x1) x [y0, y1). The face is drawn
// in a 150 px box at (45, 30) with zoom 1.2, so design (x, y) lands at canvas
// (30 + 0.75x, 15 + 0.75y).
int count_rect(const Frame& fb, uint16_t colour, int x0, int y0, int x1, int y1) {
  int n = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) n += fb[static_cast<size_t>(y) * kCanvas + x] == colour ? 1 : 0;
  return n;
}

uint16_t pixel(const Frame& fb, int x, int y) { return fb[static_cast<size_t>(y) * kCanvas + x]; }

int differences(const Frame& a, const Frame& b) {
  int n = 0;
  for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i] ? 1 : 0;
  return n;
}

}  // namespace

TEST("emoji face: every state look draws a face, and each one differs from neutral") {
  const Frame neutral = render(hg::emoji::Look::Neutral);
  CHECK(count(neutral, kInk) > 500);  // the two tall oval eyes alone are a few hundred pixels
  const hg::emoji::Look others[] = {hg::emoji::Look::Happy, hg::emoji::Look::Sad,
                                    hg::emoji::Look::Thinking, hg::emoji::Look::Sleeping,
                                    hg::emoji::Look::Listening, hg::emoji::Look::Speaking,
                                    hg::emoji::Look::Silly, hg::emoji::Look::Laughing,
                                    hg::emoji::Look::Loving, hg::emoji::Look::Angry,
                                    hg::emoji::Look::Shocked, hg::emoji::Look::Embarrassed,
                                    hg::emoji::Look::Winking, hg::emoji::Look::Connecting,
                                    hg::emoji::Look::Disconnected};
  for (hg::emoji::Look look : others) {
    const Frame fb = render(look);
    CHECK(differences(neutral, fb) > 40);  // a look that changes fewer pixels is invisible
  }
}

TEST("emoji face: the looks carry their own colours") {
  const auto pink = hg::rgb565(247, 117, 146);
  const auto sky = hg::rgb565(120, 200, 255);
  const auto wave = hg::rgb565(80, 160, 255);
  const auto ring = hg::rgb565(255, 205, 0);
  const auto zz = hg::rgb565(170, 170, 190);
  // Happy no longer has a pink heart (the GIF has maracas instead); the pink
  // lives only in Loving's heart eyes now.
  CHECK(count(render(hg::emoji::Look::Happy), pink) == 0);
  CHECK(count(render(hg::emoji::Look::Loving), pink) > 40);       // the heart eyes
  CHECK(count(render(hg::emoji::Look::Sad), sky) > 30);           // the tears
  CHECK(count(render(hg::emoji::Look::Shocked), sky) > 5);        // the sweat drop
  // Listening arcs breathe: at frame 5 the inner arc is at full brightness.
  CHECK(count(render(hg::emoji::Look::Listening, 5), wave) > 20); // the inner arc
  CHECK(count(render(hg::emoji::Look::Speaking), wave) > 20);     // the waveform
  CHECK(count(render(hg::emoji::Look::Connecting), ring) > 100);  // the yellow ring
  // Sleeping's single "z" appears only frames 13-15 of the 20-frame cycle.
  CHECK(count(render(hg::emoji::Look::Sleeping, 14), zz) > 20);   // the "z"
  CHECK(count(render(hg::emoji::Look::Sleeping, 10), zz) == 0);   // ...not at frame 10
  CHECK(count(render(hg::emoji::Look::Neutral), pink) == 0);      // and nowhere else
  CHECK(count(render(hg::emoji::Look::Neutral), sky) == 0);
  CHECK(count(render(hg::emoji::Look::Neutral), wave) == 0);
  CHECK(count(render(hg::emoji::Look::Neutral), ring) == 0);
}

TEST("emoji face: the same state draws the same pixels") {
  CHECK(render(hg::emoji::Look::Thinking, 21) == render(hg::emoji::Look::Thinking, 21));
  CHECK(render(hg::emoji::Look::Neutral, 10, 40) == render(hg::emoji::Look::Neutral, 10, 40));
}

TEST("emoji face: the animated parts move, the still ones do not") {
  // A blink is two frames of forty-five, so the faces with open eyes change.
  CHECK(differences(render(hg::emoji::Look::Neutral, 10), render(hg::emoji::Look::Neutral, 0)) > 20);
  // The thinking dots take turns.
  CHECK(differences(render(hg::emoji::Look::Thinking, 3), render(hg::emoji::Look::Thinking, 12)) > 5);
  // Sleeping's single "z" appears only frames 13-15: frame 10 has none,
  // frame 14 has one.
  CHECK(differences(render(hg::emoji::Look::Sleeping, 10), render(hg::emoji::Look::Sleeping, 14)) > 5);
  // The connecting ring turns: a quarter turn is a different picture.
  CHECK(differences(render(hg::emoji::Look::Connecting, 0), render(hg::emoji::Look::Connecting, 5)) > 20);
  // The GIF-ported moods animate on the 20-frame cycle.
  CHECK(differences(render(hg::emoji::Look::Sad, 5), render(hg::emoji::Look::Sad, 15)) > 5);
  CHECK(differences(render(hg::emoji::Look::Laughing, 0), render(hg::emoji::Look::Laughing, 5)) > 5);
  CHECK(differences(render(hg::emoji::Look::Winking, 0), render(hg::emoji::Look::Winking, 10)) > 20);
  CHECK(differences(render(hg::emoji::Look::Shocked, 0), render(hg::emoji::Look::Shocked, 5)) > 5);
  CHECK(differences(render(hg::emoji::Look::Silly, 0), render(hg::emoji::Look::Silly, 5)) > 5);
  // The three aligned looks animate too: listening arcs breathe, happy
  // maracas sway.
  CHECK(differences(render(hg::emoji::Look::Listening, 0), render(hg::emoji::Look::Listening, 5)) > 5);
  CHECK(differences(render(hg::emoji::Look::Happy, 0), render(hg::emoji::Look::Happy, 5)) > 5);
}

TEST("emoji face: the speaking bars follow the mic level in batch 2") {
  // Level 0 uses the fixed GIF heights, so the same frame is deterministic.
  CHECK(render(hg::emoji::Look::Speaking, 10, 0) == render(hg::emoji::Look::Speaking, 10, 0));
  // A loud mic draws taller bars than silence at the same frame.
  CHECK(differences(render(hg::emoji::Look::Speaking, 10, 0), render(hg::emoji::Look::Speaking, 10, 100)) > 0);
}

TEST("emoji face: sad has a wavy mouth and no brows") {
  // The wavy mouth sits at design (120, 182), w=40: canvas x 105-135, y 148-155.
  const Frame sad = render(hg::emoji::Look::Sad);
  CHECK(count_rect(sad, kInk, 105, 148, 136, 155) > 10);
  CHECK(count_rect(render(hg::emoji::Look::Neutral), kInk, 105, 148, 136, 155) == 0);
  // The old brows ran from design (37, 112) to (107, 100); the new face has no
  // ink in the strip left of the left disc. Canvas x 57-64, y 95-103.
  CHECK(count_rect(sad, kInk, 57, 95, 65, 103) == 0);
}

TEST("emoji face: shocked eyes are solid discs") {
  // The eye centres are design (72, 112) and (145, 112): canvas (84, 99) and
  // (139, 99). A hollow ring or a black-centre donut would leave them background.
  const Frame shocked = render(hg::emoji::Look::Shocked);
  CHECK(pixel(shocked, 84, 99) == kInk);
  CHECK(pixel(shocked, 139, 99) == kInk);
}

TEST("emoji face: silly has a question mark") {
  // The "?" sits at design x~205-223, y~21-60: canvas x 184-198, y 36-59.
  const Frame silly = render(hg::emoji::Look::Silly);
  CHECK(count_rect(silly, kInk, 184, 36, 198, 59) > 5);
  CHECK(count_rect(render(hg::emoji::Look::Neutral), kInk, 184, 36, 198, 59) == 0);
}

TEST("emoji face: sleeping has U eyes and a T mouth, not bars") {
  // The U eyes are bottom-half arcs at design (72, 86) and (145, 86), r=22:
  // canvas y for the arc bottom (design y~108) is 15+0.75*108 = 96. The old
  // bars were horizontal bars at design y=128 (canvas y 111).
  const Frame sleeping = render(hg::emoji::Look::Sleeping);
  // Ink at the U bottom (canvas x~84, y~96) and none where the old bar was.
  CHECK(pixel(sleeping, 84, 96) == kInk);
  CHECK(count_rect(sleeping, kInk, 57, 111, 63, 117) == 0);
  // The T mouth bar is design (85-150, 134), w=25. Between the two oval
  // eyes (canvas x gap 103-120) the bar is the only ink: canvas x 106-117,
  // y 112-122.
  CHECK(count_rect(sleeping, kInk, 106, 112, 117, 122) > 10);
  CHECK(count_rect(render(hg::emoji::Look::Neutral), kInk, 106, 112, 117, 122) == 0);
}

TEST("emoji face: happy has maracas and no heart") {
  // The maraca rings are at design (64, 137) and (102, 137), r=13: canvas
  // (78, 118) and (107, 118). The old pink heart was at design (200, 66).
  const Frame happy = render(hg::emoji::Look::Happy);
  CHECK(count_rect(happy, kInk, 70, 110, 86, 126) > 10);   // left ring
  CHECK(count_rect(happy, kInk, 99, 110, 115, 126) > 10);  // right ring
  CHECK(count_rect(happy, kInk, 180, 60, 190, 70) == 0);   // no heart there
}

TEST("emoji face: listening arcs sit in the upper-right corner") {
  // GIF arcs are centred design (163, 74), radii 16/28/40. At frame 5 the
  // inner arc (r=16) is at full brightness: it runs design (163, 58) to
  // (179, 74), canvas (152, 58) to (164, 70). The old arcs were centred at
  // design (172, 112), r up to 56.
  const Frame listening = render(hg::emoji::Look::Listening, 5);
  CHECK(count_rect(listening, hg::rgb565(80, 160, 255), 152, 58, 164, 70) > 5);
  CHECK(count_rect(render(hg::emoji::Look::Neutral), hg::rgb565(80, 160, 255), 152, 58, 164, 70) == 0);
}

// Confused is gone: it is not in the Look enum any more (this file no longer
// references it, and the code compiles without it), and Silly covers it. The
// Phase 2 moods (Laughing, Loving, Winking, Angry, Shocked, Embarrassed,
// Silly) are now drawn instead of falling back to Neutral — the "differs from
// neutral" test above pins that. Surprised is gone too: shocked covers it.
