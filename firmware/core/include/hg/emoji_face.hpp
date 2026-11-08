// The pure-emoji face (小墨, feat/moji-emoji-only): Otto-style eyes-only faces,
// drawn from primitives in the same 240 px design frame as the robot face.
// No head, no antenna, no text — the link state is a face, not a lamp.
// See notes/设计-小墨纯表情.md (emote-pure-ui-v8-oval.png).
#pragma once

#include <cstdint>

#include "hg/canvas.hpp"

namespace hg::emoji {

// What the face shows. The app derives it from the link, the emote and the
// screen, so one place decides what she looks like. The four Phase 2 moods
// (Laughing, Loving, Surprised, Winking) need host mood and fall back to
// Neutral until then.
enum class Look : uint8_t {
  Neutral,
  Happy,
  Sad,
  Thinking,
  Sleeping,
  Listening,
  Speaking,
  Confused,
  Laughing,
  Loving,
  Surprised,
  Winking,
  Connecting,
  Disconnected
};

// How much bigger than the hero box the face is drawn. The emoji artwork fills
// most of its 240 frame (unlike the robot head, which is 44% of it), so the
// zoom stays near 1.
float zoom();

// Draws the face centred in the square box (x, y, size). `frame` is the app's
// animation frame counter (100 ms per frame), `level` the microphone level 0..100
// (batch 2: the speaking waveform follows it; batch 1 draws it fixed), and bg
// the colour she sits on (a sad eye is cut out of it).
void draw(Canvas& c, int x, int y, int size, Look look, uint32_t frame, uint8_t level, uint16_t ink,
          uint16_t bg);

}  // namespace hg::emoji
