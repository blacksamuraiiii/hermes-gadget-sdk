#pragma once

#include <cstddef>
#include <cstdint>

// Wake-word engine seam.
//
// The engine lives in the port layer, because it needs a DSP library only some
// boards have and only those boards can afford the RAM. Core needs two things
// from it: it must get out of the way while the user talks, and it must be able
// to take the microphone samples while the device is idle. Everything here has a
// weak default that does nothing, so a port without an engine behaves exactly as
// before and no board has to opt in to stay correct.
namespace hg {

// True when the port linked an engine. Core asks before handing over the mic.
bool wake_available();

// Stop detection: a talk session is about to open the microphone at its own rate.
void wake_pause();

// Give the microphone back once the talk session closed.
void wake_resume();

// Mono PCM16 samples at the engine's own rate. Ignored unless the engine is
// running; the port decides the rate it can work with.
void wake_feed(const int16_t* samples, size_t count);

}  // namespace hg
