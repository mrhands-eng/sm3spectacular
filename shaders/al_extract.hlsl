// Standalone PS_3_0 — bright extract for AmbientLight (threshold then blur externally).
sampler2D s0 : register(s0);
float4 gP : register(c0); // threshold (0..1-ish from alThreshold/100), knee, unused, unused

float4 main(float2 uv : TEXCOORD0) : COLOR {
  float3 c = tex2D(s0, uv).rgb;
  float br = max(max(c.r, c.g), c.b);
  float m = saturate((br - gP.x) / max(gP.y, 1e-4));
  m = m * m;
  return float4(c * m, m);
}
