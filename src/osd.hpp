#pragma once

#include <d3d9.h>

namespace sm3spectacular {

void osdOnPresent(IDirect3DDevice9* device);
void osdOnLostDevice();
void osdShutdown();

}  // namespace sm3spectacular
