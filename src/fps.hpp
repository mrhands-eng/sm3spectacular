#pragma once

namespace sm3spectacular {

void installFpsPatches();
void applyFpsLimit(int fpsLimit);
void presentFrameLimit();
// Hybrid companion: VA display/sim restick without Present sleep.
void fpsOnTick();
int currentFpsLimit();

}  // namespace sm3spectacular
