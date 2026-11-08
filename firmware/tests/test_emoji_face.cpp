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
                                    hg::emoji::Look::Confused, hg::emoji::Look::Connecting,
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
  CHECK(count(render(hg::emoji::Look::Happy), pink) > 40);        // the heart
  CHECK(count(render(hg::emoji::Look::Sad), sky) > 30);           // the tear
  CHECK(count(render(hg::emoji::Look::Listening), wave) > 40);    // the three arcs
  CHECK(count(render(hg::emoji::Look::Speaking), wave) > 20);     // the waveform
  CHECK(count(render(hg::emoji::Look::Connecting), ring) > 100);  // the yellow ring
  CHECK(count(render(hg::emoji::Look::Sleeping), zz) > 40);       // the Zz
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
  // The Zz drifts.
  CHECK(differences(render(hg::emoji::Look::Sleeping, 5), render(hg::emoji::Look::Sleeping, 20)) > 5);
  // The connecting ring turns: a quarter turn is a different picture.
  CHECK(differences(render(hg::emoji::Look::Connecting, 0), render(hg::emoji::Look::Connecting, 5)) > 20);
  // A laugh has no blink and no drift: the same frame twice is the same picture.
  CHECK(render(hg::emoji::Look::Happy, 3) == render(hg::emoji::Look::Happy, 33));
}

TEST("emoji face: the speaking waveform is fixed in batch 1, whatever the level") {
  // Batch 2 lets the waveform follow the microphone level; batch 1 draws the
  // same squiggle at any level, so the look is already right on the device.
  CHECK(render(hg::emoji::Look::Speaking, 10, 0) == render(hg::emoji::Look::Speaking, 10, 100));
}

TEST("emoji face: the Phase 2 moods fall back to neutral until they have a trigger") {
  const Frame neutral = render(hg::emoji::Look::Neutral);
  CHECK(render(hg::emoji::Look::Laughing) == neutral);
  CHECK(render(hg::emoji::Look::Loving) == neutral);
  CHECK(render(hg::emoji::Look::Surprised) == neutral);
  CHECK(render(hg::emoji::Look::Winking) == neutral);
}
