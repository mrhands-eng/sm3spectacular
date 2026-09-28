#pragma once

namespace sm3spectacular {

// True if launched under Steam (overlay / steamclient injected).
bool steamDetected();

// Call early (after logInit): if Steam is present, switch ReShade to a
// depth-free preset so MXAO/SSR don't paint the screen red, and mark
// windowed-prefer mode for CreateDevice.
void applySteamCompat();

// Prefer windowed CreateDevice under Steam (exclusive FS + overlay breaks depth).
bool steamPreferWindowed();

}  // namespace sm3spectacular
