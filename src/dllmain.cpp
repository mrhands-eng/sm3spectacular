// SM3 Spectacular Edition — d3d9 proxy remaster for Spider-Man 3 (PC)
// Author: zryuyu
#include "hooks.hpp"

#include <Windows.h>
#include <d3d9.h>

namespace {

FARPROC getRealProc(const char* name) {
    HMODULE real = sm3spectacular::realD3d9();
    if (!real) {
        sm3spectacular::ensureInitialized();
        real = sm3spectacular::realD3d9();
    }
    if (!real) {
        return nullptr;
    }
    return GetProcAddress(real, name);
}

}  // namespace

extern "C" {

IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion) {
    sm3spectacular::ensureInitialized();
    return sm3spectacular::ProxyDirect3DCreate9(SDKVersion);
}

HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** pp) {
    sm3spectacular::ensureInitialized();
    using Fn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    auto fn = reinterpret_cast<Fn>(getRealProc("Direct3DCreate9Ex"));
    if (!fn) {
        return E_NOTIMPL;
    }
    return fn(SDKVersion, pp);
}

int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR name) {
    using Fn = int(WINAPI*)(D3DCOLOR, LPCWSTR);
    auto fn = reinterpret_cast<Fn>(getRealProc("D3DPERF_BeginEvent"));
    return fn ? fn(col, name) : 0;
}

int WINAPI D3DPERF_EndEvent() {
    using Fn = int(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(getRealProc("D3DPERF_EndEvent"));
    return fn ? fn() : 0;
}

DWORD WINAPI D3DPERF_GetStatus() {
    using Fn = DWORD(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(getRealProc("D3DPERF_GetStatus"));
    return fn ? fn() : 0;
}

BOOL WINAPI D3DPERF_QueryRepeatFrame() {
    using Fn = BOOL(WINAPI*)();
    auto fn = reinterpret_cast<Fn>(getRealProc("D3DPERF_QueryRepeatFrame"));
    return fn ? fn() : FALSE;
}

void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR name) {
    using Fn = void(WINAPI*)(D3DCOLOR, LPCWSTR);
    auto fn = reinterpret_cast<Fn>(getRealProc("D3DPERF_SetMarker"));
    if (fn) {
        fn(col, name);
    }
}

void WINAPI D3DPERF_SetOptions(DWORD options) {
    using Fn = void(WINAPI*)(DWORD);
    auto fn = reinterpret_cast<Fn>(getRealProc("D3DPERF_SetOptions"));
    if (fn) {
        fn(options);
    }
}

void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR name) {
    using Fn = void(WINAPI*)(D3DCOLOR, LPCWSTR);
    auto fn = reinterpret_cast<Fn>(getRealProc("D3DPERF_SetRegion"));
    if (fn) {
        fn(col, name);
    }
}

void WINAPI DebugSetLevel(DWORD level) {
    using Fn = void(WINAPI*)(DWORD);
    auto fn = reinterpret_cast<Fn>(getRealProc("DebugSetLevel"));
    if (fn) {
        fn(level);
    }
}

void WINAPI DebugSetMute(DWORD mute) {
    using Fn = void(WINAPI*)(DWORD);
    auto fn = reinterpret_cast<Fn>(getRealProc("DebugSetMute"));
    if (fn) {
        fn(mute);
    }
}

HRESULT WINAPI Direct3DShaderValidatorCreate9(void) {
    using Fn = HRESULT(WINAPI*)(void);
    auto fn = reinterpret_cast<Fn>(getRealProc("Direct3DShaderValidatorCreate9"));
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI PSGPError(void) {
    using Fn = HRESULT(WINAPI*)(void);
    auto fn = reinterpret_cast<Fn>(getRealProc("PSGPError"));
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI PSGPSampleTexture(void) {
    using Fn = HRESULT(WINAPI*)(void);
    auto fn = reinterpret_cast<Fn>(getRealProc("PSGPSampleTexture"));
    return fn ? fn() : E_FAIL;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

}  // extern "C"
