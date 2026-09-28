#include "world.hpp"

#include "config.hpp"
#include "exeid.hpp"
#include "log.hpp"
#include "steam_compat.hpp"

#include <Windows.h>
#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

#ifndef _ReturnAddress
#define _ReturnAddress() __builtin_return_address(0)
#endif

namespace sm3spectacular {
namespace {

// Building LOD / city detail + shadow range (GRAPHOPTS_*).
constexpr uintptr_t kHighLodEnabled = 0x00D0C858;     // byte
constexpr uintptr_t kHighLodDistance = 0x00D0C85C;    // float vanilla ~10
constexpr uintptr_t kMediumLodDistance = 0x00D0C860;  // float vanilla ~30
constexpr uintptr_t kInteriorDistance = 0x00D0C864;   // float vanilla ~0.5
constexpr uintptr_t kDistrictLodCull = 0x00D0C820;    // float vanilla ~5
constexpr uintptr_t kRenderLowLods = 0x00D0C680;      // byte
constexpr uintptr_t kRenderShadows = 0x00D0C681;      // byte
constexpr uintptr_t kCityDetail = 0x00DE30E4;         // int
constexpr uintptr_t kCdFarA = 0x00CF502C;
constexpr uintptr_t kCdNear = 0x00CF5030;
constexpr uintptr_t kCdFarB = 0x00CF5034;
constexpr uintptr_t kCdBudgetA = 0x00CF5038;
constexpr uintptr_t kCdBudgetB = 0x00CF503C;
constexpr uintptr_t kCdScale = 0x00CF5040;
constexpr uintptr_t kCdBudgetC = 0x00CF5044;
constexpr uintptr_t kCdAltNear = 0x00CF550C;
constexpr uintptr_t kCdAltFar = 0x00CF5510;

// Shadow distance knobs (vanilla: scale=50, smallest=20).
constexpr uintptr_t kShadowsSmallestDim = 0x00D0C84C;     // float
constexpr uintptr_t kBuildingsShadowScale = 0x00D0C870;   // float

// Atmosphere / particles
constexpr uintptr_t kFogDepth = 0x00D0C874;         // byte
constexpr uintptr_t kFogVolumetric = 0x00D0C875;    // byte
constexpr uintptr_t kParticleScale = 0x00D0C88C;    // float vanilla 2400

constexpr uintptr_t kFieldOfView = 0x00D0C6D0;  // int degrees, vanilla 67
constexpr uintptr_t kDynamicFov = 0x00D0C6C8;   // byte

using SetSamplerState_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, D3DSAMPLERSTATETYPE,
                                                      DWORD);
using SetTexture_t = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);

SetSamplerState_t g_origSetSamp = nullptr;
SetTexture_t g_origSetTex = nullptr;
bool g_hooked = false;
IDirect3DDevice9* g_afDevice = nullptr;
unsigned g_presents = 0;
std::atomic<float> g_aspect{16.f / 9.f};

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

bool writeMem(void* addr, const void* src, size_t n) {
    DWORD old = 0;
    if (!VirtualProtect(addr, n, PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    std::memcpy(addr, src, n);
    VirtualProtect(addr, n, old, &old);
    return true;
}

void writeByte(uintptr_t va, uint8_t v) { writeMem(reinterpret_cast<void*>(va), &v, 1); }
void writeInt(uintptr_t va, int v) { writeMem(reinterpret_cast<void*>(va), &v, sizeof(v)); }
void writeFloat(uintptr_t va, float v) { writeMem(reinterpret_cast<void*>(va), &v, sizeof(v)); }

DWORD mipBiasAsDword() {
    float bias = config().textureLodBias;
    DWORD bits = 0;
    std::memcpy(&bits, &bias, sizeof(bits));
    return bits;
}

void applyMipBias(IDirect3DDevice9* self, DWORD stage) {
    if (!g_origSetSamp || stage > 7) {
        return;
    }
    g_origSetSamp(self, stage, D3DSAMP_MIPMAPLODBIAS, mipBiasAsDword());
}

void applyAniso(IDirect3DDevice9* self, DWORD stage) {
    if (!g_origSetSamp || stage > 7) {
        return;
    }
    const DWORD aniso = static_cast<DWORD>((std::max)(1, (std::min)(16, config().anisotropy)));
    g_origSetSamp(self, stage, D3DSAMP_MAXANISOTROPY, aniso);
    g_origSetSamp(self, stage, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC);
    g_origSetSamp(self, stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    g_origSetSamp(self, stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
    applyMipBias(self, stage);
}

// ReShade (d3d9.dll) and our own DLL also call SetSamplerState — do not force AF/mip
// on those callers or effect passes get anisotropic instead of intended POINT/LINEAR.
// `ra` must be the hook's _ReturnAddress() (caller of the hook), not a helper's.
bool moduleIsReShadeOrCompanion(void* ra) {
    if (!ra) {
        return false;
    }
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(ra), &mod) ||
        !mod) {
        return false;
    }
    static HMODULE d3d = nullptr;
    static HMODULE self = nullptr;
    if (!d3d) {
        d3d = GetModuleHandleW(L"d3d9.dll");
    }
    if (!self) {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&moduleIsReShadeOrCompanion), &self);
    }
    return (d3d && mod == d3d) || (self && mod == self);
}

HRESULT STDMETHODCALLTYPE HookSetSamplerState(IDirect3DDevice9* self, DWORD stage,
                                              D3DSAMPLERSTATETYPE type, DWORD value) {
    if (stage > 7 || moduleIsReShadeOrCompanion(_ReturnAddress())) {
        return g_origSetSamp(self, stage, type, value);
    }
    // Always force configured mip bias (keeps hi-mips longer when negative).
    if (type == D3DSAMP_MIPMAPLODBIAS) {
        return g_origSetSamp(self, stage, type, mipBiasAsDword());
    }
    if (!config().anisotropic) {
        return g_origSetSamp(self, stage, type, value);
    }
    if (type == D3DSAMP_MINFILTER &&
        (value == D3DTEXF_LINEAR || value == D3DTEXF_ANISOTROPIC || value == D3DTEXF_POINT)) {
        applyAniso(self, stage);
        return D3D_OK;
    }
    if (type == D3DSAMP_MAXANISOTROPY) {
        const DWORD aniso = static_cast<DWORD>((std::max)(1, (std::min)(16, config().anisotropy)));
        if (value < aniso) {
            value = aniso;
        }
    }
    return g_origSetSamp(self, stage, type, value);
}

HRESULT STDMETHODCALLTYPE HookSetTexture(IDirect3DDevice9* self, DWORD stage,
                                         IDirect3DBaseTexture9* tex) {
    const HRESULT hr = g_origSetTex(self, stage, tex);
    if (SUCCEEDED(hr) && tex && stage < 8 && !moduleIsReShadeOrCompanion(_ReturnAddress())) {
        IDirect3DTexture9* t2d = nullptr;
        if (SUCCEEDED(tex->QueryInterface(IID_IDirect3DTexture9, reinterpret_cast<void**>(&t2d))) &&
            t2d) {
            if (t2d->GetLevelCount() > 1) {
                if (config().anisotropic) {
                    applyAniso(self, stage);
                } else {
                    applyMipBias(self, stage);
                }
            }
            t2d->Release();
        }
    }
    return hr;
}

void applyShadows() {
    // Longer / denser building shadows — big remaster cue without new assets.
    writeByte(kRenderShadows, 1);
    writeFloat(kBuildingsShadowScale, 85.f);  // vanilla 50
    writeFloat(kShadowsSmallestDim, 42.f);    // vanilla 20
}

void applyAtmosphere() {
    const auto& c = config();
    writeByte(kFogDepth, c.fogDepth ? 1 : 0);
    writeByte(kFogVolumetric, c.fogVolumetric ? 1 : 0);
    writeFloat(kParticleScale, c.particleScale);
}

void applyLod() {
    const auto& c = config();
    applyShadows();
    applyAtmosphere();
    if (!c.lodBoost) {
        return;
    }
    writeByte(kHighLodEnabled, 1);
    writeByte(kRenderLowLods, 0);
    writeFloat(kHighLodDistance, c.lodHighDistance);
    writeFloat(kMediumLodDistance, c.lodMediumDistance);
    writeFloat(kInteriorDistance, c.lodInteriorDistance);
    writeFloat(kDistrictLodCull, c.lodDistrictCull);
    writeInt(kCityDetail, c.cityDetail);

    const float farD = c.lodCityFar;
    const float nearD = c.lodCityNear;
    const float scale = 1.f;
    const int budget = c.lodCityBudget;
    const int budgetC = c.lodCityBudgetC;
    writeFloat(kCdFarA, farD);
    writeFloat(kCdNear, nearD);
    writeFloat(kCdFarB, farD);
    writeInt(kCdBudgetA, budget);
    writeInt(kCdBudgetB, budget);
    writeFloat(kCdScale, scale);
    writeInt(kCdBudgetC, budgetC);
    writeFloat(kCdAltNear, nearD);
    writeFloat(kCdAltFar, farD);
}

int effectiveFovDegrees() {
    const auto& c = config();
    int fov = c.fovDegrees;
    if (c.aspectFovCorrect) {
        const float aspect = g_aspect.load();
        if (aspect > 1.7f) {
            // Scale from 16:9 so ultrawide doesn't crop horizontally.
            const float scale = aspect / (16.f / 9.f);
            fov = static_cast<int>(std::lround(static_cast<float>(fov) * scale));
            fov = (std::max)(50, (std::min)(110, fov));
        }
    }
    return fov;
}

void applyFov() {
    const int fov = effectiveFovDegrees();
    if (fov >= 50 && fov <= 110) {
        writeByte(kDynamicFov, 1);
        writeInt(kFieldOfView, fov);
    }
}

void applyLodAndFov() {
    applyLod();
    applyFov();
}

}  // namespace

void worldSetDisplaySize(UINT width, UINT height) {
    if (width > 0 && height > 0) {
        g_aspect.store(static_cast<float>(width) / static_cast<float>(height));
    }
}

void worldPreparePresentParams(D3DPRESENT_PARAMETERS* pp) {
    if (!pp) {
        return;
    }
    // MSAA + ReShade depth (MXAO/SSR) is unstable on modern GPUs — CreateDevice
    // can fail intermittently (user has to relaunch several times). Force Off.
    if (pp->MultiSampleType != D3DMULTISAMPLE_NONE) {
        logf("world: forcing MSAA Off (was MultiSampleType=%u)",
             static_cast<unsigned>(pp->MultiSampleType));
        pp->MultiSampleType = D3DMULTISAMPLE_NONE;
        pp->MultiSampleQuality = 0;
    }
    if (steamPreferWindowed() && !pp->Windowed) {
        pp->Windowed = TRUE;
        pp->FullScreen_RefreshRateInHz = 0;
        logf("world: Steam compat — forcing windowed (exclusive FS breaks depth/overlay)");
    }
    const auto& c = config();
    UINT w = static_cast<UINT>(c.displayWidth);
    UINT h = static_cast<UINT>(c.displayHeight);
    if (c.desktopResolution) {
        w = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
        h = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));
    }
    if (w >= 640 && h >= 480) {
        pp->BackBufferWidth = w;
        pp->BackBufferHeight = h;
        logf("world: forcing resolution %ux%u", w, h);
    }
    if (pp->BackBufferWidth > 0 && pp->BackBufferHeight > 0) {
        worldSetDisplaySize(pp->BackBufferWidth, pp->BackBufferHeight);
    }
}

void worldOnCreateDevice(IDirect3DDevice9* device) {
    if (!device) {
        return;
    }
    // Device recreate (alt-tab / Reset failure / new CreateDevice) needs a fresh vtable patch.
    if (g_hooked && g_afDevice == device) {
        if (exeIdentityOk()) {
            applyLodAndFov();
        }
        return;
    }
    g_afDevice = device;
    g_hooked = false;
    void** vt = vtableOf(device);
    const bool okSamp = patchVtableSlot(vt, 69, reinterpret_cast<void*>(&HookSetSamplerState),
                                        reinterpret_cast<void**>(&g_origSetSamp));
    const bool okTex = patchVtableSlot(vt, 65, reinterpret_cast<void*>(&HookSetTexture),
                                       reinterpret_cast<void**>(&g_origSetTex));
    g_hooked = okSamp && okTex;
    if (exeIdentityOk()) {
        applyLodAndFov();
    }
    logf("world: AF hooks samp=%d tex=%d aniso=%d mipBias=%.2f lod=%d fov=%d fogVol=%d particles=%.0f",
         (int)okSamp, (int)okTex, config().anisotropic ? config().anisotropy : 0,
         config().textureLodBias, exeIdentityOk() && config().lodBoost ? 1 : 0,
         exeIdentityOk() ? effectiveFovDegrees() : 0, (int)config().fogVolumetric,
         config().particleScale);
}

void worldOnPresent() {
    if (!exeIdentityOk()) {
        return;
    }
    ++g_presents;
    // LOD/atmosphere restick on a slow timer. FOV only if snapped back to
    // vanilla 67 — leave cinematic / mission FOV alone.
    if ((g_presents % 90) == 1) {
        applyLod();
        const int want = effectiveFovDegrees();
        if (want >= 50 && want <= 110) {
            const int cur = *reinterpret_cast<const int*>(kFieldOfView);
            if (cur == 67) {
                applyFov();
            }
        }
    }
}

void worldOnLostDevice() {
    // Keep g_orig* — same vtable slots usually survive Reset; clear device identity
    // so a brand-new CreateDevice rebinds AF.
    g_afDevice = nullptr;
    g_hooked = false;
}

void worldOnResetDevice(IDirect3DDevice9* device) {
    if (device) {
        g_afDevice = device;
        // Same device after Reset — re-assert sampler/texture hooks if needed.
        if (!g_hooked) {
            void** vt = vtableOf(device);
            const bool okSamp = patchVtableSlot(vt, 69, reinterpret_cast<void*>(&HookSetSamplerState),
                                                reinterpret_cast<void**>(&g_origSetSamp));
            const bool okTex = patchVtableSlot(vt, 65, reinterpret_cast<void*>(&HookSetTexture),
                                               reinterpret_cast<void**>(&g_origSetTex));
            g_hooked = okSamp && okTex;
            logf("world: AF rebind after Reset samp=%d tex=%d", (int)okSamp, (int)okTex);
        }
    }
    if (exeIdentityOk()) {
        applyLodAndFov();
    }
}

}  // namespace sm3spectacular
