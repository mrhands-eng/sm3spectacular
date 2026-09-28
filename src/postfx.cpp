#include "postfx.hpp"

#include "config.hpp"
#include "depth.hpp"
#include "log.hpp"

#include <Windows.h>
#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sm3spectacular {
namespace {

using D3DXCompileShader_t = HRESULT(WINAPI*)(LPCSTR, UINT, const void*, const void*, LPCSTR, LPCSTR,
                                             DWORD, void**, void**, void*);
using D3DXCreateTextureFromFileW_t = HRESULT(WINAPI*)(LPDIRECT3DDEVICE9, LPCWSTR,
                                                      LPDIRECT3DTEXTURE9*);

HMODULE g_d3dx = nullptr;
D3DXCompileShader_t pCompile = nullptr;
D3DXCreateTextureFromFileW_t pLoadTex = nullptr;

void* d3dxBufPtr(void* buf) {
    if (!buf) return nullptr;
    void** vt = *reinterpret_cast<void***>(buf);
    using Fn = void*(STDMETHODCALLTYPE*)(void*);
    return reinterpret_cast<Fn>(vt[3])(buf);
}
DWORD d3dxBufSize(void* buf) {
    if (!buf) return 0;
    void** vt = *reinterpret_cast<void***>(buf);
    using Fn = DWORD(STDMETHODCALLTYPE*)(void*);
    return reinterpret_cast<Fn>(vt[4])(buf);
}
void d3dxBufRelease(void* buf) {
    if (!buf) return;
    void** vt = *reinterpret_cast<void***>(buf);
    using Fn = ULONG(STDMETHODCALLTYPE*)(void*);
    reinterpret_cast<Fn>(vt[2])(buf);
}

bool loadD3dx() {
    if (g_d3dx) return pCompile != nullptr;
    static const wchar_t* kNames[] = {L"d3dx9_43.dll", L"d3dx9_42.dll", L"d3dx9_30.dll"};
    for (auto* n : kNames) {
        g_d3dx = LoadLibraryW(n);
        if (g_d3dx) {
            logf("postfx: loaded %ls", n);
            break;
        }
    }
    if (!g_d3dx) {
        logf("postfx: d3dx9*.dll not found — visuals off");
        return false;
    }
    pCompile = reinterpret_cast<D3DXCompileShader_t>(GetProcAddress(g_d3dx, "D3DXCompileShader"));
    pLoadTex = reinterpret_cast<D3DXCreateTextureFromFileW_t>(
        GetProcAddress(g_d3dx, "D3DXCreateTextureFromFileW"));
    return pCompile != nullptr;
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
static const char kVs[] = R"(
struct VS_IN { float4 pos : POSITION; float2 uv : TEXCOORD0; };
struct VS_OUT { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VS_OUT main(VS_IN i) { VS_OUT o; o.pos = i.pos; o.uv = i.uv; return o; }
)";

// Copy (kept for future 1:1 blit paths)
static const char kPsCopy[] = R"(
sampler2D s0 : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(s0, uv); }
)";

// Dual kawase-ish downsample (avg 4 taps)
static const char kPsDown[] = R"(
sampler2D s0 : register(s0);
float4 gPx : register(c0); // px.xy, unused
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float2 px = gPx.xy;
  float4 c =
    tex2D(s0, uv + float2(-0.5,-0.5)*px) +
    tex2D(s0, uv + float2( 0.5,-0.5)*px) +
    tex2D(s0, uv + float2(-0.5, 0.5)*px) +
    tex2D(s0, uv + float2( 0.5, 0.5)*px);
  return c * 0.25;
}
)";

// Separable gaussian blur (horizontal or vertical via gPx.zw direction)
static const char kPsBlur[] = R"(
sampler2D s0 : register(s0);
float4 gPx : register(c0); // px * dir in xy, sigma scale in z
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float2 dir = gPx.xy;
  float3 c = tex2D(s0, uv).rgb * 0.227027;
  c += tex2D(s0, uv + dir * 1.384615).rgb * 0.316216;
  c += tex2D(s0, uv - dir * 1.384615).rgb * 0.316216;
  c += tex2D(s0, uv + dir * 3.230769).rgb * 0.070270;
  c += tex2D(s0, uv - dir * 3.230769).rgb * 0.070270;
  return float4(c, 1);
}
)";

// Bloom / neon extract — ONLY vivid neon + hot lamps (strict; midtones stay out)
static const char kPsBloomExtract[] = R"(
sampler2D s0 : register(s0);
float4 gP : register(c0); // threshold, knee, exposure, neonBoost
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float3 c = tex2D(s0, uv).rgb * gP.z;
  float br = max(max(c.r, c.g), c.b);
  float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
  float sat = saturate((br - luma) / max(br, 1e-4));

  // Hard threshold — walls/sky/concrete must not enter the glow buffer
  float soft = saturate((br - gP.x + gP.y) / max(2.0 * gP.y, 1e-4));
  soft = soft * soft * soft;
  float brightMask = soft * soft;

  float vivid = saturate(max(max(c.r - c.g, c.b - c.g), max(c.g - c.r, abs(c.r - c.b))) * 2.8);
  float neon = sat * sat * vivid * saturate((br - 0.48) * 4.2) * gP.w;

  float warm = saturate(c.r - c.b * 0.90 - 0.18) * saturate((br - 0.68) * 5.0);
  float hot = saturate((br - 0.88) * 8.0);
  float lamp = max(warm, hot);

  float rejectFlat = saturate((sat - 0.18) * 10.0);
  float mask = saturate(max(neon, lamp * rejectFlat) * brightMask);
  float3 boosted = c * (1.0 + neon * 1.25 + lamp * 0.40);
  return float4(boosted * mask, mask);
}
)";

// Bloom composite: additive glow only — scene luma/edges untouched (no replace)
static const char kPsBloomComp[] = R"(
sampler2D sScene : register(s0);
sampler2D sBloom : register(s1);
float4 gP : register(c0); // mix, satKeep, softCap, unused
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float3 scene = tex2D(sScene, uv).rgb;
  float4 b = tex2D(sBloom, uv);
  float3 bloom = b.rgb;
  float luma = dot(bloom, float3(0.2126, 0.7152, 0.0722));
  bloom = lerp(luma.xxx, bloom, gP.y);
  bloom = bloom / (1.0 + bloom * gP.z);
  // Weight by extract mask so empty glow texels never lift the frame
  float w = saturate(b.a);
  float3 outc = saturate(scene + bloom * gP.x * w);
  return float4(outc, 1);
}
)";

// Main grade: FakeHDR, Clarity, Unsharp, Ambient, Dehaze, AO, SSR, DPX, dual LUT
static const char kPsGrade[] = R"(
sampler2D sScene : register(s0);
sampler2D sLutA  : register(s1);
sampler2D sLutB  : register(s2);
sampler2D sDepth : register(s3);
sampler2D sAO    : register(s4);
sampler2D sSSR   : register(s5);
float4 gP0 : register(c0); // px.xy, strength, HDRPower
float4 gP1 : register(c1); // lutA, lutB, unsharp, clarity
float4 gP2 : register(c2); // selA, amtA, selB, amtB
float4 gP3 : register(c3); // dpx CF, contrast, sat, strength
float4 gP4 : register(c4); // fakeHdrAmt, contrast, exposure, vibrance
float4 gP5 : register(c5); // dehazeAlpha, dehazeDepthMul, aoAmount, ssrAmount
float4 gP6 : register(c6); // depthNear, far, reverse, flip
float4 gP7 : register(c7); // uiMaskThresh, useDepth, useAO, useSSR
float4 gP8 : register(c8); // shadowCrush, highlightSoft, warmBias, aerialHaze

float linearDepth(float2 uv) {
  float2 uvs = uv;
  if (gP6.w > 0.5) uvs.y = 1.0 - uvs.y;
  float d = tex2D(sDepth, uvs).r;
  if (gP6.z > 0.5) d = 1.0 - d;
  float n = gP6.x, f = gP6.y;
  // perspective decode (typical DX9)
  float z = d;
  return (n * f) / max(f - z * (f - n), 1e-5);
}

float3 applyLut(sampler2D lut, float3 color, float selector, float lutAmount, float useBw) {
  float tile = 64.0;
  float2 texelsize = float2(1.0 / tile, 1.0 / tile);
  texelsize.x /= tile;
  color = saturate(color);
  if (useBw > 0.5) {
    // Cinetools black/white in-out from preset
    float3 ib = float3(0.164706, 0.0, 0.035294);
    float3 iw = float3(1.0, 1.0, 0.894118);
    color = saturate((color - ib) / max(iw - ib, 1e-5));
  }
  float3 lutcoord = float3((color.xy * tile - color.xy + 0.5) * texelsize.xy,
                           color.z * tile - color.z);
  lutcoord.y /= lutAmount;
  lutcoord.y += selector / lutAmount;
  float lerpfact = frac(lutcoord.z);
  lutcoord.x += (lutcoord.z - lerpfact) * texelsize.y;
  float3 a = tex2Dlod(lut, float4(lutcoord.xy, 0, 0)).xyz;
  float3 b = tex2Dlod(lut, float4(lutcoord.x + texelsize.y, lutcoord.y, 0, 0)).xyz;
  float3 outc = lerp(a, b, lerpfact);
  if (useBw > 0.5) {
    float3 ob = float3(0.011765, 0.0, 0.0);
    float3 ow = float3(0.949020, 1.0, 0.952941);
    outc = saturate(lerp(ob, ow, outc));
  }
  return saturate(outc);
}

float3 dpxGrade(float3 input) {
  float Colorfulness = gP3.x, Contrast = gP3.y, Saturation = gP3.z, Strength = gP3.w;
  float3 RGB_Curve = float3(8,8,8), RGB_C = float3(0.36,0.36,0.34);
  float3x3 RGB = float3x3(
     2.6714711726599600, -1.2672360578624100, -0.4109956021722270,
    -1.0251070293466400,  1.9840911624108900,  0.0439502493584124,
     0.0610009456429445, -0.2236707508128630,  1.1590210416706100);
  float3x3 XYZ = float3x3(
     0.5003033835433160,  0.3380975732227390,  0.1645897795458570,
     0.2579688942747580,  0.6761952591447060,  0.0658358459823868,
     0.0234517888692628,  0.1126992737203000,  0.8668396731242010);
  float3 B = input * (1.0 - Contrast) + (0.5 * Contrast);
  float3 Btemp = 1.0 / (1.0 + exp(RGB_Curve * 0.5));
  B = ((1.0 / (1.0 + exp(-RGB_Curve * (B - RGB_C)))) / (-2.0 * Btemp + 1.0))
    + (-Btemp / (-2.0 * Btemp + 1.0));
  float value = max(max(B.r, B.g), B.b);
  float3 color = B / max(value, 1e-5);
  color = pow(abs(color), 1.0 / max(Colorfulness, 0.1));
  float3 c0 = mul(XYZ, color * value);
  float luma = dot(c0, float3(0.30, 0.59, 0.11));
  c0 = (1.0 - Saturation) * luma + Saturation * c0;
  c0 = mul(RGB, c0);
  return lerp(input, saturate(c0), Strength);
}

float4 main(float2 uv : TEXCOORD0) : COLOR {
  float2 px = gP0.xy;
  float strength = gP0.z;
  float3 original = tex2D(sScene, uv).rgb;
  float3 color = original;

  // Bright HUD-ish pixels only (no frame-flatness — that flickered while moving)
  float uiLuma = dot(original, float3(0.2126,0.7152,0.0722));
  float uiMask = saturate((uiLuma - gP7.x) * 8.0);

  // FakeHDR — neighbor blur softens the whole frame; amount via gP4.x (0 = off)
  if (gP4.x > 0.001) {
    float r1 = 0.793, r2 = 0.870;
    float3 s1 =
      tex2D(sScene, uv+float2( 1.5,-1.5)*r1*px).rgb +
      tex2D(sScene, uv+float2(-1.5,-1.5)*r1*px).rgb +
      tex2D(sScene, uv+float2( 1.5, 1.5)*r1*px).rgb +
      tex2D(sScene, uv+float2(-1.5, 1.5)*r1*px).rgb +
      tex2D(sScene, uv+float2( 0.0,-2.5)*r1*px).rgb +
      tex2D(sScene, uv+float2( 0.0, 2.5)*r1*px).rgb +
      tex2D(sScene, uv+float2(-2.5, 0.0)*r1*px).rgb +
      tex2D(sScene, uv+float2( 2.5, 0.0)*r1*px).rgb;
    s1 *= 0.005;
    float3 s2 =
      tex2D(sScene, uv+float2( 1.5,-1.5)*r2*px).rgb +
      tex2D(sScene, uv+float2(-1.5,-1.5)*r2*px).rgb +
      tex2D(sScene, uv+float2( 1.5, 1.5)*r2*px).rgb +
      tex2D(sScene, uv+float2(-1.5, 1.5)*r2*px).rgb +
      tex2D(sScene, uv+float2( 0.0,-2.5)*r2*px).rgb +
      tex2D(sScene, uv+float2( 0.0, 2.5)*r2*px).rgb +
      tex2D(sScene, uv+float2(-2.5, 0.0)*r2*px).rgb +
      tex2D(sScene, uv+float2( 2.5, 0.0)*r2*px).rgb;
    s2 *= 0.010;
    float dist = r2 - r1;
    float3 HDR = (color + (s2 - s1)) * dist;
    float3 hdrCol = saturate(pow(abs(HDR + color), abs(gP0.w)) + HDR);
    color = lerp(color, hdrCol, gP4.x);
  }

  // Clarity — local contrast helps distant building edges
  if (gP1.w > 0.001) {
    float3 blur =
      (tex2D(sScene, uv+float2( 2.5,0)*px).rgb +
       tex2D(sScene, uv+float2(-2.5,0)*px).rgb +
       tex2D(sScene, uv+float2(0, 2.5)*px).rgb +
       tex2D(sScene, uv+float2(0,-2.5)*px).rgb) * 0.25;
    float cl = dot(color - blur, float3(0.2126,0.7152,0.0722));
    color = saturate(color + cl * gP1.w * (1.0 - uiMask));
  }

  if (gP1.z > 0.001) {
    float3 blur =
      (tex2D(sScene, uv+float2(1,0)*px).rgb +
       tex2D(sScene, uv+float2(-1,0)*px).rgb +
       tex2D(sScene, uv+float2(0,1)*px).rgb +
       tex2D(sScene, uv+float2(0,-1)*px).rgb) * 0.25;
    float lumaC = dot(color, float3(0.2126,0.7152,0.0722));
    float lumaB = dot(blur, float3(0.2126,0.7152,0.0722));
    float sharp = clamp((lumaC - lumaB) * gP1.z, -0.18, 0.18);
    color = saturate(color + sharp * (1.0 - uiMask));
  }

  // Exposure + midtone contrast (gP4.z / gP4.y)
  color = saturate(color * gP4.z);
  color = saturate((color - 0.5) * gP4.y + 0.5);

  // Deeper shadows + soft highlights (filmic-ish)
  if (gP8.x > 0.001 || gP8.y > 0.001) {
    float3 crushed = color * color / max(lerp(color, float3(1,1,1), 1.0 - gP8.x), 1e-4);
    color = lerp(color, crushed, gP8.x);
    // Soften extreme highlights so sunlight stays natural
    float3 softHi = color / (1.0 + color * gP8.y);
    color = lerp(color, softHi, saturate(dot(color, float3(0.2126,0.7152,0.0722))));
  }
  // Mild warm daylight bias (keeps skin/brick alive without yellow sun wash)
  if (gP8.z > 0.001) {
    color.r = saturate(color.r + gP8.z * 0.012);
    color.b = saturate(color.b - gP8.z * 0.008);
  }

  float ld = 0.0;
  if (gP7.y > 0.5) {
    ld = saturate(linearDepth(uv) / gP6.y);
  }

  // Real dehaze: restore contrast (do NOT add grey air light)
  if (gP5.x > 0.001 && uiMask < 0.5) {
    float haze = gP5.x;
    if (gP7.y > 0.5) haze *= saturate(ld * gP5.y * 4.0);
    else haze *= 0.45;
    float mid = dot(color, float3(0.2126,0.7152,0.0722));
    color = saturate((color - mid) * (1.0 + haze * 1.1) + mid * (1.0 - haze * 0.25));
  }

  // Aerial perspective — cool distant city air (depth-driven)
  if (gP8.w > 0.001 && gP7.y > 0.5 && uiMask < 0.5) {
    float air = saturate((ld - 0.18) / 0.72);
    air = air * air * (3.0 - 2.0 * air); // smoothstep
    air *= gP8.w;
    float3 hazeCol = float3(0.52, 0.60, 0.78);
    // Soft light shafts feel: slightly lift mid-distance, denser far
    color = lerp(color, saturate(color * (1.0 - air * 0.35) + hazeCol * air), air);
  }

  if (gP7.z > 0.5 && gP5.z > 0.001 && uiMask < 0.5) {
    float ao = tex2D(sAO, uv).r;
    // Stronger AO in crevices for deeper contact shadows
    color *= lerp(1.0, ao * ao, gP5.z * 0.65);
    color *= lerp(1.0, ao, gP5.z * 0.45);
  }

  if (gP7.w > 0.5 && gP5.w > 0.001 && uiMask < 0.5) {
    float4 ssr = tex2D(sSSR, uv);
    float fres = saturate(1.0 - ld * 0.75) * ssr.a;
    color = lerp(color, saturate(color + ssr.rgb * 0.85), fres * gP5.w * 0.92);
  }

  color = dpxGrade(color);

  if (gP1.x > 0.001)
    color = lerp(color, applyLut(sLutA, color, gP2.x, gP2.y, 0.0), gP1.x * (1.0 - uiMask * 0.9));
  if (gP1.y > 0.001)
    color = lerp(color, applyLut(sLutB, color, gP2.z, gP2.w, 0.0), gP1.y * (1.0 - uiMask * 0.9));

  // Vibrance — punch mid-sats without clipping already-vivid neons
  if (gP4.w > 0.001 && uiMask < 0.5) {
    float lumaV = dot(color, float3(0.2126,0.7152,0.0722));
    float mx = max(color.r, max(color.g, color.b));
    float mn = min(color.r, min(color.g, color.b));
    float satNow = saturate((mx - mn) / max(mx, 1e-4));
    float vib = gP4.w * (1.0 - satNow * satNow);
    color = saturate(lerp(lumaV.xxx, color, 1.0 + vib));
  }

  float dither = frac(sin(dot(uv, float2(12.9898,78.233))) * 43758.5453);
  color += (dither - 0.5) / 512.0;

  color = lerp(original, color, strength);
  return float4(saturate(color), 1);
}
)";

// Light FXAA — use with FSAA Off for remaster AA without killing INTZ depth
static const char kPsFxaa[] = R"(
sampler2D s0 : register(s0);
float4 gPx : register(c0); // px.xy, blend (0..1), unused
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float2 px = gPx.xy;
  float blend = gPx.z;
  float3 rgbNW = tex2D(s0, uv+float2(-1,-1)*px).rgb;
  float3 rgbNE = tex2D(s0, uv+float2( 1,-1)*px).rgb;
  float3 rgbSW = tex2D(s0, uv+float2(-1, 1)*px).rgb;
  float3 rgbSE = tex2D(s0, uv+float2( 1, 1)*px).rgb;
  float3 rgbM  = tex2D(s0, uv).rgb;
  float3 lumaW = float3(0.299,0.587,0.114);
  float lumaNW = dot(rgbNW, lumaW), lumaNE = dot(rgbNE, lumaW);
  float lumaSW = dot(rgbSW, lumaW), lumaSE = dot(rgbSE, lumaW);
  float lumaM  = dot(rgbM, lumaW);
  float lumaMin = min(lumaM, min(min(lumaNW,lumaNE), min(lumaSW,lumaSE)));
  float lumaMax = max(lumaM, max(max(lumaNW,lumaNE), max(lumaSW,lumaSE)));
  float2 dir;
  dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
  dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
  float dirReduce = max((lumaNW+lumaNE+lumaSW+lumaSE)*0.03125, 0.0078125);
  float rcpDir = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
  dir = clamp(dir * rcpDir, -8.0, 8.0) * px;
  float3 rgbA = 0.5 * (tex2D(s0, uv+dir*(1.0/3.0-0.5)).rgb + tex2D(s0, uv+dir*(2.0/3.0-0.5)).rgb);
  float3 rgbB = rgbA * 0.5 + 0.25 * (tex2D(s0, uv+dir*-0.5).rgb + tex2D(s0, uv+dir*0.5).rgb);
  float lumaB = dot(rgbB, lumaW);
  float3 outc = ((lumaB < lumaMin) || (lumaB > lumaMax)) ? rgbA : rgbB;
  outc = lerp(rgbM, outc, blend);
  return float4(outc, 1);
}
)";

// Deband (stable) + tiny grain — no time-varying noise (flicker)
static const char kPsFinish[] = R"(
sampler2D s0 : register(s0);
float4 gP : register(c0); // px.xy, grain, unused
float4 gP1 : register(c1); // debandStrength, vignette, unused
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float2 px = gP.xy;
  float3 color = tex2D(s0, uv).rgb;
  float3 avg =
    (tex2D(s0, uv+float2(1,0)*px).rgb +
     tex2D(s0, uv+float2(-1,0)*px).rgb +
     tex2D(s0, uv+float2(0,1)*px).rgb +
     tex2D(s0, uv+float2(0,-1)*px).rgb) * 0.25;
  float diff = length(color - avg);
  float flat = 1.0 - saturate(diff * 60.0);
  color = lerp(color, avg, flat * gP1.x * 0.35);

  float n = frac(sin(dot(uv, float2(12.9898,78.233))) * 43758.5453);
  color += (n - 0.5) * gP.z;

  float2 vc = uv - 0.5;
  float vig = saturate(1.0 - dot(vc, vc) * gP1.y);
  color *= vig;

  return float4(saturate(color), 1);
}
)";

// CAS-like sharpen — restores edge clarity after MSAA resolve
static const char kPsCas[] = R"(
sampler2D s0 : register(s0);
float4 gP : register(c0); // px.xy, sharpness (0..1), unused
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float2 px = gP.xy;
  float sharp = gP.z;
  float3 c = tex2D(s0, uv).rgb;
  float3 n = tex2D(s0, uv + float2(0,-1)*px).rgb;
  float3 s = tex2D(s0, uv + float2(0, 1)*px).rgb;
  float3 e = tex2D(s0, uv + float2(1, 0)*px).rgb;
  float3 w = tex2D(s0, uv + float2(-1,0)*px).rgb;
  float3 nw = tex2D(s0, uv + float2(-1,-1)*px).rgb;
  float3 ne = tex2D(s0, uv + float2( 1,-1)*px).rgb;
  float3 sw = tex2D(s0, uv + float2(-1, 1)*px).rgb;
  float3 se = tex2D(s0, uv + float2( 1, 1)*px).rgb;

  float3 mn = min(c, min(min(n,s), min(e,w)));
  mn = min(mn, min(min(nw,ne), min(sw,se)));
  float3 mx = max(c, max(max(n,s), max(e,w)));
  mx = max(mx, max(max(nw,ne), max(sw,se)));

  float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-4)));
  float3 wts = -amp * (lerp(0.0, 0.20, sharp));
  float3 wsum = 1.0 + 4.0 * wts;
  // cross weights
  float3 outc = (n + s + e + w) * wts + c;
  outc /= max(wsum, 1e-4);

  // Extra luma unsharp for MSAA resolve recovery
  float3 blur4 = (n + s + e + w) * 0.25;
  float lumaC = dot(c, float3(0.2126,0.7152,0.0722));
  float lumaB = dot(blur4, float3(0.2126,0.7152,0.0722));
  float bump = clamp((lumaC - lumaB) * (0.55 + sharp * 0.85), -0.14, 0.14);
  outc = saturate(outc + bump);

  return float4(outc, 1);
}
)";

// Linear depth to RT (R16F or A8R8G8B8 packed in r)
static const char kPsLinDepth[] = R"(
sampler2D sDepth : register(s0);
float4 gP : register(c0); // near, far, reverse, flip
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float2 uvs = uv;
  if (gP.w > 0.5) uvs.y = 1.0 - uvs.y;
  float d = tex2D(sDepth, uvs).r;
  if (gP.z > 0.5) d = 1.0 - d;
  float n = gP.x, f = gP.y;
  float lin = (n * f) / max(f - d * (f - n), 1e-5);
  float norm = saturate(lin / f);
  return float4(norm, norm, norm, 1);
}
)";

// Reconstruct view normals from linearized depth texture (s0 = linear depth 0..1)
static const char kPsNormals[] = R"(
sampler2D sDepth : register(s0);
float4 gPx : register(c0); // px, far, unused
float3 pos(float2 uv) {
  float z = tex2D(sDepth, uv).r * gPx.z;
  float2 ndc = uv * 2.0 - 1.0;
  // approximate view-space with FoV~50
  float2 xy = ndc * float2(0.84, 0.60) * z;
  return float3(xy, z);
}
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float3 p = pos(uv);
  float3 dx = pos(uv + float2(gPx.x, 0)) - p;
  float3 dy = pos(uv + float2(0, gPx.y)) - p;
  float3 n = normalize(cross(dy, dx));
  return float4(n * 0.5 + 0.5, 1);
}
)";

// MXAO-lite (half-res friendly)
static const char kPsMxao[] = R"(
sampler2D sDepth : register(s0);
sampler2D sNormal : register(s1);
float4 gPx : register(c0); // px.xy, radius, amount
float4 gP1 : register(c1); // far, samples-ish, unused
float hash(float2 p) { return frac(sin(dot(p, float2(41.2, 289.1))) * 43758.5); }
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float center = tex2D(sDepth, uv).r;
  if (center > 0.98) return float4(1,1,1,1); // sky
  float3 n = tex2D(sNormal, uv).xyz * 2.0 - 1.0;
  float ao = 0.0;
  float wsum = 0.0;
  float rad = gPx.z * (1.0 - center);
  for (int i = 0; i < 8; ++i) {
    float fi = float(i);
    float ang = fi * 2.399963 + hash(uv) * 6.28;
    float r = (fi + 0.5) / 8.0;
    float2 off = float2(cos(ang), sin(ang)) * r * rad * gPx.xy * 40.0;
    float sampleD = tex2D(sDepth, uv + off).r;
    float3 sampleP = float3(off * sampleD * gP1.x, (sampleD - center) * gP1.x);
    float3 v = normalize(sampleP + float3(0,0,1e-4));
    float occ = saturate(dot(n, v));
    float dist = length(sampleP);
    float atten = 1.0 - saturate(dist / (gPx.z * gP1.x * 0.02 + 1e-3));
    ao += occ * atten;
    wsum += atten;
  }
  ao = 1.0 - saturate(ao / max(wsum, 1e-3)) * gPx.w;
  ao = pow(saturate(ao), 1.2);
  return float4(ao, ao, ao, 1);
}
)";

// SSR ray march — keep loop count modest; nested refine blew up D3DXCompileShader.
static const char kPsSsr[] = R"(
sampler2D sScene : register(s0);
sampler2D sDepth : register(s1);
sampler2D sNormal : register(s2);
float4 gPx : register(c0); // px.xy, intensity, fadeDist
float4 gP1 : register(c1); // stride, unused, thickness, unused
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float depth = tex2D(sDepth, uv).r;
  if (depth < 0.02 || depth > gPx.w) return float4(0,0,0,0);
  float3 n = tex2D(sNormal, uv).xyz * 2.0 - 1.0;
  float2 ndc = uv * 2.0 - 1.0;
  float3 V = normalize(float3(ndc * float2(0.84, 0.60), 1.0));
  float3 R = reflect(V, normalize(n));
  if (R.z < 0.05) return float4(0,0,0,0);

  float2 ray = uv;
  float2 stepUV = (R.xy / (R.z + 1e-3)) * gPx.xy * gP1.x;
  float3 hit = 0;
  float alpha = 0;
  float d = depth;
  for (int i = 0; i < 20; ++i) {
    ray += stepUV * (0.32 + 0.14 * float(i));
    if (ray.x < 0 || ray.x > 1 || ray.y < 0 || ray.y > 1) break;
    float sd = tex2D(sDepth, ray).r;
    if (sd < d - gP1.z * 0.002) continue;
    if (sd < d + gP1.z * 0.01 && sd > 0.001) {
      // One half-step refine (no nested loop — D3DX-safe)
      float2 mid = ray - stepUV * 0.5;
      float md = tex2D(sDepth, mid).r;
      if (md < d + gP1.z * 0.01 && md > 0.001) ray = mid;
      hit = tex2D(sScene, ray).rgb;
      float edge = saturate(1.0 - abs(ray.x - 0.5) * 2.0) *
                   saturate(1.0 - abs(ray.y - 0.5) * 2.0);
      float fres = pow(1.0 - saturate(dot(-V, n)), 2.2);
      alpha = fres * edge * gPx.z * saturate(1.0 - depth / gPx.w);
      break;
    }
    d = min(d, sd);
  }
  return float4(hit, alpha);
}
)";

// Soft SSR blur — preserves alpha (mask)
static const char kPsSsrBlur[] = R"(
sampler2D s0 : register(s0);
float4 gPx : register(c0); // dir.xy (pixel step), unused
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float2 dir = gPx.xy;
  float4 c = tex2D(s0, uv) * 0.227027;
  c += tex2D(s0, uv + dir * 1.384615) * 0.316216;
  c += tex2D(s0, uv - dir * 1.384615) * 0.316216;
  c += tex2D(s0, uv + dir * 3.230769) * 0.070270;
  c += tex2D(s0, uv - dir * 3.230769) * 0.070270;
  return c;
}
)";

// Simple GI/IL bounce: blur AO-weighted scene color into crevices
static const char kPsGi[] = R"(
sampler2D sScene : register(s0);
sampler2D sAO : register(s1);
sampler2D sDepth : register(s2);
float4 gPx : register(c0); // px.xy, amount, unused
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float ao = tex2D(sAO, uv).r;
  float3 scene = tex2D(sScene, uv).rgb;
  float3 bounce = 0;
  bounce += tex2D(sScene, uv + float2( 3, 0)*gPx.xy).rgb;
  bounce += tex2D(sScene, uv + float2(-3, 0)*gPx.xy).rgb;
  bounce += tex2D(sScene, uv + float2( 0, 3)*gPx.xy).rgb;
  bounce += tex2D(sScene, uv + float2( 0,-3)*gPx.xy).rgb;
  bounce += tex2D(sScene, uv + float2( 2, 2)*gPx.xy).rgb;
  bounce += tex2D(sScene, uv + float2(-2,-2)*gPx.xy).rgb;
  bounce *= 0.1667;
  float factor = saturate(1.0 - ao) * gPx.z;
  float3 outc = saturate(scene + bounce * factor * 0.35);
  return float4(outc, 1);
}
)";

// ---------------------------------------------------------------------------
// GPU objects
// ---------------------------------------------------------------------------
constexpr int kBloomLevels = 5;

IDirect3DTexture9* g_scene = nullptr;
IDirect3DTexture9* g_workA = nullptr;
IDirect3DTexture9* g_workB = nullptr;
IDirect3DTexture9* g_bloom[kBloomLevels] = {};
IDirect3DTexture9* g_bloomTmp = nullptr;
IDirect3DTexture9* g_linDepth = nullptr;
IDirect3DTexture9* g_normals = nullptr;
IDirect3DTexture9* g_ao = nullptr;
IDirect3DTexture9* g_ssr = nullptr;
IDirect3DTexture9* g_ssrTmp = nullptr;
IDirect3DTexture9* g_lutA = nullptr;
IDirect3DTexture9* g_lutB = nullptr;
IDirect3DTexture9* g_adapt = nullptr;     // small luma RT for eye adapt / AL
IDirect3DTexture9* g_alBright = nullptr;  // AmbientLight bright buffer
IDirect3DTexture9* g_dirt = nullptr;
IDirect3DTexture9* g_dirtOvr = nullptr;
IDirect3DTexture9* g_dirtOvb = nullptr;
IDirect3DTexture9* g_lensDb = nullptr;
IDirect3DTexture9* g_lensDb2 = nullptr;
IDirect3DTexture9* g_lensDov = nullptr;
IDirect3DTexture9* g_lensDuv = nullptr;

IDirect3DVertexShader9* g_vs = nullptr;
IDirect3DPixelShader9* g_psCopy = nullptr;
IDirect3DPixelShader9* g_psDown = nullptr;
IDirect3DPixelShader9* g_psBlur = nullptr;
IDirect3DPixelShader9* g_psBloomExtract = nullptr;
IDirect3DPixelShader9* g_psGrade = nullptr;
IDirect3DPixelShader9* g_psBloomComp = nullptr;
IDirect3DPixelShader9* g_psFxaa = nullptr;
IDirect3DPixelShader9* g_psFinish = nullptr;
IDirect3DPixelShader9* g_psCas = nullptr;
IDirect3DPixelShader9* g_psLinDepth = nullptr;
IDirect3DPixelShader9* g_psNormals = nullptr;
IDirect3DPixelShader9* g_psMxao = nullptr;
IDirect3DPixelShader9* g_psSsr = nullptr;
IDirect3DPixelShader9* g_psSsrBlur = nullptr;
IDirect3DPixelShader9* g_psGi = nullptr;
IDirect3DPixelShader9* g_psFakeHdr = nullptr;
IDirect3DPixelShader9* g_psEyeAdapt = nullptr;
IDirect3DPixelShader9* g_psAlExtract = nullptr;
IDirect3DPixelShader9* g_psAmbient = nullptr;

IDirect3DVertexDeclaration9* g_decl = nullptr;
IDirect3DVertexBuffer9* g_vb = nullptr;

UINT g_w = 0, g_h = 0;
bool g_ready = false;
bool g_failed = false;
bool g_lutsTried = false;
float g_time = 0.f;

// SSR compile state: 0 none, 1 ok, 2 fail (retry after Reset)
std::atomic<int> g_ssrState{0};

struct Vert {
    float x, y, z, w;
    float u, v;
};

bool compile(const char* src, const char* entry, const char* profile, std::vector<uint8_t>* out) {
    void* code = nullptr;
    void* errs = nullptr;
    const HRESULT hr =
        pCompile(src, (UINT)strlen(src), nullptr, nullptr, entry, profile, 0, &code, &errs, nullptr);
    if (FAILED(hr) || !code) {
        if (errs) {
            const char* msg = static_cast<const char*>(d3dxBufPtr(errs));
            logf("postfx %s: %s", profile, msg ? msg : "(no message)");
            d3dxBufRelease(errs);
        } else {
            logf("postfx %s failed 0x%08lX", profile, (unsigned long)hr);
        }
        return false;
    }
    auto* p = static_cast<const uint8_t*>(d3dxBufPtr(code));
    const DWORD sz = d3dxBufSize(code);
    if (!p || !sz) {
        d3dxBufRelease(code);
        if (errs) d3dxBufRelease(errs);
        return false;
    }
    out->assign(p, p + sz);
    d3dxBufRelease(code);
    if (errs) d3dxBufRelease(errs);
    return true;
}

// Disk cache for heavy shaders (SSR compile is ~20s on D3DX — once per machine).
constexpr DWORD kShaderCacheMagic = 0x33534D33;  // '3MS3'
constexpr DWORD kSsrCacheVersion = 5;            // bump when kPsSsr source/constants change

std::wstring cacheDir() { return moduleDirectory() + L"\\sm3spectacular_cache"; }

std::wstring cacheFilePath(const wchar_t* name) { return cacheDir() + L"\\" + name; }

bool loadShaderSourceFile(const wchar_t* fileName, std::string* out) {
    const auto path = moduleDirectory() + L"\\shaders\\" + fileName;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 512 * 1024) {
        CloseHandle(h);
        return false;
    }
    out->resize(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    const bool ok =
        ReadFile(h, out->data(), (DWORD)out->size(), &read, nullptr) && read == out->size();
    CloseHandle(h);
    if (!ok) {
        out->clear();
    }
    return ok;
}

IDirect3DPixelShader9* makePs(IDirect3DDevice9* dev, const char* src, const char* tag);  // fwd

IDirect3DPixelShader9* makePsFile(IDirect3DDevice9* dev, const wchar_t* fileName, const char* tag) {
    std::string src;
    if (!loadShaderSourceFile(fileName, &src)) {
        logf("postfx: shader file missing shaders\\%ls — %s unavailable", fileName, tag);
        return nullptr;
    }
    return makePs(dev, src.c_str(), tag);
}

bool loadCachedBlob(const wchar_t* name, DWORD version, std::vector<uint8_t>* out) {
    const auto path = cacheFilePath(name);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD magic = 0, ver = 0, sz = 0, read = 0;
    bool ok = false;
    if (ReadFile(h, &magic, 4, &read, nullptr) && read == 4 && magic == kShaderCacheMagic &&
        ReadFile(h, &ver, 4, &read, nullptr) && read == 4 && ver == version &&
        ReadFile(h, &sz, 4, &read, nullptr) && read == 4 && sz > 0 && sz < 8 * 1024 * 1024) {
        out->resize(sz);
        ok = ReadFile(h, out->data(), sz, &read, nullptr) && read == sz;
    }
    CloseHandle(h);
    if (!ok) {
        out->clear();
    }
    return ok;
}

bool saveCachedBlob(const wchar_t* name, DWORD version, const std::vector<uint8_t>& blob) {
    if (blob.empty() || blob.size() > 8 * 1024 * 1024) {
        return false;
    }
    CreateDirectoryW(cacheDir().c_str(), nullptr);
    const auto path = cacheFilePath(name);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD magic = kShaderCacheMagic;
    DWORD ver = version;
    DWORD sz = (DWORD)blob.size();
    DWORD written = 0;
    bool ok = WriteFile(h, &magic, 4, &written, nullptr) && written == 4 &&
              WriteFile(h, &ver, 4, &written, nullptr) && written == 4 &&
              WriteFile(h, &sz, 4, &written, nullptr) && written == 4 &&
              WriteFile(h, blob.data(), sz, &written, nullptr) && written == sz;
    CloseHandle(h);
    return ok;
}

IDirect3DPixelShader9* createPsFromBlob(IDirect3DDevice9* dev, const std::vector<uint8_t>& blob,
                                        const char* tag) {
    if (blob.empty()) {
        return nullptr;
    }
    IDirect3DPixelShader9* ps = nullptr;
    if (FAILED(dev->CreatePixelShader((const DWORD*)blob.data(), &ps)) || !ps) {
        logf("postfx: PS %s CreatePixelShader failed", tag);
        return nullptr;
    }
    return ps;
}

IDirect3DPixelShader9* makePs(IDirect3DDevice9* dev, const char* src, const char* tag) {
    std::vector<uint8_t> blob;
    if (!compile(src, "main", "ps_3_0", &blob)) {
        logf("postfx: PS %s compile failed", tag);
        return nullptr;
    }
    return createPsFromBlob(dev, blob, tag);
}

IDirect3DPixelShader9* makePsCached(IDirect3DDevice9* dev, const char* src, const char* tag,
                                    const wchar_t* cacheName, DWORD version) {
    std::vector<uint8_t> blob;
    if (loadCachedBlob(cacheName, version, &blob)) {
        IDirect3DPixelShader9* ps = createPsFromBlob(dev, blob, tag);
        if (ps) {
            logf("postfx: PS %s loaded from cache (%u bytes)", tag, (unsigned)blob.size());
            return ps;
        }
        logf("postfx: PS %s cache invalid — recompiling", tag);
        blob.clear();
    }
    logf("postfx: compiling PS %s (will cache for next launch)", tag);
    if (!compile(src, "main", "ps_3_0", &blob)) {
        logf("postfx: PS %s compile failed", tag);
        return nullptr;
    }
    if (saveCachedBlob(cacheName, version, blob)) {
        logf("postfx: PS %s cached (%u bytes)", tag, (unsigned)blob.size());
    } else {
        logf("postfx: PS %s cache write failed (still usable)", tag);
    }
    return createPsFromBlob(dev, blob, tag);
}

void releaseTex(IDirect3DTexture9*& t) {
    if (t) {
        t->Release();
        t = nullptr;
    }
}

void releaseDeviceObjects() {
    releaseTex(g_scene);
    releaseTex(g_workA);
    releaseTex(g_workB);
    releaseTex(g_bloomTmp);
    for (int i = 0; i < kBloomLevels; ++i) releaseTex(g_bloom[i]);
    releaseTex(g_linDepth);
    releaseTex(g_normals);
    releaseTex(g_ao);
    releaseTex(g_ssr);
    releaseTex(g_ssrTmp);
    releaseTex(g_lutA);
    releaseTex(g_lutB);
    releaseTex(g_adapt);
    releaseTex(g_alBright);
    releaseTex(g_dirt);
    releaseTex(g_dirtOvr);
    releaseTex(g_dirtOvb);
    releaseTex(g_lensDb);
    releaseTex(g_lensDb2);
    releaseTex(g_lensDov);
    releaseTex(g_lensDuv);
    g_lutsTried = false;
    if (g_vb) {
        g_vb->Release();
        g_vb = nullptr;
    }
    if (g_decl) {
        g_decl->Release();
        g_decl = nullptr;
    }
    auto relPs = [](IDirect3DPixelShader9*& p) {
        if (p) {
            p->Release();
            p = nullptr;
        }
    };
    if (g_vs) {
        g_vs->Release();
        g_vs = nullptr;
    }
    relPs(g_psCopy);
    relPs(g_psDown);
    relPs(g_psBlur);
    relPs(g_psBloomExtract);
    relPs(g_psGrade);
    relPs(g_psBloomComp);
    relPs(g_psFxaa);
    relPs(g_psFinish);
    relPs(g_psCas);
    relPs(g_psLinDepth);
    relPs(g_psNormals);
    relPs(g_psMxao);
    relPs(g_psSsr);
    relPs(g_psSsrBlur);
    relPs(g_psGi);
    relPs(g_psFakeHdr);
    relPs(g_psEyeAdapt);
    relPs(g_psAlExtract);
    relPs(g_psAmbient);
    // Allow SSR recompile after Reset / LostDevice.
    g_ssrState.store(0);
    g_ready = false;
    g_w = g_h = 0;
}

bool createRt(IDirect3DDevice9* dev, UINT w, UINT h, IDirect3DTexture9** out) {
    releaseTex(*out);
    HRESULT hr = dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                    out, nullptr);
    if (FAILED(hr)) {
        logf("postfx CreateTexture %ux%u failed 0x%08lX", w, h, (unsigned long)hr);
        return false;
    }
    return true;
}

bool ensureRts(IDirect3DDevice9* dev, UINT w, UINT h) {
    if (g_scene && g_w == w && g_h == h) return true;
    g_w = w;
    g_h = h;
    if (!createRt(dev, w, h, &g_scene)) return false;
    if (!createRt(dev, w, h, &g_workA)) return false;
    if (!createRt(dev, w, h, &g_workB)) return false;
    UINT bw = w, bh = h;
    for (int i = 0; i < kBloomLevels; ++i) {
        bw = (std::max)(1u, bw / 2);
        bh = (std::max)(1u, bh / 2);
        if (!createRt(dev, bw, bh, &g_bloom[i])) return false;
    }
    if (!createRt(dev, (std::max)(1u, w / 2), (std::max)(1u, h / 2), &g_bloomTmp)) return false;
    if (!createRt(dev, (std::max)(1u, w / 2), (std::max)(1u, h / 2), &g_linDepth)) return false;
    if (!createRt(dev, (std::max)(1u, w / 2), (std::max)(1u, h / 2), &g_normals)) return false;
    if (!createRt(dev, (std::max)(1u, w / 2), (std::max)(1u, h / 2), &g_ao)) return false;
    // Half-res SSR (matches depth/normals; full-res + heavy march froze D3DX/GPU)
    const UINT sw = (std::max)(1u, w / 2);
    const UINT sh = (std::max)(1u, h / 2);
    if (!createRt(dev, sw, sh, &g_ssr)) return false;
    if (!createRt(dev, sw, sh, &g_ssrTmp)) return false;
    if (!createRt(dev, 64, 64, &g_adapt)) return false;
    if (!createRt(dev, sw, sh, &g_alBright)) return false;
    return true;
}

void tryLoadLuts(IDirect3DDevice9* dev) {
    if (g_lutsTried || !pLoadTex) return;
    g_lutsTried = true;
    const auto dir = moduleDirectory() + L"\\textures\\";
    auto load = [&](const wchar_t* name, IDirect3DTexture9** out) {
        HRESULT hr = pLoadTex(dev, (dir + name).c_str(), out);
        logf("postfx tex %ls hr=0x%08lX %p", name, (unsigned long)hr, *out);
    };
    // PD80 LUTs (Remastered Cinetools + Bonus pack)
    load(L"pd80_cinelut.png", &g_lutA);
    load(L"pd80_example-lut.png", &g_lutB);
    // AmbientLight.fx sources (Remastered preset)
    load(L"Dirt.png", &g_dirt);
    load(L"DirtOVR.png", &g_dirtOvr);
    load(L"DirtOVB.png", &g_dirtOvb);
    load(L"LensDB.png", &g_lensDb);
    load(L"LensDB2.png", &g_lensDb2);
    load(L"LensDOV.png", &g_lensDov);
    load(L"LensDUV.png", &g_lensDuv);
}

bool initGpu(IDirect3DDevice9* dev) {
    if (g_ready) return true;
    if (g_failed) return false;
    logf("postfx: initGpu multipass begin");
    if (!loadD3dx()) {
        g_failed = true;
        return false;
    }
    std::vector<uint8_t> vsBlob;
    if (!compile(kVs, "main", "vs_3_0", &vsBlob) ||
        FAILED(dev->CreateVertexShader((const DWORD*)vsBlob.data(), &g_vs))) {
        g_failed = true;
        return false;
    }
    g_psCopy = makePs(dev, kPsCopy, "copy");
    g_psDown = makePs(dev, kPsDown, "down");
    g_psBlur = makePs(dev, kPsBlur, "blur");
    g_psBloomExtract = makePs(dev, kPsBloomExtract, "bloomExtract");
    g_psGrade = makePs(dev, kPsGrade, "grade");
    g_psBloomComp = makePs(dev, kPsBloomComp, "bloomComp");
    g_psFxaa = makePs(dev, kPsFxaa, "fxaa");
    g_psFinish = makePs(dev, kPsFinish, "finish");
    g_psCas = makePs(dev, kPsCas, "cas");
    g_psLinDepth = makePs(dev, kPsLinDepth, "linDepth");
    g_psNormals = makePs(dev, kPsNormals, "normals");
    g_psMxao = makePs(dev, kPsMxao, "mxao");
    g_psSsr = makePsCached(dev, kPsSsr, "ssr", L"ssr_ps30.bin", kSsrCacheVersion);
    g_psSsrBlur = makePs(dev, kPsSsrBlur, "ssrBlur");
    g_ssrState.store(g_psSsr ? 1 : 2);
    g_psGi = makePs(dev, kPsGi, "gi");
    g_psFakeHdr = makePsFile(dev, L"fake_hdr.hlsl", "fakeHdr");
    g_psEyeAdapt = makePsFile(dev, L"eye_adapt.hlsl", "eyeAdapt");
    g_psAlExtract = makePsFile(dev, L"al_extract.hlsl", "alExtract");
    g_psAmbient = makePsFile(dev, L"ambient_light.hlsl", "ambientLight");
    if (!g_psCopy || !g_psDown || !g_psBlur || !g_psBloomExtract || !g_psGrade || !g_psBloomComp ||
        !g_psFxaa || !g_psFinish || !g_psCas) {
        g_failed = true;
        return false;
    }
    if (!g_psLinDepth || !g_psNormals || !g_psMxao || !g_psGi) {
        logf("postfx: depth-effect shaders incomplete (AO/SSR/GI may be skipped)");
    }
    if (!g_psSsr) {
        logf("postfx: SSR unavailable");
    }
    if (!g_psSsrBlur) {
        logf("postfx: SSR blur shader missing — reflections will be sharper/noisier");
    }
    if (!g_psFakeHdr || !g_psEyeAdapt || !g_psAlExtract || !g_psAmbient) {
        logf("postfx: remaster disk shaders incomplete (fakeHdr=%d eyeAdapt=%d al=%d ambient=%d)",
             (int)!!g_psFakeHdr, (int)!!g_psEyeAdapt, (int)!!g_psAlExtract, (int)!!g_psAmbient);
    }
    const D3DVERTEXELEMENT9 el[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()};
    if (FAILED(dev->CreateVertexDeclaration(el, &g_decl))) {
        g_failed = true;
        return false;
    }
    if (FAILED(dev->CreateVertexBuffer(sizeof(Vert) * 3, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &g_vb,
                                       nullptr))) {
        g_failed = true;
        return false;
    }
    Vert* v = nullptr;
    if (SUCCEEDED(g_vb->Lock(0, 0, (void**)&v, 0))) {
        v[0] = {-1.f, 1.f, 0.f, 1.f, 0.f, 0.f};
        v[1] = {3.f, 1.f, 0.f, 1.f, 2.f, 0.f};
        v[2] = {-1.f, -3.f, 0.f, 1.f, 0.f, 2.f};
        g_vb->Unlock();
    }
    g_ready = true;
    logf("postfx multipass ready quality=%d ssr=%d fakeHdr=%d eyeAdapt=%d ambient=%d",
         (int)config().quality, g_psSsr ? 1 : 0, g_psFakeHdr ? 1 : 0, g_psEyeAdapt ? 1 : 0,
         g_psAmbient ? 1 : 0);
    return true;
}

struct StateSaver {
    IDirect3DDevice9* dev = nullptr;
    IDirect3DSurface9* oldRt = nullptr;
    IDirect3DVertexDeclaration9* oldDecl = nullptr;
    IDirect3DVertexShader9* oldVs = nullptr;
    IDirect3DPixelShader9* oldPs = nullptr;
    IDirect3DBaseTexture9* t[10] = {};
    IDirect3DVertexBuffer9* oldVb = nullptr;
    UINT oldOff = 0, oldStride = 0;
    DWORD oldFvf = 0;
    DWORD rs[16] = {};
    D3DVIEWPORT9 oldVp{};
    DWORD samp[10][4] = {};

    void save(IDirect3DDevice9* d) {
        dev = d;
        d->GetRenderTarget(0, &oldRt);
        d->GetFVF(&oldFvf);
        d->GetVertexDeclaration(&oldDecl);
        d->GetVertexShader(&oldVs);
        d->GetPixelShader(&oldPs);
        d->GetStreamSource(0, &oldVb, &oldOff, &oldStride);
        d->GetViewport(&oldVp);
        for (DWORD i = 0; i < 10; ++i) d->GetTexture(i, &t[i]);
        d->GetRenderState(D3DRS_ZENABLE, &rs[0]);
        d->GetRenderState(D3DRS_ZWRITEENABLE, &rs[1]);
        d->GetRenderState(D3DRS_ALPHABLENDENABLE, &rs[2]);
        d->GetRenderState(D3DRS_ALPHATESTENABLE, &rs[3]);
        d->GetRenderState(D3DRS_CULLMODE, &rs[4]);
        d->GetRenderState(D3DRS_FOGENABLE, &rs[5]);
        d->GetRenderState(D3DRS_LIGHTING, &rs[6]);
        d->GetRenderState(D3DRS_STENCILENABLE, &rs[7]);
        d->GetRenderState(D3DRS_SCISSORTESTENABLE, &rs[8]);
        d->GetRenderState(D3DRS_SRGBWRITEENABLE, &rs[9]);
        d->GetRenderState(D3DRS_COLORWRITEENABLE, &rs[10]);
        d->GetRenderState(D3DRS_FILLMODE, &rs[11]);
        for (DWORD i = 0; i < 10; ++i) {
            d->GetSamplerState(i, D3DSAMP_MAGFILTER, &samp[i][0]);
            d->GetSamplerState(i, D3DSAMP_MINFILTER, &samp[i][1]);
            d->GetSamplerState(i, D3DSAMP_ADDRESSU, &samp[i][2]);
            d->GetSamplerState(i, D3DSAMP_ADDRESSV, &samp[i][3]);
        }
    }
    void restore() {
        if (!dev) return;
        if (oldRt) {
            dev->SetRenderTarget(0, oldRt);
            oldRt->Release();
            oldRt = nullptr;
        }
        dev->SetViewport(&oldVp);
        dev->SetStreamSource(0, oldVb, oldOff, oldStride);
        if (oldVb) oldVb->Release();
        for (DWORD i = 0; i < 10; ++i) {
            dev->SetTexture(i, t[i]);
            if (t[i]) t[i]->Release();
        }
        dev->SetVertexShader(oldVs);
        dev->SetPixelShader(oldPs);
        dev->SetVertexDeclaration(oldDecl);
        dev->SetFVF(oldFvf);
        if (oldVs) oldVs->Release();
        if (oldPs) oldPs->Release();
        if (oldDecl) oldDecl->Release();
        dev->SetRenderState(D3DRS_ZENABLE, rs[0]);
        dev->SetRenderState(D3DRS_ZWRITEENABLE, rs[1]);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, rs[2]);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, rs[3]);
        dev->SetRenderState(D3DRS_CULLMODE, rs[4]);
        dev->SetRenderState(D3DRS_FOGENABLE, rs[5]);
        dev->SetRenderState(D3DRS_LIGHTING, rs[6]);
        dev->SetRenderState(D3DRS_STENCILENABLE, rs[7]);
        dev->SetRenderState(D3DRS_SCISSORTESTENABLE, rs[8]);
        dev->SetRenderState(D3DRS_SRGBWRITEENABLE, rs[9]);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE, rs[10]);
        dev->SetRenderState(D3DRS_FILLMODE, rs[11]);
        for (DWORD i = 0; i < 10; ++i) {
            dev->SetSamplerState(i, D3DSAMP_MAGFILTER, samp[i][0]);
            dev->SetSamplerState(i, D3DSAMP_MINFILTER, samp[i][1]);
            dev->SetSamplerState(i, D3DSAMP_ADDRESSU, samp[i][2]);
            dev->SetSamplerState(i, D3DSAMP_ADDRESSV, samp[i][3]);
        }
    }
};

void beginPassState(IDirect3DDevice9* dev) {
    dev->SetFVF(0);
    dev->SetVertexDeclaration(g_decl);
    dev->SetVertexShader(g_vs);
    dev->SetStreamSource(0, g_vb, 0, sizeof(Vert));
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    dev->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
}

void setLinearClamp(IDirect3DDevice9* dev, DWORD stage, bool point) {
    const DWORD f = point ? D3DTEXF_POINT : D3DTEXF_LINEAR;
    dev->SetSamplerState(stage, D3DSAMP_MAGFILTER, f);
    dev->SetSamplerState(stage, D3DSAMP_MINFILTER, f);
    dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}

void drawTo(IDirect3DDevice9* dev, IDirect3DTexture9* rtTex, IDirect3DPixelShader9* ps,
            IDirect3DTexture9* t0, IDirect3DTexture9* t1 = nullptr, IDirect3DTexture9* t2 = nullptr,
            IDirect3DTexture9* t3 = nullptr, IDirect3DTexture9* t4 = nullptr,
            IDirect3DTexture9* t5 = nullptr, IDirect3DTexture9* t6 = nullptr,
            IDirect3DTexture9* t7 = nullptr, IDirect3DTexture9* t8 = nullptr,
            IDirect3DTexture9* t9 = nullptr) {
    IDirect3DSurface9* surf = nullptr;
    if (FAILED(rtTex->GetSurfaceLevel(0, &surf)) || !surf) return;
    D3DSURFACE_DESC desc{};
    surf->GetDesc(&desc);
    D3DVIEWPORT9 vp{0, 0, desc.Width, desc.Height, 0.f, 1.f};
    dev->SetRenderTarget(0, surf);
    dev->SetViewport(&vp);
    dev->SetPixelShader(ps);
    IDirect3DTexture9* slots[10] = {t0, t1, t2, t3, t4, t5, t6, t7, t8, t9};
    for (DWORD i = 0; i < 10; ++i) {
        dev->SetTexture(i, nullptr);
        if (slots[i]) {
            dev->SetTexture(i, slots[i]);
            // s0 scene/pointy; s1+ color overlays linear
            setLinearClamp(dev, i, i != 0);
        }
    }
    dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    for (DWORD i = 0; i < 10; ++i) dev->SetTexture(i, nullptr);
    surf->Release();
}

void drawToSurface(IDirect3DDevice9* dev, IDirect3DSurface9* surf, UINT w, UINT h,
                   IDirect3DPixelShader9* ps, IDirect3DTexture9* t0,
                   IDirect3DTexture9* t1 = nullptr) {
    D3DVIEWPORT9 vp{0, 0, w, h, 0.f, 1.f};
    dev->SetRenderTarget(0, surf);
    dev->SetViewport(&vp);
    dev->SetPixelShader(ps);
    for (DWORD i = 0; i < 6; ++i) dev->SetTexture(i, nullptr);
    if (t0) {
        dev->SetTexture(0, t0);
        setLinearClamp(dev, 0, false);
    }
    if (t1) {
        dev->SetTexture(1, t1);
        setLinearClamp(dev, 1, false);
    }
    dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
}

void setC(IDirect3DDevice9* dev, int i, float x, float y, float z, float w) {
    float v[4] = {x, y, z, w};
    dev->SetPixelShaderConstantF(i, v, 1);
}

// Unbind INTZ while sampling it as a texture; restore after. Do NOT leave DS null
// across the whole Present — that black-screened on this title before.
struct DepthSampleScope {
    IDirect3DDevice9* dev = nullptr;
    IDirect3DSurface9* oldDs = nullptr;

    explicit DepthSampleScope(IDirect3DDevice9* d) : dev(d) {
        if (!dev) {
            return;
        }
        dev->GetDepthStencilSurface(&oldDs);
        dev->SetDepthStencilSurface(nullptr);
    }

    ~DepthSampleScope() {
        if (!dev) {
            return;
        }
        dev->SetDepthStencilSurface(oldDs);
        if (oldDs) {
            oldDs->Release();
            oldDs = nullptr;
        }
    }

    DepthSampleScope(const DepthSampleScope&) = delete;
    DepthSampleScope& operator=(const DepthSampleScope&) = delete;
};

}  // namespace

void postfxOnCreateDevice(IDirect3DDevice9* device) {
    // Native INTZ depth only when our postfx is active — ReShade needs stock depth.
    if (!config().visuals) {
        logf("postfx: Visuals=0 — skipping INTZ depth hooks (ReShade-safe)");
        return;
    }
    depthOnDeviceHooked(device);
    depthCaptureReplace(device);
}

void postfxOnLostDevice() {
    if (!config().visuals) {
        releaseDeviceObjects();
        return;
    }
    releaseDeviceObjects();
    depthOnLostDevice();
}

void postfxOnResetDevice(IDirect3DDevice9* device) {
    g_failed = false;
    if (!config().visuals) {
        return;
    }
    depthOnResetDevice(device);
    depthCaptureReplace(device);
}

void postfxOnPresent(IDirect3DDevice9* device) {
    if (!config().visuals || config().visualStrength <= 0.001f || !device) return;
    if (g_failed) return;
    if (!g_ready && !initGpu(device)) return;

    // After Reset, shaders were released — recreate (SSR hits disk cache, near-instant).
    if (!g_psSsr && g_ssrState.load() != 2) {
        g_psSsr = makePsCached(device, kPsSsr, "ssr", L"ssr_ps30.bin", kSsrCacheVersion);
        g_ssrState.store(g_psSsr ? 1 : 2);
    }

    IDirect3DSurface9* bb = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC desc{};
    bb->GetDesc(&desc);
    if (!ensureRts(device, desc.Width, desc.Height)) {
        bb->Release();
        return;
    }
    tryLoadLuts(device);

    IDirect3DSurface9* sceneSurf = nullptr;
    if (FAILED(g_scene->GetSurfaceLevel(0, &sceneSurf)) || !sceneSurf) {
        bb->Release();
        return;
    }
    if (FAILED(device->StretchRect(bb, nullptr, sceneSurf, nullptr, D3DTEXF_NONE))) {
        static bool once = false;
        if (!once) {
            once = true;
            logf("postfx StretchRect failed");
        }
        sceneSurf->Release();
        bb->Release();
        return;
    }
    sceneSurf->Release();

    StateSaver st;
    st.save(device);
    beginPassState(device);

    const VisualQuality q = config().quality;
    const bool wantDepth = q != VisualQuality::ColorOnly;
    const bool wantAo = q >= VisualQuality::Balanced;
    const bool wantSsr = q >= VisualQuality::Cinema;
    const bool wantGi = q >= VisualQuality::Cinema;
    const bool haveDepth = wantDepth && depthAvailable() && g_psLinDepth && g_psNormals;
    const bool msaa = desc.MultiSampleType != D3DMULTISAMPLE_NONE;

    g_time += 0.016f;
    if (g_time > 1000.f) g_time = 0.f;

    auto copyToScene = [&](IDirect3DTexture9* src) {
        if (src == g_scene) {
            return;
        }
        drawTo(device, g_scene, g_psCopy, src);
    };

    // --- FakeHDR / EyeAdapt / AmbientLight: skip on menus (no INTZ → odd color casts) ---
    const bool inWorld = depthAvailable();

    // --- FakeHDR (Remastered: HDRPower=1.109) — separate pass, keep moderate amount ---
    if (inWorld && config().fakeHdrAmount > 0.001f && g_psFakeHdr) {
        setC(device, 0, 1.f / g_w, 1.f / g_h, config().fakeHdrAmount, 1.109f);
        drawTo(device, g_workA, g_psFakeHdr, g_scene);
        copyToScene(g_workA);
    }

    // --- Adapt pyramid (64x64) for EyeAdapt + AmbientLight ---
    if (inWorld && (config().eyeAdapt || config().ambientLight) && g_adapt && g_psDown) {
        setC(device, 0, 1.f / g_w, 1.f / g_h, 0.f, 0.f);
        drawTo(device, g_adapt, g_psDown, g_scene);
        // Extra blur so center sample is stable
        if (g_psBlur && g_bloomTmp) {
            setC(device, 0, 1.5f / 64.f, 0.f, 1.f, 0.f);
            drawTo(device, g_bloomTmp, g_psBlur, g_adapt);
            setC(device, 0, 0.f, 1.5f / 64.f, 1.f, 0.f);
            drawTo(device, g_adapt, g_psBlur, g_bloomTmp);
        }
    }

    // --- EyeAdapt (BloomingHDR Auto_Exposure) ---
    if (inWorld && config().eyeAdapt && g_psEyeAdapt && g_adapt) {
        setC(device, 0, 0.45f, 0.128f, 0.78f, 1.28f);  // milder — avoid menu/sky freakouts
        drawTo(device, g_workA, g_psEyeAdapt, g_scene, g_adapt);
        copyToScene(g_workA);
    }

    // --- AmbientLight (Remastered: DirtOVR/OVB + LensDB/DB2/DOV/DUV) ---
    if (inWorld && config().ambientLight && g_psAlExtract && g_psAmbient && g_alBright) {
        setC(device, 0, 0.18f, 0.08f, 0.f, 0.f);
        drawTo(device, g_alBright, g_psAlExtract, g_scene);
        if (g_psBlur && g_bloomTmp) {
            D3DSURFACE_DESC bd{};
            IDirect3DSurface9* s = nullptr;
            g_alBright->GetSurfaceLevel(0, &s);
            if (s) {
                s->GetDesc(&bd);
                s->Release();
            }
            const float blurPx = 2.0f;
            setC(device, 0, blurPx / (float)bd.Width, 0.f, 1.f, 0.f);
            drawTo(device, g_bloomTmp, g_psBlur, g_alBright);
            setC(device, 0, 0.f, blurPx / (float)bd.Height, 1.f, 0.f);
            drawTo(device, g_alBright, g_psBlur, g_bloomTmp);
        }
        const bool haveDirt = g_dirtOvr && g_dirtOvb;
        const bool haveLens = g_lensDb && g_lensDb2 && g_lensDov && g_lensDuv;
        // gP0: alInt(mix), alDirtInt, alDirtOVInt, alAdapt — Remastered scaled
        setC(device, 0, 0.55f, 1.0f, 1.0f, 0.70f);
        // gP1: alLensInt, alLensThresh, AL_DirtTex=0, AL_Lens
        setC(device, 1, 2.0f, 0.50f, 0.0f, haveLens ? 1.0f : 0.0f);
        if (haveDirt) {
            drawTo(device, g_workA, g_psAmbient, g_scene, g_alBright, g_adapt,
                   g_dirt ? g_dirt : g_scene, g_dirtOvr, g_dirtOvb,
                   g_lensDb ? g_lensDb : g_scene, g_lensDb2 ? g_lensDb2 : g_scene,
                   g_lensDov ? g_lensDov : g_scene, g_lensDuv ? g_lensDuv : g_scene);
            copyToScene(g_workA);
        } else {
            logf("postfx: AmbientLight dirt overlays missing — AL skipped");
        }
    }

    // --- Depth passes (half-res) ---
    bool aoReady = false;
    bool ssrReady = false;
    if (haveDepth) {
        float dp[4];
        depthLinearParams(dp);
        setC(device, 0, dp[0], dp[1], dp[2], dp[3]);
        {
            DepthSampleScope unbind(device);
            drawTo(device, g_linDepth, g_psLinDepth, depthTexture());
        }
        D3DSURFACE_DESC ld{};
        IDirect3DSurface9* s = nullptr;
        g_linDepth->GetSurfaceLevel(0, &s);
        if (s) {
            s->GetDesc(&ld);
            s->Release();
        }
        setC(device, 0, 1.f / ld.Width, 1.f / ld.Height, config().depthFar, 0.f);
        drawTo(device, g_normals, g_psNormals, g_linDepth);
        if (wantAo && g_psMxao) {
            // Stronger MXAO toward Remastered RTGI AO feel
            setC(device, 0, 1.f / ld.Width, 1.f / ld.Height, 1.85f, 3.35f);
            setC(device, 1, config().depthFar, 12.f, 0.f, 0.f);
            drawTo(device, g_ao, g_psMxao, g_linDepth, g_normals);
            aoReady = true;
        }
    }

    if (wantSsr && haveDepth && g_psSsr) {
        D3DSURFACE_DESC sd{};
        IDirect3DSurface9* ss = nullptr;
        g_ssr->GetSurfaceLevel(0, &ss);
        if (ss) {
            ss->GetDesc(&sd);
            ss->Release();
        }
        // Tuned toward qUINT SSR preset (fade/intensity)
        setC(device, 0, 1.f / (float)sd.Width, 1.f / (float)sd.Height, 1.70f, 0.74f);
        setC(device, 1, 3.0f, 0.f, 0.65f, 0.f);
        drawTo(device, g_ssr, g_psSsr, g_scene, g_linDepth, g_normals);
        if (g_psSsrBlur && g_ssrTmp) {
            setC(device, 0, 1.0f / (float)sd.Width, 0.f, 0.f, 0.f);
            drawTo(device, g_ssrTmp, g_psSsrBlur, g_ssr);
            setC(device, 0, 0.f, 1.0f / (float)sd.Height, 0.f, 0.f);
            drawTo(device, g_ssr, g_psSsrBlur, g_ssrTmp);
        }
        ssrReady = true;
    }

    const float unsharp = (std::min)(1.f, config().unsharp + (msaa ? 0.06f : 0.f));
    const float clarity = (std::min)(1.f, config().clarity + (msaa ? 0.04f : 0.f));
    const float casAmt =
        (std::min)(1.f, config().casSharpen + (msaa ? 0.14f : 0.04f));
    // FakeHDR already applied as standalone pass
    const float fakeHdr = 0.f;
    // Remastered: Cinetools Intensity=0.591 Sel=7; Bonus Intensity=0.915 Sel=1
    // BW remap left off (ob red blacks caused menu cast); textures used correctly
    const float lutA = g_lutA ? 0.55f : 0.f;
    const float lutB = g_lutB ? 0.18f : 0.f;
    const float vibrance = 0.42f;
    const float dpxStr = 0.20f;  // DPX Strength preset
    const float contrast = 1.18f;
    const float exposure = 1.0f;
    const bool useSsr = ssrReady;
    const bool useAo = aoReady;
    const bool useGi = wantGi && haveDepth && aoReady && g_psGi;
    {
        static int lastKey = -1;
        const int key = ((int)msaa) | ((int)depthAvailable() << 1) | ((int)useAo << 2) |
                        ((int)useSsr << 3) | ((int)useGi << 4) | ((int)q << 8) |
                        ((int)config().ambientLight << 12) | ((int)config().eyeAdapt << 13);
        if (key != lastKey) {
            lastKey = key;
            logf("postfx status quality=%d msaa=%d ao=%d ssr=%d gi=%d cas=%.2f bloom=%d "
                 "fakeHdrAmt=%.2f eyeAdapt=%d ambient=%d deband=%d",
                 (int)q, (int)msaa, (int)useAo, (int)useSsr, (int)useGi, casAmt,
                 (int)config().bloom, config().fakeHdrAmount, (int)config().eyeAdapt,
                 (int)config().ambientLight, (int)config().deband);
            if (wantDepth && !depthAvailable()) {
                if (msaa) {
                    logf("postfx: depth effects offline (FSAA/MSAA On — turn Off for AO/SSR/GI)");
                } else {
                    logf("postfx: depth effects offline (no samplable depth yet)");
                }
            } else if (useSsr) {
                logf("postfx: SSR active");
            }
        }
    }

    // --- Grade (Dehaze/Unsharp/Clarity/DPX/LUT/AO/SSR) — FakeHDR already done ---
    float dparams[4];
    depthLinearParams(dparams);
    setC(device, 0, 1.f / g_w, 1.f / g_h, config().visualStrength, 1.109f);
    setC(device, 1, lutA, lutB, unsharp, clarity);
    setC(device, 2, 7.f, 31.f, 1.f, 50.f);
    // DPX Remastered: Colorfulness=2.5, Contrast=0.1, Saturation=3.0, Strength=0.2
    setC(device, 3, 2.5f, 0.10f, 3.0f, dpxStr);
    setC(device, 4, fakeHdr, contrast, exposure, vibrance);
    const float dehaze = haveDepth ? 0.296f : 0.14f;  // Dehaze Alpha preset
    const float aoMix = useAo ? (0.82f * config().aoAmount) : 0.f;
    setC(device, 5, dehaze, 0.175f, aoMix, useSsr ? 1.0f : 0.f);
    setC(device, 6, dparams[0], dparams[1], dparams[2], dparams[3]);
    setC(device, 7, 0.88f, haveDepth ? 1.f : 0.f, useAo ? 1.f : 0.f, useSsr ? 1.f : 0.f);
    // shadowCrush, highlightSoft, warmBias=0, aerialHaze — warmBias reddened UI
    const float aerial = haveDepth ? 0.22f : 0.f;
    setC(device, 8, 0.28f, 0.36f, 0.0f, aerial);

    {
        DepthSampleScope unbind(haveDepth && depthTexture() ? device : nullptr);
        IDirect3DSurface9* out = nullptr;
        g_workA->GetSurfaceLevel(0, &out);
        D3DVIEWPORT9 vp{0, 0, g_w, g_h, 0.f, 1.f};
        device->SetRenderTarget(0, out);
        device->SetViewport(&vp);
        device->SetPixelShader(g_psGrade);
        for (DWORD i = 0; i < 6; ++i) device->SetTexture(i, nullptr);
        device->SetTexture(0, g_scene);
        setLinearClamp(device, 0, false);
        if (g_lutA) {
            device->SetTexture(1, g_lutA);
            setLinearClamp(device, 1, true);
        }
        if (g_lutB) {
            device->SetTexture(2, g_lutB);
            setLinearClamp(device, 2, true);
        }
        if (haveDepth && depthTexture()) {
            device->SetTexture(3, depthTexture());
            setLinearClamp(device, 3, false);
        }
        if (useAo) {
            device->SetTexture(4, g_ao);
            setLinearClamp(device, 4, false);
        }
        if (useSsr) {
            device->SetTexture(5, g_ssr);
            setLinearClamp(device, 5, false);
        }
        device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
        out->Release();
    }

    IDirect3DTexture9* graded = g_workA;
    if (useGi) {
        setC(device, 0, 1.f / g_w, 1.f / g_h, config().giAmount, 0.f);
        drawTo(device, g_workB, g_psGi, g_workA, g_ao, g_linDepth);
        graded = g_workB;
    }

    IDirect3DTexture9* afterBloom = graded;
    // Sharp bloom tuned toward PD80 BloomLimit/Mix without wide smear
    if (config().bloom) {
        setC(device, 0, 0.333f, 0.06f, 1.15f, 2.0f);  // BloomLimit-ish threshold
        drawTo(device, g_bloom[0], g_psBloomExtract, graded);

        {
            D3DSURFACE_DESC bd{};
            IDirect3DSurface9* s = nullptr;
            g_bloom[0]->GetSurfaceLevel(0, &s);
            if (s) {
                s->GetDesc(&bd);
                s->Release();
            }
            const float blurPx = 0.75f;
            setC(device, 0, blurPx / (float)bd.Width, 0.f, 1.f, 0.f);
            drawTo(device, g_bloomTmp, g_psBlur, g_bloom[0]);
            setC(device, 0, 0.f, blurPx / (float)bd.Height, 1.f, 0.f);
            drawTo(device, g_bloom[0], g_psBlur, g_bloomTmp);

            setC(device, 0, 1.f / (float)bd.Width, 1.f / (float)bd.Height, 0.f, 0.f);
            drawTo(device, g_bloom[1], g_psDown, g_bloom[0]);
        }

        afterBloom = graded == g_workA ? g_workB : g_workA;
        setC(device, 0, 0.32f, 1.45f, 1.35f, 0.f);  // ~BloomMix
        drawTo(device, afterBloom, g_psBloomComp, graded, g_bloom[0]);
        {
            IDirect3DTexture9* tmp = (afterBloom == g_workA) ? g_workB : g_workA;
            setC(device, 0, 0.08f, 1.40f, 1.50f, 0.f);
            drawTo(device, tmp, g_psBloomComp, afterBloom, g_bloom[1]);
            afterBloom = tmp;
        }
    }

    IDirect3DTexture9* afterCas = (afterBloom == g_workA) ? g_workB : g_workA;
    setC(device, 0, 1.f / g_w, 1.f / g_h, casAmt, 0.f);
    drawTo(device, afterCas, g_psCas, afterBloom);

    IDirect3DTexture9* afterAa = afterCas;
    if (config().fxaa && g_psFxaa) {
        const float blend = msaa ? 0.32f : 0.42f;
        afterAa = (afterCas == g_workA) ? g_workB : g_workA;
        setC(device, 0, 1.f / g_w, 1.f / g_h, blend, 0.f);
        drawTo(device, afterAa, g_psFxaa, afterCas);
    }

    setC(device, 0, 1.f / g_w, 1.f / g_h, 0.0f, 0.f);
    setC(device, 1, config().deband ? 0.45f : 0.0f, 0.10f, 0.f, 0.f);
    drawToSurface(device, bb, g_w, g_h, g_psFinish, afterAa);

    st.restore();
    bb->Release();
}

}  // namespace sm3spectacular
