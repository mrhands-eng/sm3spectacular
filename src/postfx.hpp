#pragma once

#include <d3d9.h>

namespace sm3spectacular {

void postfxOnCreateDevice(IDirect3DDevice9* device);
void postfxOnLostDevice();
void postfxOnResetDevice(IDirect3DDevice9* device);
void postfxOnPresent(IDirect3DDevice9* device);

}  // namespace sm3spectacular
