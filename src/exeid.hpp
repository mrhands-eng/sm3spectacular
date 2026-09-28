#pragma once

namespace sm3spectacular {

// Returns true if Game.exe matches the known Activision build used for VA patches.
// On mismatch, absolute memory hooks must not run.
bool exeIdentityOk();

}  // namespace sm3spectacular
