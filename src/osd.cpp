#include "osd.hpp"

#include "config.hpp"
#include "fps.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstring>

namespace sm3spectacular {
namespace {

LARGE_INTEGER g_qpcFreq{};
LARGE_INTEGER g_lastFrame{};
LARGE_INTEGER g_windowStart{};
int g_windowFrames = 0;
float g_displayFps = 0.f;
bool g_qpcOk = false;

// 5x7 bitmap font for 0-9, F, P, S, :, space, /, U, L, M, I, N, T, D
// bit0 = top-left, row-major, 5 bits per row.
const unsigned char kGlyph[][7] = {
    // 0
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
    // 1
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    // 2
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F},
    // 3
    {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E},
    // 4
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    // 5
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
    // 6
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    // 7
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
    // 8
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    // 9
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
};

const unsigned char kGlyphF[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
const unsigned char kGlyphP[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
const unsigned char kGlyphS[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
const unsigned char kGlyphColon[7] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
const unsigned char kGlyphSpace[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const unsigned char kGlyphSlash[7] = {0x01, 0x02, 0x04, 0x04, 0x08, 0x10, 0x10};
const unsigned char kGlyphL[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
const unsigned char kGlyphI[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
const unsigned char kGlyphM[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
const unsigned char kGlyphU[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
const unsigned char kGlyphN[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
const unsigned char kGlyphT[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
const unsigned char kGlyphD[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};

const unsigned char* glyphFor(char c) {
    if (c >= '0' && c <= '9') {
        return kGlyph[c - '0'];
    }
    switch (c) {
        case 'F':
        case 'f':
            return kGlyphF;
        case 'P':
        case 'p':
            return kGlyphP;
        case 'S':
        case 's':
            return kGlyphS;
        case ':':
            return kGlyphColon;
        case '/':
            return kGlyphSlash;
        case 'L':
        case 'l':
            return kGlyphL;
        case 'I':
        case 'i':
            return kGlyphI;
        case 'M':
        case 'm':
            return kGlyphM;
        case 'U':
        case 'u':
            return kGlyphU;
        case 'N':
        case 'n':
            return kGlyphN;
        case 'T':
        case 't':
            return kGlyphT;
        case 'D':
        case 'd':
            return kGlyphD;
        default:
            return kGlyphSpace;
    }
}

void drawRect(IDirect3DDevice9* device, float x, float y, float w, float h, D3DCOLOR color) {
#pragma pack(push, 1)
    struct Vtx {
        float x, y, z, rhw;
        D3DCOLOR color;
    };
#pragma pack(pop)
    Vtx v[4] = {
        {x, y, 0, 1, color},
        {x + w, y, 0, 1, color},
        {x, y + h, 0, 1, color},
        {x + w, y + h, 0, 1, color},
    };
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx));
}

void drawText(IDirect3DDevice9* device, float x, float y, const char* text, D3DCOLOR color,
              float scale) {
    const float px = 2.f * scale;
    float cx = x;
    for (const char* p = text; *p; ++p) {
        const unsigned char* g = glyphFor(*p);
        for (int row = 0; row < 7; ++row) {
            const unsigned char bits = g[row];
            for (int col = 0; col < 5; ++col) {
                if (bits & (0x10 >> col)) {
                    drawRect(device, cx + col * px, y + row * px, px, px, color);
                }
            }
        }
        cx += 6.f * px;
    }
}

void begin2d(IDirect3DDevice9* device) {
    device->SetPixelShader(nullptr);
    device->SetVertexShader(nullptr);
    device->SetTexture(0, nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
}

void tickFps() {
    if (!g_qpcOk) {
        QueryPerformanceFrequency(&g_qpcFreq);
        QueryPerformanceCounter(&g_lastFrame);
        g_windowStart = g_lastFrame;
        g_qpcOk = g_qpcFreq.QuadPart > 0;
        return;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    ++g_windowFrames;
    const double windowSec =
        double(now.QuadPart - g_windowStart.QuadPart) / double(g_qpcFreq.QuadPart);
    if (windowSec >= 0.5) {
        g_displayFps = float(g_windowFrames / windowSec);
        g_windowFrames = 0;
        g_windowStart = now;
    }
    g_lastFrame = now;
}

}  // namespace

void osdOnPresent(IDirect3DDevice9* device) {
    tickFps();
    if (!device || !config().showFpsCounter) {
        return;
    }

    D3DVIEWPORT9 vp{};
    if (FAILED(device->GetViewport(&vp)) || vp.Width < 80 || vp.Height < 40) {
        return;
    }

    char line[64];
    const int limit = currentFpsLimit();
    if (limit <= 0) {
        std::snprintf(line, sizeof(line), "FPS:%.0f LIM:UNL", g_displayFps);
    } else {
        std::snprintf(line, sizeof(line), "FPS:%.0f LIM:%d", g_displayFps, limit);
    }

    // Save a minimal set of states.
    DWORD oldFVF = 0, oldAlpha = 0, oldSrc = 0, oldDst = 0, oldZ = 0, oldCull = 0, oldFog = 0;
    DWORD oldLight = 0, oldColorOp = 0, oldColorArg1 = 0, oldAlphaOp = 0;
    IDirect3DPixelShader9* oldPs = nullptr;
    IDirect3DVertexShader9* oldVs = nullptr;
    IDirect3DBaseTexture9* oldTex = nullptr;

    device->GetFVF(&oldFVF);
    device->GetPixelShader(&oldPs);
    device->GetVertexShader(&oldVs);
    device->GetTexture(0, &oldTex);
    device->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldAlpha);
    device->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
    device->GetRenderState(D3DRS_DESTBLEND, &oldDst);
    device->GetRenderState(D3DRS_ZENABLE, &oldZ);
    device->GetRenderState(D3DRS_CULLMODE, &oldCull);
    device->GetRenderState(D3DRS_FOGENABLE, &oldFog);
    device->GetRenderState(D3DRS_LIGHTING, &oldLight);
    device->GetTextureStageState(0, D3DTSS_COLOROP, &oldColorOp);
    device->GetTextureStageState(0, D3DTSS_COLORARG1, &oldColorArg1);
    device->GetTextureStageState(0, D3DTSS_ALPHAOP, &oldAlphaOp);

    begin2d(device);

    const float scale = (vp.Width >= 1600) ? 2.f : 1.5f;
    const float x = 12.f;
    const float y = 10.f;
    const float textW = float(std::strlen(line)) * 6.f * 2.f * scale;
    const float textH = 7.f * 2.f * scale;
    drawRect(device, x - 4.f, y - 4.f, textW + 8.f, textH + 8.f, D3DCOLOR_ARGB(160, 0, 0, 0));
    // Shadow + main
    drawText(device, x + 1.f, y + 1.f, line, D3DCOLOR_ARGB(255, 0, 0, 0), scale);
    const D3DCOLOR col = (g_displayFps >= 55.f)   ? D3DCOLOR_ARGB(255, 80, 255, 120)
                         : (g_displayFps >= 40.f) ? D3DCOLOR_ARGB(255, 255, 220, 80)
                                                  : D3DCOLOR_ARGB(255, 255, 80, 80);
    drawText(device, x, y, line, col, scale);

    device->SetFVF(oldFVF);
    device->SetPixelShader(oldPs);
    device->SetVertexShader(oldVs);
    device->SetTexture(0, oldTex);
    if (oldPs) {
        oldPs->Release();
    }
    if (oldVs) {
        oldVs->Release();
    }
    if (oldTex) {
        oldTex->Release();
    }
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, oldAlpha);
    device->SetRenderState(D3DRS_SRCBLEND, oldSrc);
    device->SetRenderState(D3DRS_DESTBLEND, oldDst);
    device->SetRenderState(D3DRS_ZENABLE, oldZ);
    device->SetRenderState(D3DRS_CULLMODE, oldCull);
    device->SetRenderState(D3DRS_FOGENABLE, oldFog);
    device->SetRenderState(D3DRS_LIGHTING, oldLight);
    device->SetTextureStageState(0, D3DTSS_COLOROP, oldColorOp);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, oldColorArg1);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, oldAlphaOp);
}

void osdOnLostDevice() {}

void osdShutdown() {}

}  // namespace sm3spectacular
