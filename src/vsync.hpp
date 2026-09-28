#pragma once

namespace sm3spectacular {

// IAT-hook Direct3DCreate9 → wrap CreateDevice (VSync unlock, MSAA Off, AF).
// Also wraps device Reset (MSAA Off + AF rebind on Video Apply / alt-tab).
// Does NOT hook Present (ReShade owns the Present path).
void installVsyncUnlock();

// True after the game's first successful CreateDevice through our wrap.
bool vsyncCreateDeviceSeen();

}  // namespace sm3spectacular
