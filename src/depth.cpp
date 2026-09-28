#include "depth.hpp"

#include "config.hpp"
#include "log.hpp"

#include <Windows.h>
#include <d3d9.h>

#include <atomic>

namespace sm3spectacular {
namespace {

constexpr DWORD kFourCcIntz = MAKEFOURCC('I', 'N', 'T', 'Z');
constexpr DWORD kFourCcRawz = MAKEFOURCC('R', 'A', 'W', 'Z');
constexpr DWORD kFourCcDf16 = MAKEFOURCC('D', 'F', '1', '6');
constexpr DWORD kFourCcDf24 = MAKEFOURCC('D', 'F', '2', '4');

// IDirect3DDevice9 vtable (after IUnknown 0..2):
//  29 = CreateDepthStencilSurface
//  39 = SetDepthStencilSurface  (NOT hooked — SetDS replace black-screened)
using CreateDepthStencilSurface_t =
    HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD,
                                BOOL, IDirect3DSurface9**, HANDLE*);

CreateDepthStencilSurface_t g_origCreateDS = nullptr;
std::atomic<bool> g_hooksInstalled{false};

IDirect3DTexture9* g_depthTex = nullptr;
UINT g_w = 0, g_h = 0;
D3DFORMAT g_depthFmt = D3DFMT_UNKNOWN;
bool g_intzOk = false;
bool g_checked = false;
bool g_lost = false;
unsigned g_createDsCalls = 0;

float g_near = 0.15f;
float g_far = 4000.f;
float g_reverse = 0.f;
float g_flip = 0.f;

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

D3DFORMAT pickDepthFormat(IDirect3DDevice9* device) {
    IDirect3D9* d3d = nullptr;
    if (FAILED(device->GetDirect3D(&d3d)) || !d3d) {
        return D3DFMT_UNKNOWN;
    }
    D3DDEVICE_CREATION_PARAMETERS cp{};
    device->GetCreationParameters(&cp);
    D3DDISPLAYMODE mode{};
    d3d->GetAdapterDisplayMode(cp.AdapterOrdinal, &mode);

    const D3DFORMAT cands[] = {(D3DFORMAT)kFourCcIntz, (D3DFORMAT)kFourCcRawz, (D3DFORMAT)kFourCcDf24,
                               (D3DFORMAT)kFourCcDf16};
    D3DFORMAT chosen = D3DFMT_UNKNOWN;
    for (D3DFORMAT f : cands) {
        if (SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
                                             D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, f))) {
            chosen = f;
            logf("depth: format OK 0x%08X", (unsigned)f);
            break;
        }
    }
    d3d->Release();
    return chosen;
}

void dropDepthTracking() {
    if (g_depthTex) {
        g_depthTex->Release();
        g_depthTex = nullptr;
    }
    g_w = g_h = 0;
}

HRESULT STDMETHODCALLTYPE HookCreateDepthStencilSurface(IDirect3DDevice9* self, UINT width,
                                                        UINT height, D3DFORMAT format,
                                                        D3DMULTISAMPLE_TYPE ms, DWORD msQuality,
                                                        BOOL discard, IDirect3DSurface9** out,
                                                        HANDLE* shared) {
    ++g_createDsCalls;

    if (!g_origCreateDS) {
        return D3DERR_INVALIDCALL;
    }

    // MSAA depth cannot be sampled as INTZ texture — stock path.
    if (!g_intzOk || g_lost || ms != D3DMULTISAMPLE_NONE || !out) {
        return g_origCreateDS(self, width, height, format, ms, msQuality, discard, out, shared);
    }

    // Shadow / aux depth targets are smaller than the backbuffer — do not steal tracking.
    // Keep sampling the main scene DS (usually full resolution).
    IDirect3DSurface9* bb = nullptr;
    UINT bbW = 0, bbH = 0;
    if (SUCCEEDED(self->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
        D3DSURFACE_DESC bd{};
        bb->GetDesc(&bd);
        bbW = bd.Width;
        bbH = bd.Height;
        bb->Release();
    }
    const bool looksLikeSceneDs =
        bbW > 0 && bbH > 0 && width >= (bbW * 3) / 4 && height >= (bbH * 3) / 4;

    if (!looksLikeSceneDs) {
        // Quiet: aux CreateDS is frequent (shadows).
        return g_origCreateDS(self, width, height, format, ms, msQuality, discard, out, shared);
    }

    // Replace with samplable INTZ/RAWZ/DF* texture surface (game still gets an IDirect3DSurface9*).
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = self->CreateTexture(width, height, 1, D3DUSAGE_DEPTHSTENCIL, g_depthFmt,
                                     D3DPOOL_DEFAULT, &tex, nullptr);
    if (FAILED(hr) || !tex) {
        logf("depth: INTZ CreateTexture failed 0x%08lX — stock DS", (unsigned long)hr);
        return g_origCreateDS(self, width, height, format, ms, msQuality, discard, out, shared);
    }

    IDirect3DSurface9* surf = nullptr;
    hr = tex->GetSurfaceLevel(0, &surf);
    if (FAILED(hr) || !surf) {
        tex->Release();
        logf("depth: GetSurfaceLevel failed — stock DS");
        return g_origCreateDS(self, width, height, format, ms, msQuality, discard, out, shared);
    }

    dropDepthTracking();
    g_depthTex = tex;  // keep our CreateTexture ref for sampling
    *out = surf;       // game owns the GetSurfaceLevel ref
    if (shared) {
        *shared = nullptr;
    }
    g_w = width;
    g_h = height;
    logf("depth: INTZ DS ready %ux%u (samplable) fmt=0x%08X CreateDS=#%u", width, height,
         (unsigned)g_depthFmt, g_createDsCalls);
    return D3D_OK;
}

}  // namespace

void depthPreparePresentParams(D3DPRESENT_PARAMETERS* /*pp*/) {
    // Do NOT disable AutoDepthStencil and do NOT force AutoDepthStencilFormat —
    // those paths black-screened. We only intercept explicit CreateDepthStencilSurface.
}

void depthCaptureReplace(IDirect3DDevice9* /*device*/) {
    // Do NOT SetDepthStencilSurface-replace — crashed / black-screened at boot.
}

void depthOnDeviceHooked(IDirect3DDevice9* device) {
    if (!device || g_hooksInstalled.load()) {
        return;
    }
    g_depthFmt = pickDepthFormat(device);
    g_intzOk = (g_depthFmt != D3DFMT_UNKNOWN);
    g_checked = true;
    g_lost = false;

    void** vt = vtableOf(device);
    const bool ok = patchVtableSlot(vt, 29, reinterpret_cast<void*>(&HookCreateDepthStencilSurface),
                                    reinterpret_cast<void**>(&g_origCreateDS));
    g_hooksInstalled.store(true);
    logf("depth: CreateDS hook=%d INTZ-like=%d (SetDS not hooked)", (int)ok, (int)g_intzOk);
}

void depthOnLostDevice() {
    g_lost = true;
    dropDepthTracking();
    logf("depth: lost device — tracking dropped");
}

void depthOnResetDevice(IDirect3DDevice9* /*device*/) {
    g_lost = false;
    logf("depth: reset device (waiting for CreateDS)");
}

IDirect3DTexture9* depthTexture() { return g_depthTex; }
bool depthAvailable() { return g_depthTex != nullptr && !g_lost; }
UINT depthWidth() { return g_w; }
UINT depthHeight() { return g_h; }

void depthLinearParams(float out[4]) {
    out[0] = config().depthNear > 1e-4f ? config().depthNear : g_near;
    out[1] = config().depthFar > 1.f ? config().depthFar : g_far;
    out[2] = config().depthReverse ? 1.f : g_reverse;
    out[3] = config().depthFlip ? 1.f : g_flip;
}

}  // namespace sm3spectacular
