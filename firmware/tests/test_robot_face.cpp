// The robot face: the moji board's own artwork, drawn from primitives. These tests
// pin what each mood puts on screen, because a face that silently stops drawing
// (an empty arc, a colour that never lands) is the failure that reaches the board.
#include <cstdint>
#include <vector>

#include "check.hpp"
#include "hg/canvas.hpp"
#include "hg/robot_face.hpp"

namespace {

constexpr int kCanvas = 240;  // a moji-sized panel
constexpr int kBox = 150;     // the hero box it hands the face (the panel's usable square)
constexpr int kBoxX = 45, kBoxY = 30;
constexpr uint16_t kInk = hg::rgb565(232, 238, 242);
constexpr uint16_t kBg = hg::rgb565(10, 14, 20);

using Frame = std::vector<uint16_t>;

Frame render(hg::robot::Look look, uint32_t frame = 10, uint8_t level = 0) {
  Frame fb(static_cast<size_t>(kCanvas) * kCanvas, kBg);
  hg::Canvas c(fb.data(), kCanvas, kCanvas, false);
  hg::robot::draw(c, kBoxX, kBoxY, kBox, look, frame, level, kInk, kBg);
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

TEST("robot face: every mood draws a face, and each one differs from neutral") {
  const Frame neutral = render(hg::robot::Look::Neutral);
  CHECK(count(neutral, kInk) > 500);  // the shell alone is a few hundred pixels
  const hg::robot::Look others[] = {hg::robot::Look::Happy, hg::robot::Look::Sad,
                                    hg::robot::Look::Thinking, hg::robot::Look::Sleeping,
                                    hg::robot::Look::Listening};
  for (hg::robot::Look look : others) {
    const Frame fb = render(look);
    CHECK(differences(neutral, fb) > 40);  // a mood that changes fewer pixels is invisible
  }
}

TEST("robot face: the moods carry their own colours") {
  const auto pink = hg::rgb565(247, 117, 146);
  const auto sky = hg::rgb565(120, 200, 255);
  const auto zz = hg::rgb565(170, 170, 190);
  CHECK(count(render(hg::robot::Look::Happy), pink) > 40);      // the heart
  CHECK(count(render(hg::robot::Look::Sad), sky) > 30);         // the tear
  CHECK(count(render(hg::robot::Look::Listening), sky) > 40);   // the waves (one lit at a time)
  CHECK(count(render(hg::robot::Look::Sleeping), zz) > 40);     // the Zz
  CHECK(count(render(hg::robot::Look::Neutral), pink) == 0);    // and nowhere else
  CHECK(count(render(hg::robot::Look::Neutral), sky) == 0);
}

TEST("robot face: the same state draws the same pixels") {
  CHECK(render(hg::robot::Look::Thinking, 21) == render(hg::robot::Look::Thinking, 21));
  CHECK(render(hg::robot::Look::Neutral, 10, 40) == render(hg::robot::Look::Neutral, 10, 40));
}

TEST("robot face: the animated parts move, the still ones do not") {
  // A blink is two frames of forty-five, so the faces with open eyes change.
  CHECK(differences(render(hg::robot::Look::Neutral, 10), render(hg::robot::Look::Neutral, 0)) > 20);
  // Waves light one after another.
  CHECK(differences(render(hg::robot::Look::Listening, 3), render(hg::robot::Look::Listening, 6)) > 10);
  // The thinking dots take turns.
  CHECK(differences(render(hg::robot::Look::Thinking, 3), render(hg::robot::Look::Thinking, 12)) > 5);
  // The Zz drifts.
  CHECK(differences(render(hg::robot::Look::Sleeping, 5), render(hg::robot::Look::Sleeping, 20)) > 5);
  // A laugh has no blink and no drift: the same frame twice is the same picture.
  CHECK(render(hg::robot::Look::Happy, 3) == render(hg::robot::Look::Happy, 33));
}

TEST("robot face: the speaking mouth follows the microphone level") {
  const Frame quiet = render(hg::robot::Look::Speaking, 10, 0);
  const Frame loud = render(hg::robot::Look::Speaking, 10, 100);
  CHECK(differences(quiet, loud) > 10);
}
