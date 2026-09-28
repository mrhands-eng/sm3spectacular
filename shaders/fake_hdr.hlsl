// Standalone PS_3_0 — FakeHDR (CeeJay) tuned to Remastered: HDRPower=1.109, r1=0.793, r2=0.870
sampler2D s0 : register(s0);
float4 gP : register(c0); // px.xy, amount, HDRPower

float4 main(float2 uv : TEXCOORD0) : COLOR {
  float2 px = gP.xy;
  float amount = gP.z;
  float HDRPower = gP.w;
  float3 color = tex2D(s0, uv).rgb;
  if (amount < 0.001) return float4(color, 1);

  float r1 = 0.793, r2 = 0.870;
  float3 s1 =
    tex2D(s0, uv+float2( 1.5,-1.5)*r1*px).rgb +
    tex2D(s0, uv+float2(-1.5,-1.5)*r1*px).rgb +
    tex2D(s0, uv+float2( 1.5, 1.5)*r1*px).rgb +
    tex2D(s0, uv+float2(-1.5, 1.5)*r1*px).rgb +
    tex2D(s0, uv+float2( 0.0,-2.5)*r1*px).rgb +
    tex2D(s0, uv+float2( 0.0, 2.5)*r1*px).rgb +
    tex2D(s0, uv+float2(-2.5, 0.0)*r1*px).rgb +
    tex2D(s0, uv+float2( 2.5, 0.0)*r1*px).rgb;
  s1 *= 0.005;
  float3 s2 =
    tex2D(s0, uv+float2( 1.5,-1.5)*r2*px).rgb +
    tex2D(s0, uv+float2(-1.5,-1.5)*r2*px).rgb +
    tex2D(s0, uv+float2( 1.5, 1.5)*r2*px).rgb +
    tex2D(s0, uv+float2(-1.5, 1.5)*r2*px).rgb +
    tex2D(s0, uv+float2( 0.0,-2.5)*r2*px).rgb +
    tex2D(s0, uv+float2( 0.0, 2.5)*r2*px).rgb +
    tex2D(s0, uv+float2(-2.5, 0.0)*r2*px).rgb +
    tex2D(s0, uv+float2( 2.5, 0.0)*r2*px).rgb;
  s2 *= 0.010;
  float dist = r2 - r1;
  float3 HDR = (color + (s2 - s1)) * dist;
  float3 blend = HDR + color;
  float3 hdrCol = saturate(pow(abs(blend), abs(HDRPower)) + HDR);
  color = lerp(color, hdrCol, amount);
  return float4(saturate(color), 1);
}
