/**
 * SpectacularFinish.fx — Brand New Day inspired finishing grade
 *
 * Comic-book cinema: rich reds/blues, inky blacks, strong but clean contrast,
 * sunny NYC warmth. Sit LAST after CAS.
 *
 * Inspired by Spider-Man: Brand New Day cinematography notes
 * (saturated suit primaries + deep shadow, contemporary film curve).
 *
 * MIT License — Copyright (c) 2026 zryuyu / SM3 Spectacular Edition
 */

#include "ReShadeUI.fxh"
#include "ReShade.fxh"

uniform float UI_ColorPop <
	ui_type = "slider";
	ui_label = "Color pop";
	ui_tooltip = "Overall vibrance — Brand New Day richness.";
	ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.38;

uniform float UI_HeroChroma <
	ui_type = "slider";
	ui_label = "Hero reds / blues";
	ui_tooltip = "Protect & boost Spidey-suit primaries (comic cover look).";
	ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.48;

uniform float UI_InkBlacks <
	ui_type = "slider";
	ui_label = "Inky blacks";
	ui_tooltip = "Crush shadows like comic ink / BND grade.";
	ui_min = 0.0; ui_max = 0.5; ui_step = 0.01;
> = 0.28;

uniform float UI_Depth <
	ui_type = "slider";
	ui_label = "Contrast depth";
	ui_min = 0.0; ui_max = 0.6; ui_step = 0.01;
> = 0.24;

uniform float UI_SunWarmth <
	ui_type = "slider";
	ui_label = "Sun warmth";
	ui_tooltip = "Golden daylight in highlights.";
	ui_min = 0.0; ui_max = 0.25; ui_step = 0.005;
> = 0.045;

uniform float UI_SkyCool <
	ui_type = "slider";
	ui_label = "Shadow cool";
	ui_tooltip = "Slight blue in shadows for modern cinema split-tone.";
	ui_min = 0.0; ui_max = 0.2; ui_step = 0.005;
> = 0.055;

uniform float UI_Exposure <
	ui_type = "slider";
	ui_label = "Exposure";
	ui_tooltip = "Negative = darker midtones (stops concrete from glowing).";
	ui_min = -0.25; ui_max = 0.15; ui_step = 0.01;
> = -0.06;

uniform float UI_Vignette <
	ui_type = "slider";
	ui_label = "Vignette";
	ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.30;

uniform float UI_Grain <
	ui_type = "slider";
	ui_label = "Film grain";
	ui_min = 0.0; ui_max = 0.06; ui_step = 0.001;
> = 0.0;

uniform bool UI_Letterbox <
	ui_label = "Cinema letterbox (2.39)";
> = false;

uniform float UI_LetterboxAspect <
	ui_type = "slider";
	ui_label = "Letterbox aspect";
	ui_min = 1.77; ui_max = 2.80; ui_step = 0.01;
> = 2.39;

uniform float timer < source = "timer"; >;

float SF_Luma(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float SF_Hash(float2 p)
{
	return frac(sin(dot(p, float2(12.9898, 78.233)) + timer * 0.0001) * 43758.5453);
}

float3 SF_SoftContrast(float3 c, float amount)
{
	float3 x = saturate(c);
	float3 y = x * x * (3.0 - 2.0 * x);
	return lerp(x, y, amount);
}

// Vibrance-style: boost low-sat pixels more than already loud ones.
float3 SF_Vibrance(float3 c, float amount)
{
	float luma = SF_Luma(c);
	float maxc = max(c.r, max(c.g, c.b));
	float minc = min(c.r, min(c.g, c.b));
	float sat = maxc - minc;
	float boost = amount * (1.0 - saturate(sat * 1.8));
	return saturate(luma + (c - luma) * (1.0 + boost * 2.2));
}

// Push reds & blues (hero suit / comic primaries), leave greens calmer.
float3 SF_HeroChroma(float3 c, float amount)
{
	float3 outc = c;
	float r = c.r - max(c.g, c.b);
	float b = c.b - max(c.r, c.g);
	outc.r += saturate(r) * amount * 0.35;
	outc.b += saturate(b) * amount * 0.30;
	// Slightly lift red/blue channel separation
	float luma = SF_Luma(c);
	float3 chroma = c - luma;
	chroma.r *= 1.0 + amount * 0.25;
	chroma.b *= 1.0 + amount * 0.22;
	chroma.g *= 1.0 - amount * 0.08;
	outc = lerp(outc, saturate(luma + chroma), 0.65);
	return saturate(outc);
}

float3 SF_InkBlacks(float3 c, float amount)
{
	float luma = SF_Luma(c);
	float crush = smoothstep(0.0, 0.28, luma);
	// Pull darks down while protecting mid/high
	float3 dark = c * lerp(1.0 - amount * 0.85, 1.0, crush);
	return saturate(dark);
}

void PS_SpectacularFinish(float4 vpos : SV_Position, float2 uv : TEXCOORD, out float4 color : SV_Target)
{
	float3 c = tex2D(ReShade::BackBuffer, uv).rgb;

	// Global exposure pull — midtones stop looking like light sources
	c *= exp2(UI_Exposure);

	c = SF_InkBlacks(c, UI_InkBlacks);
	c = SF_SoftContrast(c, UI_Depth);
	c = SF_Vibrance(c, UI_ColorPop);
	c = SF_HeroChroma(c, UI_HeroChroma);

	float luma = SF_Luma(c);
	// Warmth only on true highlights — not concrete mid-gray
	float hi = smoothstep(0.62, 0.95, luma);
	float lo = 1.0 - smoothstep(0.05, 0.38, luma);
	c = lerp(c, c * float3(1.05, 1.015, 0.95), hi * UI_SunWarmth);
	c = lerp(c, c * float3(0.94, 0.97, 1.06), lo * UI_SkyCool);

	float2 d = uv * 2.0 - 1.0;
	d.x *= BUFFER_ASPECT_RATIO;
	float r = length(d) / 1.15;
	float vig = saturate(1.0 - pow(r, 1.35) * UI_Vignette);
	c *= lerp(1.0 - UI_Vignette * 0.75, 1.0, vig);

	if (UI_Grain > 0.0005)
	{
		float n = SF_Hash(uv * float2(BUFFER_WIDTH, BUFFER_HEIGHT)) * 2.0 - 1.0;
		c += n * UI_Grain * lerp(1.0, 0.3, saturate(luma));
	}

	if (UI_Letterbox)
	{
		float target = max(UI_LetterboxAspect, 1.01);
		float bar = 0.0;
		if (BUFFER_ASPECT_RATIO < target)
		{
			float visible = BUFFER_ASPECT_RATIO / target;
			float halfBar = (1.0 - visible) * 0.5;
			if (uv.y < halfBar || uv.y > 1.0 - halfBar)
				bar = 1.0;
		}
		else
		{
			float visible = target / BUFFER_ASPECT_RATIO;
			float halfBar = (1.0 - visible) * 0.5;
			if (uv.x < halfBar || uv.x > 1.0 - halfBar)
				bar = 1.0;
		}
		c = lerp(c, 0.0, bar);
	}

	color = float4(saturate(c), 1.0);
}

technique SpectacularFinish < ui_tooltip = "Brand New Day style: rich reds/blues, inky blacks, sunny contrast."; >
{
	pass
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_SpectacularFinish;
	}
}
