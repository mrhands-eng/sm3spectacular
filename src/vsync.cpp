#include "vsync.hpp"

#include "fps.hpp"
#include "log.hpp"
#include "world.hpp"

#include <Windows.h>
#include <d3d9.h>

#include <atomic>
#include <cstring>
#include <mutex>

namespace sm3spectacular {
namespace {

using Create9Fn = IDirect3D9*(WINAPI*)(UINT);
using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                   D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
using Reset_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

Create9Fn g_origCreate9 = nullptr;
CreateDevice_t g_origCreateDevice = nullptr;
Reset_t g_origReset = nullptr;
std::atomic<bool> g_installed{false};
std::atomic<IDirect3D9*> g_d3d{nullptr};
std::atomic<bool> g_createDeviceSeen{false};
std::atomic<bool> g_inCreateDevice{false};
std::mutex g_wrapMu;

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus,
                                           DWORD behavior, D3DPRESENT_PARAMETERS* pp,
                                           IDirect3DDevice9** outDevice);
HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp);

void** vtableOf(void* obj) { return *reinterpret_cast<void***>(obj); }

bool memoryReadable(const void* p, size_t bytes) {
    if (!p) {
        return false;
    }
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) {
        return false;
    }
    const DWORD prot = mbi.Protect & 0xff;
    if (prot == PAGE_NOACCESS || prot == PAGE_EXECUTE || (mbi.Protect & PAGE_GUARD)) {
        return false;
    }
    const auto* end = static_cast<const uint8_t*>(p) + bytes;
    const auto* regionEnd = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    return end <= regionEnd;
}

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

// Safe wrap — never touch a dangling IDirect3D9* (Steam/ReShade recreate objects).
bool wrapCreateDevice(IDirect3D9* d3d) {
    if (!d3d || g_inCreateDevice.load()) {
        return false;
    }
    if (!memoryReadable(d3d, sizeof(void*))) {
        return false;
    }
    void** vt = nullptr;
    {
        if (!memoryReadable(d3d, sizeof(void*))) {
            return false;
        }
        vt = vtableOf(d3d);
    }
    if (!memoryReadable(vt, sizeof(void*) * 17)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(g_wrapMu);
    if (g_inCreateDevice.load()) {
        return false;
    }
    if (!memoryReadable(d3d, sizeof(void*)) || !memoryReadable(vt, sizeof(void*) * 17)) {
        return false;
    }
    void* slot = vt[16];
    if (slot == reinterpret_cast<void*>(&HookCreateDevice)) {
        return true;
    }
    if (!slot) {
        return false;
    }

    if (!g_origCreateDevice) {
        g_origCreateDevice = reinterpret_cast<CreateDevice_t>(slot);
        logf("vsync: CreateDevice wrapped (interval+AF+MSAA)");
    } else if (slot != reinterpret_cast<void*>(g_origCreateDevice) &&
               slot != reinterpret_cast<void*>(&HookCreateDevice)) {
        g_origCreateDevice = reinterpret_cast<CreateDevice_t>(slot);
        logf("vsync: CreateDevice re-wrapped over ReShade");
    }
    return patchVtableSlot(vt, 16, reinterpret_cast<void*>(&HookCreateDevice), nullptr);
}

// Hybrid-safe Reset: mutate PresentParams before ReShade's Reset (outermost on device).
bool wrapDeviceReset(IDirect3DDevice9* device) {
    if (!device || !memoryReadable(device, sizeof(void*))) {
        return false;
    }
    void** vt = vtableOf(device);
    if (!memoryReadable(vt, sizeof(void*) * 17)) {
        return false;
    }
    void* slot = vt[16];
    if (slot == reinterpret_cast<void*>(&HookReset)) {
        return true;
    }
    if (!slot) {
        return false;
    }
    if (!g_origReset) {
        g_origReset = reinterpret_cast<Reset_t>(slot);
        logf("vsync: Reset wrapped (MSAA Off + AF rebind on Apply/alt-tab)");
    } else if (slot != reinterpret_cast<void*>(g_origReset) &&
               slot != reinterpret_cast<void*>(&HookReset)) {
        g_origReset = reinterpret_cast<Reset_t>(slot);
        logf("vsync: Reset re-wrapped over ReShade");
    }
    return patchVtableSlot(vt, 16, reinterpret_cast<void*>(&HookReset), nullptr);
}

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* self, D3DPRESENT_PARAMETERS* pp) {
    Reset_t orig = g_origReset;
    if (!orig || orig == reinterpret_cast<Reset_t>(&HookReset)) {
        logf("vsync: Reset aborted — bad orig=%p", reinterpret_cast<void*>(orig));
        return D3DERR_INVALIDCALL;
    }
    worldOnLostDevice();
    worldPreparePresentParams(pp);
    forceImmediateIfUnlocked(pp);
    const HRESULT hr = orig(self, pp);
    logf("vsync: Reset hr=0x%08lX msaa=%u windowed=%d", (unsigned long)hr,
         pp ? static_cast<unsigned>(pp->MultiSampleType) : 0u, pp ? (int)pp->Windowed : -1);
    if (SUCCEEDED(hr)) {
        worldOnResetDevice(self);
        wrapDeviceReset(self);  // re-assert outermost if ReShade shuffled vtable
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirect3D9* self, UINT adapter, D3DDEVTYPE type, HWND focus,
                                           DWORD behavior, D3DPRESENT_PARAMETERS* pp,
                                           IDirect3DDevice9** outDevice) {
    CreateDevice_t orig = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_wrapMu);
        orig = g_origCreateDevice;
        if (!orig || orig == reinterpret_cast<CreateDevice_t>(&HookCreateDevice)) {
            logf("vsync: CreateDevice aborted — bad orig=%p", reinterpret_cast<void*>(orig));
            return D3DERR_INVALIDCALL;
        }
        g_inCreateDevice.store(true);
    }

    worldPreparePresentParams(pp);
    forceImmediateIfUnlocked(pp);

    const HRESULT hr = orig(self, adapter, type, focus, behavior, pp, outDevice);

    g_inCreateDevice.store(false);
    g_createDeviceSeen.store(true);

    logf("vsync: CreateDevice hr=0x%08lX interval=0x%08X size=%ux%u windowed=%d",
         (unsigned long)hr, pp ? pp->PresentationInterval : 0, pp ? pp->BackBufferWidth : 0,
         pp ? pp->BackBufferHeight : 0, pp ? (int)pp->Windowed : -1);
    if (pp && pp->BackBufferWidth && pp->BackBufferHeight) {
        worldSetDisplaySize(pp->BackBufferWidth, pp->BackBufferHeight);
    }
    if (SUCCEEDED(hr) && outDevice && *outDevice) {
        worldOnCreateDevice(*outDevice);
        wrapDeviceReset(*outDevice);
    }
    return hr;
}

IDirect3D9* WINAPI HookDirect3DCreate9(UINT sdkVersion) {
    IDirect3D9* d3d = g_origCreate9 ? g_origCreate9(sdkVersion) : nullptr;
    if (d3d) {
        g_d3d.store(d3d);
        wrapCreateDevice(d3d);
    }
    return d3d;
}

bool patchIatSlot(void** slot, void* detour, void** outOriginal) {
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    *outOriginal = *slot;
    *slot = detour;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return *outOriginal != nullptr;
}

bool patchIatByProcAddress(void* detour, void** outOriginal) {
    HMODULE d3d = GetModuleHandleW(L"d3d9.dll");
    if (!d3d) {
        return false;
    }
    void* create9 = reinterpret_cast<void*>(GetProcAddress(d3d, "Direct3DCreate9"));
    if (!create9) {
        return false;
    }

    HMODULE exe = GetModuleHandleW(nullptr);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(exe) + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        return false;
    }
    auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(reinterpret_cast<uint8_t*>(exe) +
                                                           dir.VirtualAddress);
    for (; imp->Name; ++imp) {
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(reinterpret_cast<uint8_t*>(exe) +
                                                        imp->FirstThunk);
        for (; iat->u1.Function; ++iat) {
            if (reinterpret_cast<void*>(iat->u1.Function) == create9) {
                return patchIatSlot(reinterpret_cast<void**>(&iat->u1.Function), detour, outOriginal);
            }
        }
    }
    return false;
}

bool patchIatByName(void* detour, void** outOriginal) {
    HMODULE exe = GetModuleHandleW(nullptr);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(exe);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<uint8_t*>(exe) + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) {
        return false;
    }
    auto* base = reinterpret_cast<uint8_t*>(exe);
    auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; imp->Name; ++imp) {
        const char* dllName = reinterpret_cast<const char*>(base + imp->Name);
        if (_stricmp(dllName, "d3d9.dll") != 0) {
            continue;
        }
        const DWORD oft = imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk;
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + oft);
        auto* iat = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; thunk->u1.AddressOfData; ++thunk, ++iat) {
            if (IMAGE_SNAP_BY_ORDINAL32(thunk->u1.Ordinal)) {
                continue;
            }
            auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + thunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(ibn->Name), "Direct3DCreate9") != 0) {
                continue;
            }
            return patchIatSlot(reinterpret_cast<void**>(&iat->u1.Function), detour, outOriginal);
        }
    }
    return false;
}

}  // namespace

bool vsyncCreateDeviceSeen() { return g_createDeviceSeen.load(); }

void installVsyncUnlock() {
    if (g_installed.load()) {
        // Re-assert outermost CreateDevice until the game succeeds once.
        // Skip while CreateDevice is running — that race was crashing in wrapCreateDevice.
        if (!g_createDeviceSeen.load() && !g_inCreateDevice.load()) {
            wrapCreateDevice(g_d3d.load());
        }
        return;
    }
    void* orig = nullptr;
    bool ok = patchIatByProcAddress(reinterpret_cast<void*>(&HookDirect3DCreate9), &orig);
    if (!ok) {
        ok = patchIatByName(reinterpret_cast<void*>(&HookDirect3DCreate9), &orig);
    }
    if (!ok || !orig) {
        return;
    }
    if (g_installed.exchange(true)) {
        return;
    }
    g_origCreate9 = reinterpret_cast<Create9Fn>(orig);
    logf("vsync: IAT Direct3DCreate9 hooked");
}

}  // namespace sm3spectacular
