#pragma once

namespace sm3spectacular {

// Surgically insert BX_3DMENU_VIDEO_OPTIONS_FPS_* into every pcinterface\igct_*.bnx
// so Video FPS labels work on RU/EN/FR/… SKUs (each Game.exe hardcodes its own igct_XX).
void ensureIgctFpsLabels();

}  // namespace sm3spectacular
