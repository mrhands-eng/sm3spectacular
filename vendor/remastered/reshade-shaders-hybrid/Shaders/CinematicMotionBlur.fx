/**
 * CinematicMotionBlur.fx — per-pixel screen-space motion blur (DX9)
 *
 * Blurs along estimated OBJECT/SCENE motion on screen (frame matching).
 * Does NOT use mouse/camera look delta — no whole-screen mouse streaks.
 * Sharp when the image is still. Must run AFTER CAS.
 *
 * MIT License — SM3 Spectacular Edition
 */

#include "ReShadeUI.fxh"
#include "ReShade.fxh"

uniform float frametime < source = "frametime"; >;
uniform int framecount < source = "framecount"; >;

uniform float UI_Shutter <
	ui_type = "slider";
	ui_label = "Shutter amount";
	ui_tooltip = "How strongly moving pixels streak.";
	ui_min = 0.0; ui_max = 2.0; ui_step = 0.01;
> = 0.70;

uniform float UI_Sensitivity <
	ui_type = "slider";
	ui_label = "Motion sensitivity";
	ui_min = 0.25; ui_max = 3.0; ui_step = 0.05;
> = 1.45;

uniform float UI_MaxBlur <
	ui_type = "slider";
	ui_label = "Max streak (px)";
	ui_min = 4.0; ui_max = 48.0; ui_step = 1.0;
> = 28.0;

uniform float UI_StillThreshold <
	ui_type = "slider";
	ui_label = "Still cutoff";
	ui_tooltip = "Local frame change below this = no blur.";
	ui_min = 0.005; ui_max = 0.08; ui_step = 0.001;
> = 0.018;

uniform int UI_Samples <
	ui_type = "slider";
	ui_label = "Samples";
	ui_min = 7; ui_max = 17; ui_step = 2;
> = 11;

uniform float UI_CenterKeep <
	ui_type = "slider";
	ui_label = "Center sharpness";
	ui_min = 0.0; ui_max = 1.0; ui_step = 0.01;
> = 0.25;

uniform bool UI_Debug <
	ui_label = "Debug motion (color = direction)";
> = false;

texture CMB_Curr { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
texture CMB_Prev { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8; };
texture CMB_Motion
{
	Width = BUFFER_WIDTH / 2;
	Height = BUFFER_HEIGHT / 2;
	Format = RGBA8;
};

sampler sCMB_Curr { Texture = CMB_Curr; };
sampler sCMB_Prev { Texture = CMB_Prev; };
sampler sCMB_Motion { Texture = CMB_Motion; MagFilter = LINEAR; MinFilter = LINEAR; };

float CMB_Luma(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float CMB_FilmScale()
{
	return min((1000.0 / 24.0) / max(frametime, 1.0), 1.25);
}

float CMB_CenterWeight(float2 uv)
{
	if (UI_CenterKeep <= 0.001)
		return 1.0;
	float2 c = uv * 2.0 - 1.0;
	c.x *= BUFFER_ASPECT_RATIO;
	return lerp(1.0, smoothstep(0.12, 1.15, length(c)), UI_CenterKeep);
}

float2 CMB_Encode(float2 motionPx)
{
	float2 c = clamp(motionPx, -UI_MaxBlur, UI_MaxBlur);
	return c / max(UI_MaxBlur, 1.0) * 0.5 + 0.5;
}

float2 CMB_Decode(float2 enc)
{
	return (enc * 2.0 - 1.0) * UI_MaxBlur;
}

// Block-match previous frame → per-pixel screen motion of whatever moved.
float2 CMB_EstimateMotion(float2 uv)
{
	float curr = CMB_Luma(tex2Dlod(sCMB_Curr, float4(uv, 0, 0)).rgb);
	float still = abs(curr - CMB_Luma(tex2Dlod(sCMB_Prev, float4(uv, 0, 0)).rgb));

	if (still < UI_StillThreshold)
		return 0.0;

	float bestCost = still;
	float2 best = 0.0;

	// Coarse (±18 px) then fine refine — object / scene motion on screen.
	static const int R = 3;
	static const float STEP = 6.0;
	[loop]
	for (int y = -R; y <= R; y++)
	{
		[loop]
		for (int x = -R; x <= R; x++)
		{
			if (x == 0 && y == 0)
				continue;
			float2 off = float2(x, y) * STEP;
			float2 suv = uv + off * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
			float prev = CMB_Luma(tex2Dlod(sCMB_Prev, float4(suv, 0, 0)).rgb);
			float cost = abs(curr - prev) + length(off) * 0.0002;
			if (cost < bestCost)
			{
				bestCost = cost;
				best = off;
			}
		}
	}

	float2 base = best;
	[unroll]
	for (int fy = -2; fy <= 2; fy++)
	{
		[unroll]
		for (int fx = -2; fx <= 2; fx++)
		{
			float2 off = base + float2(fx, fy);
			float2 suv = uv + off * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
			float prev = CMB_Luma(tex2Dlod(sCMB_Prev, float4(suv, 0, 0)).rgb);
			float cost = abs(curr - prev);
			if (cost < bestCost)
			{
				bestCost = cost;
				best = off;
			}
		}
	}

	// Must beat "no move" — reject lighting flicker.
	if (length(best) < 0.5 || bestCost > still * 0.78)
		return 0.0;

	float boost = saturate((still - UI_StillThreshold) * 10.0);
	return best * UI_Sensitivity * boost;
}

void PS_CopyCurr(float4 vpos : SV_Position, float2 uv : TEXCOORD, out float4 color : SV_Target)
{
	color = tex2D(ReShade::BackBuffer, uv);
}

void PS_Estimate(float4 vpos : SV_Position, float2 uv : TEXCOORD, out float4 motion : SV_Target)
{
	if (framecount < 2)
	{
		motion = float4(0.5, 0.5, 0.0, 1.0);
		return;
	}
	motion = float4(CMB_Encode(CMB_EstimateMotion(uv)), 0.0, 1.0);
}

void PS_Apply(float4 vpos : SV_Position, float2 uv : TEXCOORD, out float4 color : SV_Target)
{
	float3 center = tex2D(ReShade::BackBuffer, uv).rgb;
	if (framecount < 2 || UI_Shutter <= 0.001)
	{
		color = float4(center, 1.0);
		return;
	}

	float2 motionPx = CMB_Decode(tex2D(sCMB_Motion, uv).rg);
	motionPx *= UI_Shutter * CMB_FilmScale() * CMB_CenterWeight(uv);

	float len = length(motionPx);
	if (len > UI_MaxBlur)
		motionPx *= UI_MaxBlur / len;
	len = length(motionPx);

	if (UI_Debug)
	{
		float3 dirCol = float3(0.45, 0.45, 0.45);
		if (len > 0.5)
		{
			float2 n = motionPx / len;
			dirCol = float3(saturate(n.x * 0.5 + 0.5), saturate(-n.y * 0.5 + 0.5), 0.12);
		}
		color = float4(lerp(center, dirCol, 0.7), 1.0);
		return;
	}

	// No local screen motion → perfectly sharp.
	if (len < 1.5)
	{
		color = float4(center, 1.0);
		return;
	}

	float2 stepUv = motionPx * float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT);
	int samples = UI_Samples;
	if (samples < 7) samples = 7;
	if (samples > 17) samples = 17;
	if ((samples % 2) == 0) samples += 1;

	float3 acc = 0.0;
	float wsum = 0.0;
	[loop]
	for (int i = 0; i < samples; i++)
	{
		float t = (i / (float)(samples - 1)) * 2.0 - 1.0;
		float w = 1.0 - abs(t) * 0.35;
		float2 suv = saturate(uv + stepUv * t);
		acc += tex2Dlod(ReShade::BackBuffer, float4(suv, 0, 0)).rgb * w;
		wsum += w;
	}

	color = float4(acc / max(wsum, 1e-5), 1.0);
}

void PS_SavePrev(float4 vpos : SV_Position, float2 uv : TEXCOORD, out float4 prev : SV_Target)
{
	prev = tex2D(sCMB_Curr, uv);
}

technique CinematicMotionBlur < ui_tooltip = "Screen-space object/scene motion blur (no mouse). Last after CAS."; >
{
	pass CopyCurr
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_CopyCurr;
		RenderTarget = CMB_Curr;
	}
	pass Estimate
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_Estimate;
		RenderTarget = CMB_Motion;
	}
	pass Apply
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_Apply;
	}
	pass SavePrev
	{
		VertexShader = PostProcessVS;
		PixelShader = PS_SavePrev;
		RenderTarget = CMB_Prev;
	}
}
