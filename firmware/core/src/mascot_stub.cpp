// The mascot bitmaps, excluded from this build (see HG_MASCOT in
// firmware/core/CMakeLists.txt): the board draws its own face, so there is
// nothing to pick. pick() still returns a valid empty 64 px frame — callers
// such as hero_geom() dereference it — and anchors() is all zeros; the robot
// face paths never read them. The artwork stays in the repository
// (assets/mascot/) and the generator keeps working for the other boards.
#include "hg/mascot.hpp"

namespace hg::mascot {
namespace {

const uint8_t kEmpty64[512] = {};
const Bitmap kEmpty{64, Frame::Idle, kEmpty64};
const Anchors kAnchors{};

}  // namespace

const Bitmap* pick(int, Frame) { return &kEmpty; }
const Anchors& anchors() { return kAnchors; }

}  // namespace hg::mascot
