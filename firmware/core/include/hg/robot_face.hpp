// The robot face (小墨): the moji board's own face, drawn from primitives instead
// of the mascot bitmaps. Geometry is authored in the 240 px design frame the
// artwork was drawn in (see notes/设计-小墨表情.md) and scaled to whatever square
// the hero layout gives us.
#pragma once

#include <cstdint>

#include "hg/canvas.hpp"

namespace hg::robot {

// What the face shows. The app derives it from the screen and the emote, so one
// place decides what she looks like.
enum class Look : uint8_t { Neutral, Happy, Sad, Thinking, Sleeping, Listening, Speaking };

// How much bigger than the hero box the face is drawn: 1 = the design frame fits the
// box exactly, which leaves the head at 44% of it — too small on a 240 px panel.
float zoom();

// Draws the face centred in the square box (x, y, size). `frame` is the app's
// animation frame counter (100 ms per frame), `level` the microphone level 0..100
// for the speaking mouth, and bg the colour she sits on (a laugh's mouth is cut
// out of it). `antenna` paints the ball on her antenna: the board uses it as the
// link lamp, so its colour can differ from the white ink.
void draw(Canvas& c, int x, int y, int size, Look look, uint32_t frame, uint8_t level, uint16_t ink,
          uint16_t bg, uint16_t antenna);

}  // namespace hg::robot
