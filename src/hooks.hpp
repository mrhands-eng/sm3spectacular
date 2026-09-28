#pragma once

#include <d3d9.h>

namespace sm3spectacular {

void ensureInitialized();
HMODULE realD3d9();
IDirect3D9* WINAPI ProxyDirect3DCreate9(UINT sdkVersion);

}  // namespace sm3spectacular
