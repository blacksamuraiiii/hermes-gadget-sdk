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
                                    hg::emoji::Look::Winking, hg::emoji::Look::Excited,
                                    hg::emoji::Look::Bored, hg::emoji::Look::Cool,
                                    hg::emoji::Look::Hi, hg::emoji::Look::Disconnected};
  // Connecting is deliberately absent: it draws the neutral face and leaves the spinner
  // to Ui::draw_hero, which draws it in the panel's own frame so it hugs the glass.
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
  // The spinner belongs to the glass, not the face: Ui::draw_hero draws it on the
  // panel edge, so the face itself carries no ring pixels (notes/设计-小墨纯表情.md §6).
  CHECK(count(render(hg::emoji::Look::Connecting), ring) == 0);
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
  // Excited's stars pulse and its grin breathes; Bored's half-discs droop.
  CHECK(differences(render(hg::emoji::Look::Excited, 0), render(hg::emoji::Look::Excited, 5)) > 5);
  CHECK(differences(render(hg::emoji::Look::Bored, 0), render(hg::emoji::Look::Bored, 5)) > 5);
  // Cool's sunglasses and necklace bob; Hi's eyes grow and its rays pulse.
  CHECK(differences(render(hg::emoji::Look::Cool, 0), render(hg::emoji::Look::Cool, 5)) > 5);
  CHECK(differences(render(hg::emoji::Look::Hi, 2), render(hg::emoji::Look::Hi, 10)) > 5);
}

TEST("emoji face: the speaking bars run a fixed GIF wave, not the mic") {
  // The bars stopped following the microphone (2026-11-09): playback runs on the board's
  // own speaker with one mic and no AEC, so a "real" level is either her own voice or a
  // reading that never updates. Same frame, any level, same pixels.
  CHECK(render(hg::emoji::Look::Speaking, 10, 0) == render(hg::emoji::Look::Speaking, 10, 100));
  // The wave still moves frame to frame — that is the animation the artwork intends.
  CHECK(differences(render(hg::emoji::Look::Speaking, 0, 0), render(hg::emoji::Look::Speaking, 5, 0)) > 5);
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

// The neutral baseline below uses frame 0: the idle glance shifts the eyes sideways
// on other frames, so "this area is empty" only holds where the glance is centred.
TEST("emoji face: sleeping has U eyes and a T mouth, not bars") {
  // The U eyes are bottom-half arcs at design (72, 78) and (145, 78), r=24 w=8:
  // the ring reaches y 98..106 in design, which is canvas y 15+0.75*98 .. 15+0.75*106
  // = 88.5..94.5. The T mouth now sits at design 142 (the GIF's 9 px gap was a
  // quarter of the eyes' height on the 240 px glass; opened to 24).
  const Frame sleeping = render(hg::emoji::Look::Sleeping);
  // Ink at the U bottom (canvas x~84 = design x 72, y~94) and none where the old bar was.
  CHECK(pixel(sleeping, 84, 94) == kInk);
  CHECK(count_rect(sleeping, kInk, 57, 111, 63, 117) == 0);
  // The T mouth bar is design (85-150, 134), w=25. Between the two oval
  // eyes (canvas x gap 103-120) the bar is the only ink: canvas x 106-117,
  // y 112-122.
  CHECK(count_rect(sleeping, kInk, 106, 112, 117, 122) > 10);
  CHECK(count_rect(render(hg::emoji::Look::Neutral, 0), kInk, 106, 112, 117, 122) == 0);
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

TEST("emoji face: excited has star eyes and a grin arc, not a filled mouth") {
  // The grin arc is centred design (120, 152), radius ~40, bottom at design
  // y~192: canvas x 105-135, y 154-165 has ink on the arc.
  const Frame excited = render(hg::emoji::Look::Excited);
  CHECK(count_rect(excited, kInk, 105, 154, 136, 165) > 5);
  CHECK(count_rect(render(hg::emoji::Look::Neutral), kInk, 105, 154, 136, 165) == 0);
  // The grin is only an arc line: the interior just under the arc's centre
  // (design (120, 170), canvas (120, 142)) is background. Laughing's filled
  // half-ellipse mouth is ink there.
  CHECK(pixel(excited, 120, 142) == kBg);
  CHECK(pixel(render(hg::emoji::Look::Laughing), 120, 142) == kInk);
  // Excited differs from Laughing: same grin-sized mouth, different eyes.
  CHECK(differences(excited, render(hg::emoji::Look::Laughing)) > 40);
}

TEST("emoji face: bored has half-disc eyes and a flat mouth") {
  // The half-discs are cut at the flat top (design y=112 at frame 10, droop
  // 0): design (72, 95) is background for Bored but ink for Neutral's oval.
  const Frame bored = render(hg::emoji::Look::Bored);
  CHECK(pixel(bored, 84, 86) == kBg);
  CHECK(pixel(render(hg::emoji::Look::Neutral), 84, 86) == kInk);
  // The half-disc bottom is ink: design (72, 130), canvas (84, 112).
  CHECK(pixel(bored, 84, 112) == kInk);
  // The flat mouth line is design (98-142, 186): canvas x 103-136, y 154-155.
  CHECK(count_rect(bored, kInk, 103, 154, 137, 156) > 10);
  CHECK(count_rect(render(hg::emoji::Look::Neutral), kInk, 103, 154, 137, 156) == 0);
}

TEST("emoji face: cool has sunglasses over the eyes and a diamond pendant") {
  // The sunglasses top bar runs design x=42-182, y=68-78 (bob +/-4): at
  // design (44, 73), canvas (63, 70), the bar is ink for Cool at every
  // frame, but background for Neutral (left of the left oval eye).
  const Frame cool = render(hg::emoji::Look::Cool);
  CHECK(pixel(cool, 63, 70) == kInk);
  CHECK(pixel(render(hg::emoji::Look::Neutral), 63, 70) == kBg);
  // The diamond pendant sits below the eyes (design y=138-180): its lower
  // part (design x 105-120, y 165-178; canvas x 109-120, y 139-148) is ink
  // for Cool and background for Neutral, whose eyes end at design y=162.
  CHECK(count_rect(cool, kInk, 109, 139, 120, 148) > 20);
  CHECK(count_rect(render(hg::emoji::Look::Neutral), kInk, 109, 139, 120, 148) == 0);
}

TEST("emoji face: hi grows its eyes then flashes rays, with no text") {
  // The eyes grow to 1.25x at frame 2: design (72, 52) is inside the grown
  // left eye (canvas (84, 54)) but above Neutral's oval (top design y=62).
  const Frame hi_grown = render(hg::emoji::Look::Hi, 2);
  CHECK(pixel(hi_grown, 84, 54) == kInk);
  CHECK(pixel(render(hg::emoji::Look::Neutral), 84, 54) == kBg);
  // The radiating lines flank the eyes at frame 10 (first pulse peak): the
  // left middle ray runs design (40, 112) to (28, 112), canvas x 51-60,
  // y 99. Neutral has nothing there.
  const Frame hi_rays = render(hg::emoji::Look::Hi, 10);
  CHECK(count_rect(hi_rays, kInk, 51, 98, 61, 101) > 5);
  CHECK(count_rect(render(hg::emoji::Look::Neutral, 0), kInk, 51, 98, 61, 101) == 0);
  // No rays at frame 0 (the pulses are centred at f=10 and f=16).
  CHECK(count_rect(render(hg::emoji::Look::Hi, 0), kInk, 51, 98, 61, 101) == 0);
}

// Confused is gone: it is not in the Look enum any more (this file no longer
// references it, and the code compiles without it), and Silly covers it. The
// Phase 2 moods (Laughing, Loving, Winking, Angry, Shocked, Embarrassed,
// Silly) are now drawn instead of falling back to Neutral — the "differs from
// neutral" test above pins that. Surprised is gone too: shocked covers it.

// The face has to fill the glass, not sit in a corner of it: every look must cover a fair
// share of the 240 px frame and stay inside the panel. A mood drawn for a 100 px GIF and
// scaled without looking shows up here as a small box.
TEST("emoji face: every look fills the glass and stays inside it") {
  const hg::emoji::Look all[] = {
      hg::emoji::Look::Neutral, hg::emoji::Look::Happy, hg::emoji::Look::Sad,
      hg::emoji::Look::Thinking, hg::emoji::Look::Sleeping, hg::emoji::Look::Listening,
      hg::emoji::Look::Speaking, hg::emoji::Look::Silly, hg::emoji::Look::Laughing,
      hg::emoji::Look::Loving, hg::emoji::Look::Angry, hg::emoji::Look::Shocked,
      hg::emoji::Look::Embarrassed, hg::emoji::Look::Winking, hg::emoji::Look::Excited,
      hg::emoji::Look::Bored, hg::emoji::Look::Cool, hg::emoji::Look::Hi,
      hg::emoji::Look::Connecting, hg::emoji::Look::Disconnected};
  for (hg::emoji::Look look : all) {
    const Frame fb = render(look, 14);
    int x0 = kCanvas, x1 = -1, y0 = kCanvas, y1 = -1;
    for (int y = 0; y < kCanvas; ++y) {
      for (int x = 0; x < kCanvas; ++x) {
        const uint16_t px = fb[static_cast<size_t>(y) * kCanvas + x];
        if (px == kBg) continue;
        x0 = std::min(x0, x); x1 = std::max(x1, x);
        y0 = std::min(y0, y); y1 = std::max(y1, y);
      }
    }
    const int w = x1 - x0 + 1, h = y1 - y0 + 1;
    // Measured over the 20 looks (frame 14, the 150 px test box): w 76..143, h 33..114.
    // Floor of 70 and 30 keeps a look from collapsing into a few pixels — that is the
    // failure mode "the GIF was drawn for a 100 px sheet" produced.
    CHECK(w >= 70);
    CHECK(h >= 30);
    CHECK(x0 >= 0 && y0 >= 0 && x1 < kCanvas && y1 < kCanvas);
  }
}
