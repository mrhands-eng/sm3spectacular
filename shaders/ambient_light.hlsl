// AmbientLight composite — TeaserPlay Remastered (AmbientLight.fx PS_AL_Magic).
// Textures: Dirt / DirtOVR / DirtOVB / LensDB / LensDB2 / LensDOV / LensDUV
// Preset: AL_Dirt=1, AL_DirtTex=0, AL_Adaptive=2, AL_Lens=1
sampler2D sScene   : register(s0);
sampler2D sBright  : register(s1);
sampler2D sAdapt   : register(s2);
sampler2D sDirt    : register(s3);
sampler2D sDirtOVR : register(s4);
sampler2D sDirtOVB : register(s5);
sampler2D sLensDB  : register(s6);
sampler2D sLensDB2 : register(s7);
sampler2D sLensDOV : register(s8);
sampler2D sLensDUV : register(s9);
// x=alInt(mix), y=alDirtInt, z=alDirtOVInt, w=alAdapt
float4 gP0 : register(c0);
// x=alLensInt, y=alLensThresh, z=dirtTex(0/1), w=doLens(0/1)
float4 gP1 : register(c1);

float4 main(float2 uv : TEXCOORD0) : COLOR {
  float3 base = tex2D(sScene, uv).rgb;
  float4 high = tex2D(sBright, uv);
  float3 adaptC = tex2D(sAdapt, float2(0.5, 0.5)).rgb;

  float low = sqrt(0.241 * adaptC.r * adaptC.r + 0.691 * adaptC.g * adaptC.g +
                   0.068 * adaptC.b * adaptC.b);
  low = pow(low * 1.25, 2.0);
  float adapt = low * (low + 1.0) * gP0.w * gP0.x * 0.35;

  high = min(high, float4(0.0325, 0.0325, 0.0325, 1.0)) * 1.15;
  float4 highOrig = high;

  float2 flip = 1.0 - uv;
  float4 highFlipOrig = tex2D(sBright, flip);
  highFlipOrig = min(highFlipOrig, float4(0.03, 0.03, 0.03, 1.0)) * 1.15;
  float4 highFlip = highFlipOrig;
  float4 highLensSrc = high;

  // --- Dirt (Remastered AL_Dirt + AL_Adaptive=2) ---
  {
    float4 dirt = tex2D(sDirt, uv);
    float4 dirtOVR = tex2D(sDirtOVR, uv);
    float4 dirtOVB = tex2D(sDirtOVB, uv);

    float maxhigh = max(high.r, max(high.g, high.b));
    float threshDiff = maxhigh - 3.2;
    if (threshDiff > 0) {
      high.rgb = (high.rgb / maxhigh) * 3.2;
    }

    // AL_DirtTex=0 → highOrig * high * alDirtInt (Dirt.png unused as multiply)
    // keep dirt sample available if dirtTex flag set
    float4 highDirt = (gP1.z > 0.5)
                        ? highOrig * dirt * gP0.y
                        : highOrig * high * gP0.y;

    float highMix = max(highOrig.r + highOrig.g + highOrig.b, 1e-5);
    float red = highOrig.r / highMix;
    float green = highOrig.g / highMix;
    float blue = highOrig.b / highMix;
    highOrig = highOrig + highDirt;

    // AL_Adaptive == 2
    high = high + high * dirtOVR * gP0.z * green;
    high = high + highDirt;
    high = high + highOrig * dirtOVB * gP0.z * blue;
    high = high + highOrig * dirtOVR * gP0.z * red;

    highLensSrc = high * 85.0 * pow(1.25 - (abs(uv.x - 0.5) + abs(uv.y - 0.5)), 2.0);
  }

  // --- Lens (Remastered AL_Lens) ---
  if (gP1.w > 0.5) {
    float origBright = max(highLensSrc.r, max(highLensSrc.g, highLensSrc.b));
    float maxOrig = max((1.8 * gP1.y) - pow(origBright * (0.5 - abs(uv.x - 0.5)), 4.0), 0.0);
    float smartWeight = maxOrig * max(abs(flip.x - 0.5), 0.3 * abs(flip.y - 0.5)) *
                        (2.2 - 1.2 * abs(flip.x - 0.5)) * gP1.x;
    smartWeight = min(0.85, max(0.0, smartWeight - adapt));

    float4 lensDB = tex2D(sLensDB, uv);
    float4 lensDB2 = tex2D(sLensDB2, uv);
    float4 lensDOV = tex2D(sLensDOV, uv);
    float4 lensDUV = tex2D(sLensDUV, uv);

    float4 highLens = highFlip * lensDB * 0.7 * smartWeight;
    high += highLens;

    highLens = highFlipOrig * lensDUV * 1.15 * smartWeight;
    highFlipOrig += highLens;
    high += highLens;

    highLens = highFlipOrig * lensDB2 * 0.7 * smartWeight;
    highFlipOrig += highLens;
    high += highLens;

    highLens = highFlipOrig * lensDOV * 1.15 * smartWeight * 0.5 +
               highFlipOrig * smartWeight * 0.5;
    high += highLens;
  }

  // Milder screen-blend than stock alInt=8 (keeps remaster look without washout)
  float mixAmt = saturate(gP0.x - adapt);
  base *= max(0.0, 1.0 - adapt * 0.35);
  float3 screen = 1.0 - (1.0 - base) * (1.0 - saturate(high.rgb));
  float3 outc = lerp(base, screen, mixAmt);
  return float4(saturate(outc), 1);
}
