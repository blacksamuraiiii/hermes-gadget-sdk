#include "hg/wake.hpp"

// Weak defaults for hg::wake_*. A port that links a real engine (see
// firmware/esp32/main/port_wake.cpp) overrides them; every other port keeps
// these, so nobody has to know the seam exists.
namespace hg {

__attribute__((weak)) bool wake_available() { return false; }
__attribute__((weak)) void wake_pause() {}
__attribute__((weak)) void wake_resume() {}
__attribute__((weak)) void wake_feed(const int16_t*, size_t) {}

}  // namespace hg
