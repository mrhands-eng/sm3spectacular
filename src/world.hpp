#pragma once

#include <d3d9.h>

namespace sm3spectacular {

// Anisotropic filtering + LOD/FOV/shadows/fog/particles.
void worldOnCreateDevice(IDirect3DDevice9* device);
void worldOnPresent();
void worldOnLostDevice();
void worldOnResetDevice(IDirect3DDevice9* device);

// From CreateDevice PresentParams — drives aspect-correct FOV.
void worldSetDisplaySize(UINT width, UINT height);

// Optional: mutate PresentParams before CreateDevice (desktop/forced res).
void worldPreparePresentParams(D3DPRESENT_PARAMETERS* pp);

}  // namespace sm3spectacular
