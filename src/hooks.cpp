#include "hooks.hpp"

#include "citylife.hpp"
#include "config.hpp"
#include "depth.hpp"
#include "exeid.hpp"
#include "fps.hpp"
#include "igct_fps.hpp"
#include "log.hpp"
#include "menu.hpp"
#include "osd.hpp"
#include "postfx.hpp"
#include "world.hpp"

#include <Windows.h>
#include <d3d9.h>

#include <atomic>
#include <mutex>
#include <string>

namespace sm3spectacular {
namespace {

HMODULE g_real = nullptr;
using Create9Fn = IDirect3D9*(WINAPI*)(UINT);
Create9Fn g_realCreate9 = nullptr;

using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND,
                                              const RGNDATA*);
using Reset_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);

Present_t g_origPresent = nullptr;
Reset_t g_origReset = nullptr;
CreateDevice_t g_origCreateDevice = nullptr;
std::atomic<bool> g_deviceHooked{false};
std::atomic<bool> g_patchesInstalled{false};
HANDLE g_restickThread = nullptr;
std::atomic<bool> g_restickRun{false};

DWORD WINAPI RestickThreadProc(LPVOID) {
    // ReShade-safe path: no Present vtable hook — restick CityLife/LOD/FOV on a timer.
    // Menu hooks normally install from Present #120; under UseReShade Present is never hooked.
    logf("restick thread started (UseReShade, no D3D vtable hooks)");
    Sleep(2000);
    if (exeIdentityOk()) {
        installMenuHooks();
        logf("Menu hooks installed via restick (UseReShade path)");
    }
    while (g_restickRun.load()) {
        citylifeOnPresent();
        worldOnPresent();
        Sleep(16);
    }
    return 0;
}

void startRestickThread() {
    if (g_restickThread) {
        return;
    }
    g_restickRun.store(true);
    g_restickThread = CreateThread(nullptr, 0, RestickThreadProc, nullptr, 0, nullptr);
}

void** vtableOf(void* obj) { return *reinterpret_cast<void***>(obj); }

bool patchVtableSlot(void** vtable, size_t index, void* detour, void** original) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    if (original && !*original) {
        *original = vtable[index];
    }
    vtable[index] = detour;
    VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &oldProtect);
    return true;
}

void forceImmediateIfUnlocked(D3DPRESENT_PARAMETERS* pp) {
    if (!pp) {
        return;
    }
    if (currentFpsLimit() != 30) {
        pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        if (!pp->Windowed) {
            pp->FullScreen_RefreshRateInHz = 0;
        }
    }
}

HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* self, const RECT* s, const RECT* d, HWND h,
                                      const RGNDATA* r) {
    citylifeOnPresent();
    worldOnPresent();
    postfxOnPresent(self);
    osdOnPresent(self);
    const HRESULT hr = g_origPresent(self, s, d, h, r);
    presentFrameLimit();
    static std::atomic<int> frames{0};
    const int n = frames.fetch_add(1) + 1;
    if (n == 120) {
        if (exeIdentityOk()) {
            installMenuHooks();
            logf("Menu hooks installed after boot Present #%d", n);
        } else {
            logf("Menu hooks skipped (exe identity mismatch)");
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp) {
    // Unbind depth/RT before freeing DEFAULT-pool resources (FSAA Apply / Reset safety).
    IDirect3DSurface9* bb = nullptr;
    if (SUCCEEDED(self->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
        self->SetRenderTarget(0, bb);
        bb->Release();
    }
    self->SetDepthStencilSurface(nullptr);

    postfxOnLostDevice();
    worldOnLostDevice();
    osdOnLostDevice();
    forceImmediateIfUnlocked(pp);
    depthPreparePresentParams(pp);
    worldPreparePresentParams(pp);
    const HRESULT hr = g_origReset(self, pp);
    logf("Reset hr=0x%08lX", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        postfxOnResetDevice(self);
        worldOnResetDevice(self);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus,
                                           DWORD behavior, D3DPRESENT_PARAMETERS* pp,
                                           IDirect3DDevice9** outDevice) {
    forceImmediateIfUnlocked(pp);
    depthPreparePresentParams(pp);
    worldPreparePresentParams(pp);
    const HRESULT hr = g_origCreateDevice(self, adapter, type, focus, behavior, pp, outDevice);
    logf("CreateDevice hr=0x%08lX device=%p interval=0x%08X refresh=%u windowed=%d",
         (unsigned long)hr, outDevice ? *outDevice : nullptr,
         pp ? pp->PresentationInterval : 0, pp ? pp->FullScreen_RefreshRateInHz : 0,
         pp ? (int)pp->Windowed : -1);
    if (SUCCEEDED(hr) && outDevice && *outDevice && !g_deviceHooked.load()) {
        void** vt = vtableOf(*outDevice);
        patchVtableSlot(vt, 16, reinterpret_cast<void*>(&HookReset),
                        reinterpret_cast<void**>(&g_origReset));
        if (patchVtableSlot(vt, 17, reinterpret_cast<void*>(&HookPresent),
                            reinterpret_cast<void**>(&g_origPresent))) {
            g_deviceHooked.store(true);
            postfxOnCreateDevice(*outDevice);
            worldOnCreateDevice(*outDevice);
            logf("Present+Reset hooked");
        }
    }
    return hr;
}

void hookCreateDevice(IDirect3D9* d3d) {
    if (!d3d) {
        return;
    }
    void** vt = vtableOf(d3d);
    if (!g_origCreateDevice) {
        patchVtableSlot(vt, 16, reinterpret_cast<void*>(&HookCreateDevice),
                        reinterpret_cast<void**>(&g_origCreateDevice));
        logf("CreateDevice hooked (first)");
    } else if (vt[16] != reinterpret_cast<void*>(&HookCreateDevice)) {
        void* ignored = nullptr;
        patchVtableSlot(vt, 16, reinterpret_cast<void*>(&HookCreateDevice), &ignored);
        logf("CreateDevice hooked (additional instance)");
    }
}

}  // namespace

bool initProxy() {
    const auto dir = moduleDirectory();
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&initProxy), &self);

    if (!config().chainDll.empty()) {
        const std::wstring chainPath = dir + L"\\" + config().chainDll;
        g_real = LoadLibraryW(chainPath.c_str());
        if (g_real && self && g_real == self) {
            logf("ChainDLL resolved to self — ignoring (would recurse)");
            FreeLibrary(g_real);
            g_real = nullptr;
        }
        if (!g_real && config().useReShade) {
            logf("WARNING: UseReShade=1 but failed to load %ls (err=%lu) — falling back to system d3d9",
                 config().chainDll.c_str(), GetLastError());
        }
    }
    if (!g_real) {
        wchar_t sys[MAX_PATH]{};
        GetSystemDirectoryW(sys, MAX_PATH);
        g_real = LoadLibraryW((std::wstring(sys) + L"\\d3d9.dll").c_str());
        logf("Loaded system d3d9");
    } else {
        logf("Loaded chain d3d9 (%ls) UseReShade=%d", config().chainDll.c_str(),
             (int)config().useReShade);
    }
    if (!g_real) {
        logf("FAILED to load real d3d9 err=%lu", GetLastError());
        return false;
    }
    g_realCreate9 = reinterpret_cast<Create9Fn>(
        reinterpret_cast<void*>(GetProcAddress(g_real, "Direct3DCreate9")));
    logf("Direct3DCreate9=%p", g_realCreate9);
    return g_realCreate9 != nullptr;
}

HMODULE realD3d9() { return g_real; }

void ensureInitialized() {
    static std::once_flag once;
    std::call_once(once, [] {
        loadConfig();
        logInit();
        logf("SM3 Spectacular Edition by zryuyu");
        ensureIgctFpsLabels();
        logf("ensureInitialized FpsLimit=%d Visuals=%d UseReShade=%d ChainDLL=%ls Strength=%.2f "
             "Preset=%d Fxaa=%d Bloom=%d AF=%d LOD=%d FOV=%d",
             config().fpsLimit, (int)config().visuals, (int)config().useReShade,
             config().chainDll.c_str(), config().visualStrength, (int)config().preset,
             (int)config().fxaa, (int)config().bloom,
             config().anisotropic ? config().anisotropy : 0, (int)config().lodBoost,
             config().fovDegrees);
        if (!initProxy()) {
            logf("initProxy failed");
            return;
        }
        logf("ensureInitialized OK");
    });
}

IDirect3D9* WINAPI ProxyDirect3DCreate9(UINT sdkVersion) {
    ensureInitialized();
    if (!g_realCreate9) {
        return nullptr;
    }
    logf("ProxyDirect3DCreate9 calling real Create9 sdk=%u UseReShade=%d", sdkVersion,
         (int)config().useReShade);
    IDirect3D9* d3d = g_realCreate9(sdkVersion);
    logf("ProxyDirect3DCreate9 sdk=%u real=%p", sdkVersion, d3d);
    if (d3d) {
        // Never vtable-hook ReShade's IDirect3D9/Device — that Access Violates.
        if (!config().useReShade) {
            hookCreateDevice(d3d);
        } else {
            logf("UseReShade=1 — skipping CreateDevice/Present vtable hooks");
            startRestickThread();
        }
        if (!g_patchesInstalled.exchange(true)) {
            if (exeIdentityOk()) {
                installFpsPatches();
                citylifeInstall();
            } else {
                logf("Skipping FPS/CityLife patches (exe identity mismatch)");
            }
        }
    }
    return d3d;
}

}  // namespace sm3spectacular
