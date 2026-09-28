// Quick offline check: does D3DXCompileShader hang on our SSR HLSL?
#include <Windows.h>
#include <cstdio>
#include <cstring>

using D3DXCompileShader_t = HRESULT(WINAPI*)(LPCSTR, UINT, const void*, const void*, LPCSTR,
                                             LPCSTR, DWORD, void**, void**, void*);

static const char kPsSsr[] = R"(
sampler2D sScene : register(s0);
sampler2D sDepth : register(s1);
sampler2D sNormal : register(s2);
float4 gPx : register(c0);
float4 gP1 : register(c1);
float4 main(float2 uv : TEXCOORD0) : COLOR {
  float depth = tex2D(sDepth, uv).r;
  if (depth < 0.02 || depth > gPx.w) return float4(0,0,0,0);
  float3 n = tex2D(sNormal, uv).xyz * 2.0 - 1.0;
  float2 ndc = uv * 2.0 - 1.0;
  float3 V = normalize(float3(ndc * float2(0.84, 0.60), 1.0));
  float3 R = reflect(V, normalize(n));
  if (R.z < 0.05) return float4(0,0,0,0);
  float3 albedo = tex2D(sScene, uv).rgb;
  float luma = dot(albedo, float3(0.299,0.587,0.114));
  float gloss = saturate(0.35 + (1.0 - luma) * 0.45 + saturate(n.y) * 0.35);
  float2 ray = uv;
  float2 stepUV = (R.xy / (R.z + 1e-3)) * gPx.xy * gP1.x;
  float3 hit = 0;
  float alpha = 0;
  float d = depth;
  for (int i = 0; i < 16; ++i) {
    ray += stepUV * (0.35 + 0.15 * float(i));
    if (ray.x < 0 || ray.x > 1 || ray.y < 0 || ray.y > 1) break;
    float sd = tex2D(sDepth, ray).r;
    if (sd < d - gP1.y * 0.002) continue;
    if (sd < d + gP1.y * 0.01 && sd > 0.001) {
      hit = tex2D(sScene, ray).rgb;
      float edge = saturate(1.0 - abs(ray.x - 0.5) * 2.0) *
                   saturate(1.0 - abs(ray.y - 0.5) * 2.0);
      float fres = pow(1.0 - saturate(dot(-V, n)), 3.4);
      alpha = fres * gloss * edge * gPx.z * saturate(1.0 - depth / gPx.w);
      break;
    }
    d = min(d, sd);
  }
  return float4(hit, alpha);
}
)";

int main() {
    HMODULE m = LoadLibraryA("d3dx9_43.dll");
    if (!m) m = LoadLibraryA("d3dx9_42.dll");
    if (!m) {
        printf("no d3dx\n");
        return 1;
    }
    auto p = reinterpret_cast<D3DXCompileShader_t>(GetProcAddress(m, "D3DXCompileShader"));
    if (!p) {
        printf("no D3DXCompileShader\n");
        return 1;
    }
    printf("compiling...\n");
    fflush(stdout);
    void* code = nullptr;
    void* errs = nullptr;
    DWORD t0 = GetTickCount();
    HRESULT hr = p(kPsSsr, (UINT)strlen(kPsSsr), nullptr, nullptr, "main", "ps_3_0", 0, &code, &errs,
                   nullptr);
    DWORD dt = GetTickCount() - t0;
    printf("hr=0x%08lX ms=%lu code=%p\n", (unsigned long)hr, (unsigned long)dt, code);
    if (errs) {
        // ID3DXBuffer GetBufferPointer at vtable[3] typically — just free via Release
        using Rel = ULONG(STDMETHODCALLTYPE*)(void*);
        auto* vt = *reinterpret_cast<void***>(errs);
        reinterpret_cast<Rel>(vt[2])(errs);
    }
    if (code) {
        auto* vt = *reinterpret_cast<void***>(code);
        using Rel = ULONG(STDMETHODCALLTYPE*)(void*);
        reinterpret_cast<Rel>(vt[2])(code);
    }
    return FAILED(hr) ? 2 : 0;
}
