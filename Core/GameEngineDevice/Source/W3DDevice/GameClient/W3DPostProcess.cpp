/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: W3DPostProcess.cpp ///////////////////////////////////////////////////
// Builds the PostProcess chain of the INI with the post-processing of the Direct3D 12 renderer.
// A pass's shader comes from Data\Shaders\PostProcess\<Shader>.hlsl, else from the built-in shaders
// below, so the mod can replace them or add its own.
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DPostProcess.h"

#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "GameClient/PostProcess.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/texture.h"

#ifdef RTS_RENDERER_DX12
#include <d3d8on12_ext.h>
#endif

W3DPostProcess *TheW3DPostProcess = nullptr;

#ifdef RTS_RENDERER_DX12

static const char *SHADER_DIRECTORY = "Data\\Shaders\\PostProcess\\";

//-------------------------------------------------------------------------------------------------
// Built-in shaders. Parameters left out of the INI are 0, so every parameter is neutral at 0 or
// falls back to a sensible value.
//-------------------------------------------------------------------------------------------------

// Input0: Depth. Shows the distance from the camera as grey, white at Far (0 = 2000) world units.
static const char s_depthViewShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float Far;
};
float4 main(PostProcessInput input) : SV_Target
{
	float distance = LinearDepth(Input0.Sample(PointClamp, input.uv).r);
	return float4(saturate(distance / (Far > 0 ? Far : 2000)).xxx, 1);
}
)";

// Input0: Depth. Ambient occlusion: how much of the hemisphere above each surface point is blocked
// by nearby geometry, from samples in view space checked against the depth. 1 = open, 0 = closed.
static const char s_ssaoShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float Radius;		// reach of the occlusion in world units, 0 = 20
	float Intensity;	// 0 = 1
	float Bias;			// world units a sample must be behind the surface to count, 0 = 2
	float Power;		// contrast of the result, 0 = 1.5
};
static const float3 KERNEL[12] =
{
	float3(0.0673, 0.0000, 0.2462),
	float3(-0.0896, 0.0821, 0.2421),
	float3(0.0147, -0.1680, 0.2443),
	float3(0.1337, 0.1743, 0.2507),
	float3(-0.2741, -0.0485, 0.2590),
	float3(0.2924, -0.1860, 0.2671),
	float3(-0.1104, 0.4108, 0.2726),
	float3(-0.2375, -0.4573, 0.2734),
	float3(0.5790, 0.2114, 0.2674),
	float3(-0.6733, 0.2779, 0.2521),
	float3(0.3606, -0.7706, 0.2256),
	float3(0.2941, 0.9376, 0.1854),
};
float DepthAt(float2 uv) { return Input0.SampleLevel(PointClamp, uv, 0).r; }
float4 main(PostProcessInput input) : SV_Target
{
	float depth = DepthAt(input.uv);
	if (depth >= 1)
		return float4(1, 1, 1, 1);
	float radius = Radius > 0 ? Radius : 20;
	float3 center = ViewPosition(input.uv, depth);
	// Depth steps grow with the distance, so the bias does too.
	float bias = (Bias > 0 ? Bias : 2) + abs(center.z) * 0.004;

	// The normal from the neighbors on the side with the smaller depth step, so edges stay sharp;
	// two pixels away, so the steps of the depth buffer do not tilt it.
	float2 texel = InputSize[0].zw * 2;
	float3 right = ViewPosition(input.uv + float2(texel.x, 0), DepthAt(input.uv + float2(texel.x, 0)));
	float3 left = ViewPosition(input.uv - float2(texel.x, 0), DepthAt(input.uv - float2(texel.x, 0)));
	float3 down = ViewPosition(input.uv + float2(0, texel.y), DepthAt(input.uv + float2(0, texel.y)));
	float3 up = ViewPosition(input.uv - float2(0, texel.y), DepthAt(input.uv - float2(0, texel.y)));
	float3 dx = abs(right.z - center.z) < abs(center.z - left.z) ? right - center : center - left;
	float3 dy = abs(down.z - center.z) < abs(center.z - up.z) ? down - center : center - up;
	float3 normal = normalize(cross(dx, dy));
	if (dot(normal, -center) < 0)
		normal = -normal;

	// A different rotation of the samples for each pixel; the blur afterwards smooths the noise.
	float noise = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
	float3 random = float3(cos(noise * 6.2831853), sin(noise * 6.2831853), 0.5);
	float3 tangent = normalize(random - normal * dot(random, normal));
	float3 bitangent = cross(normal, tangent);

	float occlusion = 0;
	[unroll]
	for (int i = 0; i < 12; ++i)
	{
		float3 sample = center + (tangent * KERNEL[i].x + bitangent * KERNEL[i].y + normal * KERNEL[i].z) * radius;
		float4 clip = mul(float4(sample, 1), Projection);
		float2 uv = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
		if (any(uv < 0) || any(uv > 1))
			continue;
		float3 surface = ViewPosition(uv, DepthAt(uv));
		// Blocked when the visible surface there is nearer to the camera than the sample point; a soft
		// step, so the depth steps of flat ground do not show as stripes.
		float blocked = saturate((abs(sample.z) - bias - abs(surface.z)) / bias);
		// Surfaces far in front (a tree before the ground behind it) do not count, against dark halos.
		float range = 1 - smoothstep(radius, radius * 2, abs(center.z - surface.z));
		occlusion += blocked * range;
	}
	float ao = saturate(1 - occlusion / 12 * (Intensity > 0 ? Intensity : 1));
	ao = pow(ao, Power > 0 ? Power : 1.5);
	return float4(ao, ao, ao, 1);
}
)";

// Input0: the image to blur (such as ambient occlusion), Input1: Depth. A blur along Direction that
// does not mix across depth edges, so occlusion stays on its object.
static const char s_depthBlurShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float2 Direction;	// 1 0 for horizontal, 0 1 for vertical
	float Sharpness;	// how strictly depth edges are kept, 0 = 1
};
static const float WEIGHTS[4] = { 0.2270270, 0.1945946, 0.1216216, 0.0540540 };
float4 main(PostProcessInput input) : SV_Target
{
	float centerDepth = LinearDepth(Input1.SampleLevel(PointClamp, input.uv, 0).r);
	float2 step = Direction * InputSize[0].zw;
	float sharpness = Sharpness > 0 ? Sharpness : 1;
	float4 sum = 0;
	float total = 0;
	[unroll]
	for (int i = -3; i <= 3; ++i)
	{
		float2 uv = input.uv + step * i;
		float depth = LinearDepth(Input1.SampleLevel(PointClamp, uv, 0).r);
		float weight = WEIGHTS[abs(i)] * exp(-abs(depth - centerDepth) / max(centerDepth * 0.01, 0.001) * sharpness);
		sum += Input0.SampleLevel(LinearClamp, uv, 0) * weight;
		total += weight;
	}
	return sum / max(total, 0.0001);
}
)";

// Input0: the image, Input1: Depth. Depth of field: sharp around the focus distance, blurred with
// a disk in front of and behind it. Samples count only where their own blur reaches this pixel, so
// sharp objects keep their edges against a blurred background and do not smear into it.
static const char s_depthOfFieldShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float FocusDistance;	// distance from the camera that is sharp, 0 = the distance at the screen center
	float FocusRange;		// sharp this far in front of and behind the focus, 0 = 100
	float BlurRange;		// distance beyond FocusRange until the blur is full, 0 = 300
	float MaxBlur;			// full blur radius in pixels, 0 = 6
	float FarOnly;			// 1 = only behind the focus, so units near the camera (aircraft) stay sharp
};
static const int TAPS = 24;
float BlurAmount(float depth, float focus)
{
	float offset = LinearDepth(depth) - focus;
	if (FarOnly > 0 && offset < 0)
		return 0;
	return saturate((abs(offset) - (FocusRange > 0 ? FocusRange : 100)) / (BlurRange > 0 ? BlurRange : 300));
}
float4 main(PostProcessInput input) : SV_Target
{
	float4 color = Input0.SampleLevel(PointClamp, input.uv, 0);
	float focus = FocusDistance > 0 ? FocusDistance : LinearDepth(Input1.SampleLevel(PointClamp, float2(0.5, 0.5), 0).r);
	float maxBlur = MaxBlur > 0 ? MaxBlur : 6;
	float radius = BlurAmount(Input1.SampleLevel(PointClamp, input.uv, 0).r, focus) * maxBlur;
	if (radius < 0.5)
		return color;
	float3 sum = color.rgb;
	float total = 1;
	[unroll]
	for (int i = 0; i < TAPS; ++i)
	{
		// Evenly spread over the disk (golden angle spiral).
		float distance = sqrt((i + 0.5) / TAPS) * radius;
		float angle = i * 2.39996323;
		float2 uv = input.uv + float2(cos(angle), sin(angle)) * distance * InputSize[0].zw;
		float sampleRadius = BlurAmount(Input1.SampleLevel(PointClamp, uv, 0).r, focus) * maxBlur;
		float weight = saturate(sampleRadius - distance + 1);
		sum += Input0.SampleLevel(LinearClamp, uv, 0).rgb * weight;
		total += weight;
	}
	return float4(sum / total, color.a);
}
)";

// Input0: the image, Input1: Depth. Distance and height fog toward Color.
static const char s_fogShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float3 Color;			// fog color
	float Density;			// 0..1 maximum fog, 0 = 1
	float Start;			// distance from the camera where fog begins, 0 = 700
	float End;				// distance where it is full, 0 = 1600
	float HeightStart;		// world height where height fog ends (above it none)
	float HeightEnd;		// world height where height fog is full; height fog only when below HeightStart
};
float4 main(PostProcessInput input) : SV_Target
{
	float3 color = Input0.Sample(PointClamp, input.uv).rgb;
	float depth = Input1.SampleLevel(PointClamp, input.uv, 0).r;
	if (depth >= 1)
		return float4(color, 1);
	float3 view = ViewPosition(input.uv, depth);
	float start = Start > 0 ? Start : 700;
	float end = End > start ? End : start + 900;
	float fog = saturate((abs(view.z) - start) / (end - start));
	if (HeightEnd < HeightStart)
	{
		float height = WorldPosition(view).z;
		fog = max(fog, saturate((HeightStart - height) / (HeightStart - HeightEnd)));
	}
	return float4(lerp(color, Color, fog * (Density > 0 ? Density : 1)), 1);
}
)";

static const char s_copyShader[] = R"(
#include "PostProcess.hlsli"
float4 main(PostProcessInput input) : SV_Target
{
	return float4(Input0.Sample(PointClamp, input.uv).rgb, 1);
}
)";

// Input0: scene. Keeps what is brighter than Threshold, smoothly over Knee, at the smaller size.
static const char s_bloomExtractShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float Threshold;	// brightness where bloom starts, 0 = 0.75
	float Knee;			// softness of the start, 0 = 0.1
};
float4 main(PostProcessInput input) : SV_Target
{
	float2 texel = InputSize[0].zw;
	float3 color = Input0.Sample(LinearClamp, input.uv + float2(-texel.x, -texel.y)).rgb;
	color += Input0.Sample(LinearClamp, input.uv + float2(texel.x, -texel.y)).rgb;
	color += Input0.Sample(LinearClamp, input.uv + float2(-texel.x, texel.y)).rgb;
	color += Input0.Sample(LinearClamp, input.uv + float2(texel.x, texel.y)).rgb;
	color *= 0.25;
	float threshold = Threshold > 0 ? Threshold : 0.75;
	float knee = Knee > 0 ? Knee : 0.1;
	float brightness = max(color.r, max(color.g, color.b));
	return float4(color * smoothstep(threshold - knee, threshold + knee, brightness), 1);
}
)";

// Input0: the image to blur. A 9 tap Gaussian along Direction, with 5 bilinear samples.
static const char s_blurShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float2 Direction;	// 1 0 for horizontal, 0 1 for vertical
	float Radius;		// spread in texels of the input, 0 = 1
};
float4 main(PostProcessInput input) : SV_Target
{
	float2 step = Direction * InputSize[0].zw * (Radius > 0 ? Radius : 1);
	float3 color = Input0.Sample(LinearClamp, input.uv).rgb * 0.2270270;
	color += (Input0.Sample(LinearClamp, input.uv + step * 1.3846154).rgb + Input0.Sample(LinearClamp, input.uv - step * 1.3846154).rgb) * 0.3162162;
	color += (Input0.Sample(LinearClamp, input.uv + step * 3.2307692).rgb + Input0.Sample(LinearClamp, input.uv - step * 3.2307692).rgb) * 0.0702703;
	return float4(color, 1);
}
)";

// Input0: scene, Input1: bloom (optional), Input2: 256x16 color lookup table (optional),
// Input3: ambient occlusion (optional, from PP_SSAO). With a Tonemap the scene, bloom and exposure
// are combined in linear light and brought to 0..1 by a tone curve before the grading.
static const char s_colorGradeShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float BloomIntensity;	// 0 = no bloom
	float Exposure;			// in stops
	float Contrast;			// 0.2 = 20 % more
	float Saturation;		// -1 = grey, 0.2 = 20 % more
	float3 Tint;			// color the image is pulled toward
	float TintStrength;		// 0..1
	float3 Lift;			// added to the shadows
	float LutStrength;		// 0..1, needs Input2
	float3 Gamma;			// midtones, 0.2 = 20 % brighter
	float GammaPad;
	float3 Gain;			// highlights, 0.2 = 20 % more
	float AOStrength;		// how much Input3 darkens, 0 = 1
	float Tonemap;			// tone curve: 0 = none, 1 = ACES, 2 = filmic (Hable), 3 = Reinhard
	float WhitePoint;		// linear brightness that becomes white with 2 and 3, 0 = 4
	float HighlightExpand;	// 0..0.95, stretches the highlights of an 8 bit scene before the curve (0 with HDRScene)
};
float3 ApplyLut(float3 color)
{
	color = saturate(color);
	float blue = color.b * 15;
	float slice = floor(blue);
	float x = (color.r * 15 + 0.5) / 256;
	float y = (color.g * 15 + 0.5) / 16;
	float3 low = Input2.Sample(LinearClamp, float2(x + slice / 16, y)).rgb;
	float3 high = Input2.Sample(LinearClamp, float2(x + min(slice + 1, 15) / 16, y)).rgb;
	return lerp(low, high, blue - slice);
}
float3 Hable(float3 x)
{
	return ((x * (0.15 * x + 0.05) + 0.004) / (x * (0.15 * x + 0.5) + 0.06)) - 0.0667;
}
float3 ToneCurve(float3 color)
{
	float white = WhitePoint > 0 ? WhitePoint : 4;
	if (Tonemap < 1.5)
		return saturate((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14));
	if (Tonemap < 2.5)
		return saturate(Hable(color * 2) / Hable(white.xxx));
	float luma = Luminance(color);
	float mapped = luma * (1 + luma / (white * white)) / (1 + luma);
	return saturate(color * (mapped / max(luma, 0.0001)));
}
float4 main(PostProcessInput input) : SV_Target
{
	float3 color = Input0.Sample(PointClamp, input.uv).rgb;
	float ao = InputSize[3].x > 0 ? lerp(1, Input3.Sample(LinearClamp, input.uv).r, AOStrength > 0 ? AOStrength : 1) : 1;
	float3 bloom = InputSize[1].x > 0 ? Input1.Sample(LinearClamp, input.uv).rgb * BloomIntensity : 0;
	if (Tonemap > 0)
	{
		// The scene is stored gamma encoded; the curves expect linear light.
		color = pow(max(color, 0), 2.2) * ao + pow(max(bloom, 0), 2.2);
		float expand = clamp(HighlightExpand, 0, 0.95);
		color /= max(1 - expand * saturate(Luminance(color)), 0.05);
		color = pow(ToneCurve(color * exp2(Exposure)), 1 / 2.2);
	}
	else
	{
		color = (color * ao + bloom) * exp2(Exposure);
	}
	color = color * (1 + Gain) + Lift * (1 - color);
	color = pow(max(color, 0), 1 / max(1 + Gamma, 0.01));
	color = (color - 0.5) * (1 + Contrast) + 0.5;
	float luma = Luminance(color);
	color = lerp(luma.xxx, color, 1 + Saturation);
	color = lerp(color, luma * Tint, TintStrength);
	if (LutStrength > 0 && InputSize[2].x > 0)
		color = lerp(color, ApplyLut(color), LutStrength);
	return float4(saturate(color), 1);
}
)";

// Input0: the image. Smooths aliased edges: finds the edge direction from the luma around the pixel,
// searches along the edge for its ends and samples across it by how far the pixel is from them.
static const char s_edgeAAShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float EdgeThreshold;	// contrast needed relative to the brightest neighbor, 0 = 0.166
	float EdgeThresholdMin;	// contrast always ignored, for dark areas, 0 = 0.0625
	float Subpixel;			// smoothing of single pixel detail, 0..1, 0 = 0.75
};
float LumaAt(float2 uv) { return Luminance(Input0.SampleLevel(LinearClamp, uv, 0).rgb); }
float4 main(PostProcessInput input) : SV_Target
{
	float2 texel = InputSize[0].zw;
	float2 uv = input.uv;
	float3 center = Input0.SampleLevel(PointClamp, uv, 0).rgb;
	float lumaM = Luminance(center);
	float lumaN = LumaAt(uv + float2(0, -texel.y));
	float lumaS = LumaAt(uv + float2(0, texel.y));
	float lumaW = LumaAt(uv + float2(-texel.x, 0));
	float lumaE = LumaAt(uv + float2(texel.x, 0));
	float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
	float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
	float range = lumaMax - lumaMin;
	float threshold = EdgeThreshold > 0 ? EdgeThreshold : 0.166;
	float thresholdMin = EdgeThresholdMin > 0 ? EdgeThresholdMin : 0.0625;
	if (range < max(thresholdMin, lumaMax * threshold))
		return float4(center, 1);

	float lumaNW = LumaAt(uv + float2(-texel.x, -texel.y));
	float lumaNE = LumaAt(uv + float2(texel.x, -texel.y));
	float lumaSW = LumaAt(uv + float2(-texel.x, texel.y));
	float lumaSE = LumaAt(uv + float2(texel.x, texel.y));

	// A horizontal edge changes most from north to south.
	float edgeH = abs(lumaNW + lumaNE - 2 * lumaN) + 2 * abs(lumaW + lumaE - 2 * lumaM) + abs(lumaSW + lumaSE - 2 * lumaS);
	float edgeV = abs(lumaNW + lumaSW - 2 * lumaW) + 2 * abs(lumaN + lumaS - 2 * lumaM) + abs(lumaNE + lumaSE - 2 * lumaE);
	bool horizontal = edgeH >= edgeV;

	float lumaBefore = horizontal ? lumaN : lumaW;
	float lumaAfter = horizontal ? lumaS : lumaE;
	float gradientBefore = abs(lumaBefore - lumaM);
	float gradientAfter = abs(lumaAfter - lumaM);
	float2 across = horizontal ? float2(0, texel.y) : float2(texel.x, 0);
	float lumaOther = lumaAfter;
	if (gradientBefore >= gradientAfter)
	{
		across = -across;
		lumaOther = lumaBefore;
	}
	float gradient = max(gradientBefore, gradientAfter) * 0.25;
	float lumaEdge = (lumaM + lumaOther) * 0.5;

	// Walk along the edge in both directions until the luma no longer matches it.
	float2 along = horizontal ? float2(texel.x, 0) : float2(0, texel.y);
	float2 uvEdge = uv + across * 0.5;
	float2 uv1 = uvEdge - along;
	float2 uv2 = uvEdge + along;
	float end1 = LumaAt(uv1) - lumaEdge;
	float end2 = LumaAt(uv2) - lumaEdge;
	bool done1 = abs(end1) >= gradient;
	bool done2 = abs(end2) >= gradient;
	static const float steps[8] = { 1, 1, 1, 1.5, 2, 2, 4, 8 };
	[unroll]
	for (int i = 0; i < 8; ++i)
	{
		if (!done1)
		{
			uv1 -= along * steps[i];
			end1 = LumaAt(uv1) - lumaEdge;
			done1 = abs(end1) >= gradient;
		}
		if (!done2)
		{
			uv2 += along * steps[i];
			end2 = LumaAt(uv2) - lumaEdge;
			done2 = abs(end2) >= gradient;
		}
	}
	float distance1 = horizontal ? uv.x - uv1.x : uv.y - uv1.y;
	float distance2 = horizontal ? uv2.x - uv.x : uv2.y - uv.y;
	float closest = min(distance1, distance2);
	float edgeOffset = 0.5 - closest / (distance1 + distance2);
	// Only blend when the closer end confirms the pixel is on the side the edge passes.
	bool centerDarker = lumaM < lumaEdge;
	bool confirmed = ((distance1 < distance2 ? end1 : end2) < 0) != centerDarker;
	edgeOffset = confirmed ? edgeOffset : 0;

	// Single pixel detail blends by how much the pixel differs from its neighborhood.
	float average = (2 * (lumaN + lumaS + lumaW + lumaE) + lumaNW + lumaNE + lumaSW + lumaSE) / 12;
	float subpixel = smoothstep(0, 1, saturate(abs(average - lumaM) / range));
	float subpixelOffset = subpixel * subpixel * (Subpixel > 0 ? Subpixel : 0.75);

	float offset = max(edgeOffset, subpixelOffset);
	return float4(Input0.SampleLevel(LinearClamp, uv + across * offset, 0).rgb, 1);
}
)";

// Input0: the image. Contrast adaptive sharpening and a vignette.
static const char s_sharpenVignetteShader[] = R"(
#include "PostProcess.hlsli"
cbuffer Params : register(b1)
{
	float Sharpen;			// 0 = off, 1 = strongest
	float Vignette;			// darkening at the corners, 0 = off, 1 = black
	float VignetteRadius;	// distance from the center where it starts (screen height = 1), 0 = 0.75
	float VignetteSoftness;	// width of the transition, 0 = 0.45
};
float4 main(PostProcessInput input) : SV_Target
{
	float2 texel = InputSize[0].zw;
	float3 color = Input0.Sample(PointClamp, input.uv).rgb;
	if (Sharpen > 0)
	{
		float3 n = Input0.Sample(PointClamp, input.uv + float2(0, -texel.y)).rgb;
		float3 s = Input0.Sample(PointClamp, input.uv + float2(0, texel.y)).rgb;
		float3 w = Input0.Sample(PointClamp, input.uv + float2(-texel.x, 0)).rgb;
		float3 e = Input0.Sample(PointClamp, input.uv + float2(texel.x, 0)).rgb;
		float3 low = min(color, min(min(n, s), min(w, e)));
		float3 high = max(color, max(max(n, s), max(w, e)));
		// Less sharpening where the neighborhood already has strong contrast, so edges do not ring.
		float3 amount = sqrt(saturate(min(low, 2 - high) / max(high, 0.0001)));
		float3 weight = amount * (-1 / lerp(8.0, 5.0, saturate(Sharpen)));
		color = saturate((color + (n + s + w + e) * weight) / (1 + 4 * weight));
	}
	if (Vignette > 0)
	{
		float2 offset = input.uv - 0.5;
		offset.x *= ScreenSize.x * ScreenSize.w;
		float radius = VignetteRadius > 0 ? VignetteRadius : 0.75;
		float softness = VignetteSoftness > 0 ? VignetteSoftness : 0.45;
		float shade = smoothstep(radius, radius - softness, length(offset));
		color *= lerp(1, shade, saturate(Vignette));
	}
	return float4(color, 1);
}
)";

struct BuiltInShader
{
	const char *name;
	const char *source;
	size_t length;
};

#define BUILT_IN(name, source) { name, source, sizeof(source) - 1 }

static const BuiltInShader s_builtInShaders[] =
{
	BUILT_IN("PP_Copy", s_copyShader),
	BUILT_IN("PP_DepthView", s_depthViewShader),
	BUILT_IN("PP_SSAO", s_ssaoShader),
	BUILT_IN("PP_DepthBlur", s_depthBlurShader),
	BUILT_IN("PP_Fog", s_fogShader),
	BUILT_IN("PP_DepthOfField", s_depthOfFieldShader),
	BUILT_IN("PP_BloomExtract", s_bloomExtractShader),
	BUILT_IN("PP_Blur", s_blurShader),
	BUILT_IN("PP_ColorGrade", s_colorGradeShader),
	BUILT_IN("PP_EdgeAA", s_edgeAAShader),
	BUILT_IN("PP_SharpenVignette", s_sharpenVignetteShader),
};

//-------------------------------------------------------------------------------------------------
/** Reads a file through the game's file system (so BIG archives work); the caller deletes the
	* buffer with delete[]. */
//-------------------------------------------------------------------------------------------------
static char *readShaderFile(const AsciiString& path, UINT *length)
{
	File *file = TheFileSystem->openFile(path.str(), File::READ | File::BINARY);
	if (file == nullptr)
		return nullptr;
	*length = (UINT)file->size();
	return file->readEntireAndClose();
}

//-------------------------------------------------------------------------------------------------
/** #include in a pass shader reads from Data\Shaders\PostProcess. */
//-------------------------------------------------------------------------------------------------
class ShaderIncludes : public ID3DInclude
{
public:
	HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID *data, UINT *bytes) override
	{
		AsciiString path;
		path.format("%s%s", SHADER_DIRECTORY, name);
		char *buffer = readShaderFile(path, bytes);
		if (buffer == nullptr)
			return E_FAIL;
		*data = buffer;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override
	{
		delete[] (char *)data;
		return S_OK;
	}
};

#endif // RTS_RENDERER_DX12

//-------------------------------------------------------------------------------------------------
W3DPostProcess::W3DPostProcess() :
	m_postProcess(nullptr),
	m_built(FALSE),
	m_startTime(0)
{
}

//-------------------------------------------------------------------------------------------------
W3DPostProcess::~W3DPostProcess()
{
	release();
	delete ThePostProcessSettings;
	ThePostProcessSettings = nullptr;
	delete TheShadowMapSettings;
	TheShadowMapSettings = nullptr;
}

//-------------------------------------------------------------------------------------------------
void W3DPostProcess::init()
{
	m_startTime = timeGetTime();
	loadSettings();
}

//-------------------------------------------------------------------------------------------------
void W3DPostProcess::loadSettings()
{
	delete ThePostProcessSettings;
	ThePostProcessSettings = nullptr;
	delete TheShadowMapSettings;
	TheShadowMapSettings = nullptr;

	INI ini;
	ini.loadFileDirectory("Data\\INI\\Default\\PostProcess", INI_LOAD_OVERWRITE, nullptr, TRUE, TRUE);
	ini.loadFileDirectory("Data\\INI\\PostProcess", INI_LOAD_OVERWRITE, nullptr, TRUE, TRUE);
}

//-------------------------------------------------------------------------------------------------
void W3DPostProcess::reload()
{
	release();
	loadSettings();
}

//-------------------------------------------------------------------------------------------------
Bool W3DPostProcess::isAvailable()
{
	return DX8Wrapper::Get_Post_Process() != nullptr;
}

//-------------------------------------------------------------------------------------------------
Bool W3DPostProcess::isRunning()
{
	return m_built && !m_passNames.empty() && TheGlobalData->m_postProcessing && m_postProcess == DX8Wrapper::Get_Post_Process();
}

//-------------------------------------------------------------------------------------------------
void W3DPostProcess::release()
{
#ifdef RTS_RENDERER_DX12
	if (m_postProcess != nullptr && m_postProcess == DX8Wrapper::Get_Post_Process())
	{
		m_postProcess->Set_HDR_Scene(FALSE);
		m_postProcess->Set_Chain(nullptr, 0);
		for (size_t i = 0; i < m_shaders.size(); ++i)
			m_postProcess->Release_Shader(m_shaders[i]);
	}
#endif
	m_shaders.clear();
	for (size_t i = 0; i < m_textures.size(); ++i)
		REF_PTR_RELEASE(m_textures[i]);
	m_textures.clear();
	m_passNames.clear();
	m_postProcess = nullptr;
	m_built = FALSE;
	m_error.clear();
}

//-------------------------------------------------------------------------------------------------
void W3DPostProcess::run(CameraClass *camera)
{
#ifdef RTS_RENDERER_DX12
	ID3D8On12PostProcess *postProcess = DX8Wrapper::Get_Post_Process();
	if (postProcess == nullptr)
		return;
	// A float back buffer only while a chain brings the scene back to 0..1.
	if (!TheGlobalData->m_postProcessing || ThePostProcessSettings == nullptr || ThePostProcessSettings->m_chain.empty())
	{
		postProcess->Set_HDR_Scene(FALSE);
		return;
	}
	if (postProcess != m_postProcess)
	{
		// First run, or the device was created again.
		release();
		build(postProcess);
	}
	postProcess->Set_HDR_Scene(!m_passNames.empty() && ThePostProcessSettings->m_hdrScene);
	if (m_passNames.empty())
		return;

	// The matrices exactly as the renderer got them for this camera, for passes that read the depth.
	if (camera != nullptr)
	{
		camera->Apply();
		DX8Wrapper::Apply_Render_State_Changes();
		D3DMATRIX view;
		D3DMATRIX projection;
		DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);
		DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, projection);
		postProcess->Set_Camera(&view, &projection);
	}
	postProcess->Run((timeGetTime() - m_startTime) * 0.001f);
#endif
}

//-------------------------------------------------------------------------------------------------
/** Compiles the shaders of the chain and hands it to the renderer. On an error the chain stays
	* empty until it is reloaded; the error is in the log and in getError(). */
//-------------------------------------------------------------------------------------------------
void W3DPostProcess::build(ID3D8On12PostProcess *postProcess)
{
#ifdef RTS_RENDERER_DX12
	m_postProcess = postProcess;
	m_built = TRUE;

	const std::vector<AsciiString>& chain = ThePostProcessSettings->m_chain;
	std::vector<D3D8ON12_PP_PASS> passes;
	std::vector<AsciiString> names;
	ShaderIncludes includes;

	for (size_t index = 0; index < chain.size() && m_error.isEmpty(); ++index)
	{
		const PostProcessPassTemplate *pass = ThePostProcessSettings->findPass(chain[index]);
		if (pass == nullptr)
		{
			m_error.format("PostProcess: no PostProcessPass named %s", chain[index].str());
			break;
		}

		// The shader: a file of the mod, else a built-in one.
		AsciiString path;
		path.format("%s%s.hlsl", SHADER_DIRECTORY, pass->m_shader.str());
		UINT length = 0;
		char *file = readShaderFile(path, &length);
		const char *source = file;
		if (source == nullptr)
		{
			for (size_t i = 0; i < ARRAY_SIZE(s_builtInShaders); ++i)
			{
				if (pass->m_shader.compareNoCase(s_builtInShaders[i].name) == 0)
				{
					source = s_builtInShaders[i].source;
					length = (UINT)s_builtInShaders[i].length;
				}
			}
		}
		if (source == nullptr)
		{
			m_error.format("PostProcess: pass %s: no shader %s (looked for %s)", pass->m_name.str(), pass->m_shader.str(), path.str());
			break;
		}

		UINT shader = 0;
		ID3DBlob *messages = nullptr;
		const HRESULT hr = postProcess->Create_Shader(source, length, path.str(), &includes, &shader, &messages);
		delete[] file;
		if (FAILED(hr))
		{
			m_error.format("PostProcess: pass %s: shader %s does not compile: %s", pass->m_name.str(), pass->m_shader.str(),
				messages != nullptr ? (const char *)messages->GetBufferPointer() : "");
			if (messages != nullptr)
				messages->Release();
			break;
		}
		m_shaders.push_back(shader);

		D3D8ON12_PP_PASS desc = {};
		desc.shader = shader;
		desc.scale = pass->m_scale > 0 ? pass->m_scale : 1;
		desc.hdr = pass->m_hdr;
		for (size_t input = 0; input < pass->m_inputs.size() && input < 4; ++input)
		{
			const AsciiString& name = pass->m_inputs[input];
			if (name.compareNoCase("Scene") == 0)
			{
				desc.inputs[input] = D3D8ON12_PP_INPUT_SCENE;
			}
			else if (name.compareNoCase("Depth") == 0)
			{
				desc.inputs[input] = D3D8ON12_PP_INPUT_DEPTH;
			}
			else if (name.compareNoCase("None") == 0)
			{
				desc.inputs[input] = D3D8ON12_PP_INPUT_NONE;
			}
			else if (name.startsWithNoCase("Texture:"))
			{
				TextureClass *texture = WW3DAssetManager::Get_Instance()->Get_Texture(name.str() + 8, MIP_LEVELS_1, WW3D_FORMAT_UNKNOWN, false,
					TextureBaseClass::TEX_REGULAR, false);
				if (texture != nullptr && !texture->Is_Initialized())
					texture->Init();
				if (texture == nullptr || texture->Peek_D3D_Base_Texture() == nullptr)
				{
					REF_PTR_RELEASE(texture);
					m_error.format("PostProcess: pass %s: cannot load texture %s", pass->m_name.str(), name.str() + 8);
					break;
				}
				m_textures.push_back(texture);
				desc.inputs[input] = D3D8ON12_PP_INPUT_TEXTURE;
				desc.textures[input] = texture->Peek_D3D_Base_Texture();
			}
			else
			{
				size_t earlier = 0;
				while (earlier < names.size() && names[earlier].compareNoCase(name) != 0)
					++earlier;
				if (earlier == names.size())
				{
					m_error.format("PostProcess: pass %s: input %s is neither Scene, Depth, None, Texture:<file> nor an earlier pass", pass->m_name.str(), name.str());
					break;
				}
				desc.inputs[input] = D3D8ON12_PP_INPUT_PASS + (UINT)earlier;
			}
		}
		passes.push_back(desc);
		names.push_back(pass->m_name);
	}

	if (m_error.isEmpty() && FAILED(postProcess->Set_Chain(passes.empty() ? nullptr : &passes[0], (UINT)passes.size())))
		m_error = "PostProcess: the renderer did not accept the chain";

	if (m_error.isNotEmpty())
	{
		DEBUG_LOG(("%s", m_error.str()));
		postProcess->Set_Chain(nullptr, 0);
		return;
	}
	m_passNames = names;

	for (size_t index = 0; index < names.size(); ++index)
	{
		const PostProcessPassTemplate *pass = ThePostProcessSettings->findPass(names[index]);
		for (size_t i = 0; i < pass->m_parameters.size(); ++i)
		{
			const PostProcessParameter& parameter = pass->m_parameters[i];
			if (FAILED(postProcess->Set_Parameter((UINT)index, parameter.m_name.str(), parameter.m_values, parameter.m_count)))
				DEBUG_LOG(("PostProcess: pass %s has no parameter %s", names[index].str(), parameter.m_name.str()));
		}
	}
	DEBUG_LOG(("PostProcess: %d passes", (Int)names.size()));
#endif
}

//-------------------------------------------------------------------------------------------------
/** Changes a parameter of a running pass (not the INI). */
//-------------------------------------------------------------------------------------------------
Bool W3DPostProcess::setParameter(const AsciiString& pass, const AsciiString& name, const Real *values, Int count)
{
#ifdef RTS_RENDERER_DX12
	if (m_postProcess == nullptr || m_postProcess != DX8Wrapper::Get_Post_Process())
		return FALSE;
	for (size_t index = 0; index < m_passNames.size(); ++index)
		if (m_passNames[index].compareNoCase(pass) == 0)
			return SUCCEEDED(m_postProcess->Set_Parameter((UINT)index, name.str(), values, count));
#endif
	return FALSE;
}
