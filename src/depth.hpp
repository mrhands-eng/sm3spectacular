#pragma once

#include <d3d9.h>

namespace sm3spectacular {

// Probe INTZ support + hook CreateDepthStencilSurface (vtable 29) to return a samplable
// INTZ/RAWZ texture surface. SetDepthStencilSurface is NOT hooked (unsafe on this title).
void depthOnDeviceHooked(IDirect3DDevice9* device);
void depthOnLostDevice();
void depthOnResetDevice(IDirect3DDevice9* device);

// Kept for hooks.cpp Reset/CreateDevice call sites — currently no-ops (safe).
void depthPreparePresentParams(D3DPRESENT_PARAMETERS* pp);
void depthCaptureReplace(IDirect3DDevice9* device);

IDirect3DTexture9* depthTexture();
bool depthAvailable();
UINT depthWidth();
UINT depthHeight();

void depthLinearParams(float out[4]);

}  // namespace sm3spectacular
