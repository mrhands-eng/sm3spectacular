// Standalone PS_3_0 — eye adaptation apply (BloomingHDR Auto_Exposure spirit).
// s0=scene, s1=lumaDown (tiny RT). Smooth exposure toward mid-grey.
sampler2D sScene : register(s0);
sampler2D sAdapt : register(s1);
float4 gP : register(c0); // strength, targetGrey, minExp, maxExp

float4 main(float2 uv : TEXCOORD0) : COLOR {
  float3 c = tex2D(sScene, uv).rgb;
  float3 a = tex2D(sAdapt, float2(0.5, 0.5)).rgb;
  float luma = dot(a, float3(0.2126, 0.7152, 0.0722));
  luma = max(luma, 1e-3);
  float expv = gP.y / luma;
  expv = clamp(expv, gP.z, gP.w);
  expv = lerp(1.0, expv, gP.x);
  return float4(saturate(c * expv), 1);
}
