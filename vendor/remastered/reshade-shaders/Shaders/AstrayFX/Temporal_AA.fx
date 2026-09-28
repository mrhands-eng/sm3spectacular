 ////-------//
 ///**TAA**///
 //-------////

 //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
 //* Temporal AA "Epic Games" implementation + Some Magic:
 //* For ReShade 3.0+ v 1.1
 //*  ---------------------------------
 //*                                                                           TAA
 //* Due Diligence
 //* Based on port by yvt
 //* https://www.shadertoy.com/view/4tcXD2
 //* https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf
 //* If I missed any please tell me.
 //*
 //* LICENSE
 //* ============
 //* Image Contrast Enhancement is licenses under: Attribution-NoDerivatives 4.0 International
 //*
 //* You are free to:
 //* Share - copy and redistribute the material in any medium or format
 //* for any purpose, even commercially.
 //* The licensor cannot revoke these freedoms as long as you follow the license terms.
 //* Under the following terms:
 //* Attribution - You must give appropriate credit, provide a link to the license, and indicate if changes were made.
 //* You may do so in any reasonable manner, but not in any way that suggests the licensor endorses you or your use.
 //*
 //* NoDerivatives - If you remix, transform, or build upon the material, you may not distribute the modified material.
 //*
 //* No additional restrictions - You may not apply legal terms or technological measures that legally restrict others from doing anything the license permits.
 //*
 //* https://creativecommons.org/licenses/by-nd/4.0/
 //*
 //* Have fun,
 //* Jose Negrete AKA BlueSkyDefender
 //*
 //* https://github.com/BlueSkyDefender/Depth3D
 //*
 //* Special thank you too "Jak0bW" j4712@web.de For Mouse Compatibility & Guidance.
 //* Please feel free to message him for help and information
 //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SM3 Spectacular: see NOTICE.txt — attribution restored; on-screen logo overlays removed where present.
#if exists "Overwatch.fxh"                                           //Overwatch Intercepter//
	#include "Overwatch.fxh"
#else //DA_W Depth_Linearization | DB_X Depth_Flip
	static const float DA_W = 0.0, DB_X = 0;
	#define NC 0
	#define NP 0
#endif

#define App_Sync 0

uniform float Clamping_Adjust <
	#if Compatibility
	ui_type = "drag";
	#else
	ui_type = "slider";
	#endif
	ui_min = 0; ui_max = 1.0;
	ui_label = "Clamping Adjust";
	ui_tooltip = "Adjust Clamping that effects Blur.\n"
				 "Default is Zero.";
	ui_category = "TAA";
> = 0.0;

uniform float Persistence <
	#if Compatibility
	ui_type = "drag";
	#else
	ui_type = "slider";
	#endif
	ui_min = 0.0; ui_max = 1.00;
	ui_label = "User Adjust";
	ui_tooltip = "Increase persistence of the frames this is really the Temporal Part.\n"
				 "Default is 0.25. But, a value around 0.05 is recommended.";
	ui_category = "TAA";
> = 0.125;

uniform float Similarity <
	#if Compatibility
	ui_type = "drag";
	#else
	ui_type = "slider";
	#endif
	ui_min = -1.0; ui_max = 1.0;
	ui_label = "Depth Similarity";
	ui_tooltip = "Extra Image clamping based on Depth Similarities between Past and Current Depth for DeGhosing TAA.\n"
				 "Works on Depth & Color Delta is Selected and Depth is working in the game.\n"
				 "Default is 0.25.";
	ui_category = "TAA";
> = 0.25;

uniform int Delta <
	ui_type = "combo";
	ui_label = "Used Delta Masking";
	ui_items = "Color Delta\0Depth Delta\0";
	ui_label = "TAA";
	ui_category = "TAA";
> = 0;

uniform float Delta_Power <
	#if Compatibility
	ui_type = "drag";
	#else
	ui_type = "slider";
	#endif
	ui_min = 0.0; ui_max = 1.0;
	ui_label = "Color & Depth Delta Power";
	ui_tooltip = "Extra Image clamping based on delta between Past and Current Depth Buffer.\n"
				 "Only works on Depth Delta is Selected and Depth is working in the game.\n"
				 "Default is 0.25.";
	ui_category = "TAA";
> = 0.25;

//Depth Map//
uniform int Debug <
	ui_type = "combo";
	ui_items = "TAA\0Delta Clamping\0DeGhosting Mask\0Depth\0";
	ui_label = "Debug View";
> = 0;

uniform int Depth_Map <
	ui_type = "combo";
	ui_items = "Normal\0Reverse\0";
	ui_label = "Custom Depth Map";
	ui_tooltip = "Pick your Depth Map.";
	ui_category = "Depth Buffer";
> = DA_W;

uniform float Depth_Map_Adjust <
	#if Compatibility
	ui_type = "drag";
	#else
	ui_type = "slider";
	#endif
	ui_min = 1.0; ui_max = 1000.0; ui_step = 0.125;
	ui_label = "Depth Map Adjustment";
	ui_tooltip = "Adjust the depth map and sharpness distance.";
	ui_category = "Depth Buffer";
> = 250.0;

uniform float Depth_CutOff <
	ui_type = "drag";
	ui_min = -1.0; ui_max = 1.0;
	ui_label = "Depth CutOff point";
	ui_tooltip = "Use this too set a TAA Cutoff point based on depth.\n"
				 "Default is 0.0.";
	ui_category = "Depth Buffer";
> = 0.0;

uniform bool Depth_Map_Flip <
	ui_label = "Depth Map Flip";
	ui_tooltip = "Flip the depth map if it is upside down.";
	ui_category = "Depth Buffer";
> = DB_X;

/////////////////////////////////////////////D3D Starts Here/////////////////////////////////////////////////////////////////
texture DepthBufferTex : DEPTH;

sampler DepthBuffer
	{
		Texture = DepthBufferTex;
	};

texture BackBufferTex : COLOR;

sampler BackBuffer
	{
		Texture = BackBufferTex;
	};

texture CurrentBackBufferTAA  { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8;};

sampler CBackBuffer
	{
		Texture = CurrentBackBufferTAA;
	};

texture CurrentDepthBufferTAA  { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R16f;};

sampler CDepthBuffer
	{
		Texture = CurrentDepthBufferTAA;
	};

texture PastBackBufferTAA  { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8;};

sampler PBackBuffer
	{
		Texture = PastBackBufferTAA;
	};

texture PastSingleBackBufferTAA  { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = RGBA8;};

sampler PSBackBuffer
	{
		Texture = PastSingleBackBufferTAA;
	};

texture PastSingleDepthBufferTAA  { Width = BUFFER_WIDTH; Height = BUFFER_HEIGHT; Format = R16f;};

sampler PSDepthBuffer
	{
		Texture = PastSingleDepthBufferTAA;
	};
//Total amount of frames since the game started.
uniform uint framecount < source = "framecount"; >;
uniform float timer < source = "timer"; >;
///////////////////////////////////////////////////////////TAA/////////////////////////////////////////////////////////////////////
#define pix float2(BUFFER_RCP_WIDTH, BUFFER_RCP_HEIGHT)
#define iResolution float2(BUFFER_WIDTH, BUFFER_HEIGHT)
#define Alternate framecount % 2 == 0

float2 DepthM(float2 texcoord)
{
	if (Depth_Map_Flip)
		texcoord.y =  1 - texcoord.y;

	float zBuffer = tex2D(DepthBuffer, texcoord).x; //Depth Buffer
	//Conversions to linear space.....
	//Near & Far Adjustment
	float Far = 1.0, Near = 0.125/Depth_Map_Adjust; //Division Depth Map Adjust - Near

	float2 Z = float2( zBuffer, 1-zBuffer );

	if (Depth_Map == 0)//DM0. Normal
		zBuffer = Far * Near / (Far + Z.x * (Near - Far));
	else if (Depth_Map == 1)//DM1. Reverse
		zBuffer = Far * Near / (Far + Z.y * (Near - Far));

	return float2(step(smoothstep(0,1,zBuffer),abs(Depth_CutOff)),zBuffer);
}

float4 BB_H(float2 TC)
{
	return tex2D(BackBuffer, TC );
}

// YUV-RGB conversion routine from Hyper3D
float3 encodePalYuv(float3 rgb)
{
	float3 RGB2Y =  float3( 0.299, 0.587, 0.114);
	float3 RGB2Cb = float3(-0.169,-0.331, 0.500);
	float3 RGB2Cr = float3( 0.500,-0.419,-0.081);

	return float3(dot(rgb, RGB2Y), dot(rgb, RGB2Cb), dot(rgb, RGB2Cr));
}

float3 decodePalYuv(float3 ycc)
{
	float3 YCbCr2R = float3( 1.000, 0.000, 1.400);
	float3 YCbCr2G = float3( 1.000,-0.343,-0.711);
	float3 YCbCr2B = float3( 1.000, 1.765, 0.000);

	return float3(dot(ycc, YCbCr2R), dot(ycc, YCbCr2G), dot(ycc, YCbCr2B));
}

float4 TAA(float2 texcoord)
{   //Depth Similarity
	float M_Similarity = 1-abs(Similarity), D_Similarity = saturate(pow(abs(DepthM(texcoord).y/tex2D(PSDepthBuffer,texcoord).x), 100) + M_Similarity);
	//Velocity Scaler
	float S_Velocity = 12.5 * lerp( 1, 80,Delta_Power), V_Buffer = saturate(distance(DepthM(texcoord).y,tex2D(PSDepthBuffer,texcoord).x) * S_Velocity);
	   
	float Per = 1-Persistence;
    float4 PastColor = tex2Dlod(PBackBuffer,float4(texcoord,0,0) );//Past Back Buffer
		   PastColor = (1-Per) * tex2D(BackBuffer, texcoord) + Per * PastColor;

    float3 antialiased = PastColor.xyz;
    float mixRate = min(PastColor.w, 0.5), MB = Clamping_Adjust;//WIP

    float3 BB = tex2D(BackBuffer, texcoord).xyz;

    antialiased = lerp(antialiased * antialiased, BB * BB, mixRate);
    antialiased = sqrt(antialiased);

	const float2 XYoffset[8] = { float2( 0,+pix.y ), float2( 0,-pix.y), float2(+pix.x, 0), float2(-pix.x, 0), float2(-pix.x,-pix.y), float2(+pix.x,-pix.y), float2(-pix.x,+pix.y), float2(+pix.x,+pix.y) };

	float3 minColor = encodePalYuv(tex2D(BackBuffer, texcoord ).rgb) - MB;
	float3 maxColor = encodePalYuv(tex2D(BackBuffer, texcoord ).rgb) + MB;
	for(int i = 1; i < 8; ++i)
	{   //DX9 work around.
		minColor = min(minColor,encodePalYuv(tex2Dlod(BackBuffer, float4(texcoord + XYoffset[i],0,0)).rgb)) - MB;
		maxColor = max(maxColor,encodePalYuv(tex2Dlod(BackBuffer, float4(texcoord + XYoffset[i],0,0)).rgb)) + MB;
	}
    antialiased = clamp(encodePalYuv(antialiased), minColor, maxColor);

    mixRate = rcp(1.0 / mixRate + 1.0);

    float diff = length(BB - tex2D(PSBackBuffer, texcoord).xyz) * lerp(1.0,8.0,Delta_Power);
	
	if(Delta == 1)
		diff = V_Buffer;
		
    float clampAmount = diff;

    mixRate += clampAmount;
    mixRate = clamp(mixRate, 0.05, 0.5);

    antialiased = decodePalYuv(antialiased);
	//Need to check for DX9
	float4 Output = Similarity > 0 ? lerp(float4(BB,1), float4(antialiased,mixRate), D_Similarity) : float4(lerp(BB,antialiased, D_Similarity),mixRate);
	
	if (Debug == 1)
		Output = diff;
	else if (Debug == 2)	
		Output = lerp(float3(1,0,0),Output.rgb, D_Similarity);
	else if (Debug == 3)
		Output = DepthM(texcoord).y;
		
    return Output;
}

void Out(float4 position : SV_Position, float2 texcoord : TEXCOORD, out float4 color : SV_Target)
{
	float4 T_A_A = TAA(texcoord);

	#if App_Sync
	float Scale = 2;
	if(texcoord.x < pix.x * Scale && 1-texcoord.y < pix.y * Scale)
		T_A_A = Alternate ? 0 : 1;
	#endif

	color = T_A_A;
}

void Current_BackBuffer(float4 position : SV_Position, float2 texcoord : TEXCOORD, out float4 Color : SV_Target0, out float Depth : SV_Target1)
{
	Color = BB_H(texcoord);
	Depth =  DepthM(texcoord).y;
}

void Past_BackBuffer(float4 position : SV_Position, float2 texcoord : TEXCOORD, out float4 PastSingleC : SV_Target0, out float PastSingleD : SV_Target1, out float4 Past : SV_Target2)
{
	PastSingleC = tex2D(CBackBuffer,texcoord).rgba;
	PastSingleD = tex2D(CDepthBuffer,texcoord).x;
	Past = BB_H(texcoord);
}
///////////////////////////////////////////////////////////ReShade.fxh/////////////////////////////////////////////////////////////
// Vertex shader generating a triangle covering the entire screen
void PostProcessVS(in uint id : SV_VertexID, out float4 position : SV_Position, out float2 texcoord : TEXCOORD)
{
	texcoord.x = (id == 2) ? 2.0 : 0.0;
	texcoord.y = (id == 1) ? 2.0 : 0.0;
	position = float4(texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

technique TAA
	{
			pass CBB
		{
			VertexShader = PostProcessVS;
			PixelShader = Current_BackBuffer;
			RenderTarget0 = CurrentBackBufferTAA;
			RenderTarget1 = CurrentDepthBufferTAA;
		}
			pass Out
		{
			VertexShader = PostProcessVS;
			PixelShader = Out;
		}
			pass PBB
		{
			VertexShader = PostProcessVS;
			PixelShader = Past_BackBuffer;
			RenderTarget0 = PastSingleBackBufferTAA;
			RenderTarget1 = PastSingleDepthBufferTAA;
			RenderTarget2 = PastBackBufferTAA;
		}
	}
