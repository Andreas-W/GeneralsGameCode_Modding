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

// FILE: W3DMaterials.cpp /////////////////////////////////////////////////////
// HLSL materials for model meshes, drawn by the Direct3D 12 renderer.
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DMaterials.h"

#include "Common/File.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/INI.h"
#include "GameClient/PostProcess.h"
#include "WW3D2/assetmgr.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/texture.h"

#include <ctype.h>

#ifdef RTS_RENDERER_DX12
#include <d3d8on12_ext.h>
#endif

W3DMaterials *TheW3DMaterials = nullptr;

#ifdef RTS_RENDERER_DX12

static const char *const SHADER_DIRECTORY = "Data\\Shaders\\Material\\";

// Base texture lit per pixel. Without maps it looks like the fixed function pipeline; a normal map (or
// HeightFromColor) bends the lighting, a specular map or Specular adds highlights, an emissive map glows
// and can scroll, pulse and flicker.
static const char s_standardShader[] = R"(
#include "Material.hlsli"
cbuffer Params : register(b2)
{
	float NormalStrength;	// scales the bumps of the normal map, 0 = 1
	float Specular;			// highlight strength without a specular map, or a factor for the map's red
	float Gloss;			// highlight sharpness, 0 = 24 (with a map: the map's green times this, 0 = 64)
	float EmissiveStrength;	// 0 = 1; above 1 feeds bloom
	float3 EmissiveColor;	// tints the emissive map, 0 0 0 = white
	float HeightFromColor;	// bumps from the base texture's brightness, for trying it without normal maps
	float2 ScrollSpeed;		// texture coordinates per second of the emissive map
	float PulseRate;		// pulses per second of the glow, 0 = steady
	float PulseMin;			// darkest point of a pulse, 0..1
	float FlickerRate;		// random brightness changes per second, 0 = none
	float FlipGreen;		// 1 for normal maps with green pointing down (OpenGL convention)
};
float Brightness(float3 color) { return dot(color, float3(0.299, 0.587, 0.114)); }
float Random(float value) { return frac(sin(value * 12.9898) * 43758.5453); }
float4 main(MaterialInput input) : SV_Target
{
	float2 uv = input.tex0.xy;
	float4 base = BaseTexture.Sample(BaseSampler, uv);
	float3 normal = float3(0, 0, -1);
	bool hasNormal = dot(input.viewNormal, input.viewNormal) > 1e-6;
	if (hasNormal)
	{
		normal = normalize(input.viewNormal);
		if (HasMap(MAP_NORMAL) || HeightFromColor > 0)
		{
			float3 bump = float3(0, 0, 1);
			if (HasMap(MAP_NORMAL))
			{
				bump = NormalMap.Sample(NormalSampler, uv).xyz * 2 - 1;
				if (FlipGreen > 0)
					bump.y = -bump.y;
			}
			if (HeightFromColor > 0)
			{
				uint width, height;
				BaseTexture.GetDimensions(width, height);
				float2 texel = 1.0 / float2(max(width, 1), max(height, 1));
				float left = Brightness(BaseTexture.Sample(BaseSampler, uv - float2(texel.x, 0)).rgb);
				float right = Brightness(BaseTexture.Sample(BaseSampler, uv + float2(texel.x, 0)).rgb);
				float up = Brightness(BaseTexture.Sample(BaseSampler, uv - float2(0, texel.y)).rgb);
				float down = Brightness(BaseTexture.Sample(BaseSampler, uv + float2(0, texel.y)).rgb);
				bump.xy += float2(left - right, up - down) * HeightFromColor * 4;
			}
			bump.xy *= NormalStrength > 0 ? NormalStrength : 1;
			normal = normalize(mul(normalize(bump), TangentFrame(input.viewPosition, normal, uv)));
		}
	}

	float4 color = float4(base.rgb * Relight(input, normal), base.a * input.diffuse.a);

	if (hasNormal)
	{
		float strength = Specular;
		float power = Gloss > 0 ? Gloss : 24;
		if (HasMap(MAP_SPECULAR))
		{
			float4 map = SpecularMap.Sample(SpecularSampler, uv);
			strength = map.r * (Specular > 0 ? Specular : 1);
			power = 2 + map.g * (Gloss > 0 ? Gloss : 64);
		}
		if (strength > 0)
			color.rgb += SpecularLight(input, normal, power) * strength;
	}

	float3 glow = float3(0, 0, 0);
	if (HasMap(MAP_EMISSIVE))
	{
		float3 tint = any(EmissiveColor > 0) ? EmissiveColor : float3(1, 1, 1);
		float strength = EmissiveStrength > 0 ? EmissiveStrength : 1;
		if (PulseRate > 0)
			strength *= lerp(saturate(PulseMin), 1, 0.5 + 0.5 * sin(Time * PulseRate * 6.2831853));
		if (FlickerRate > 0)
			strength *= 0.7 + 0.3 * Random(floor(Time * FlickerRate));
		glow = EmissiveMap.Sample(EmissiveSampler, uv + ScrollSpeed * Time).rgb * tint * strength;
	}

	return Finish(color, glow, input);
}
)";

// Particles: the texture times the particle colour, unlit. Soft where it meets the scene behind it,
// glowing, pulsing, scrolling, and bending the scene behind it (heat haze).
static const char s_particleShader[] = R"(
#include "Material.hlsli"
cbuffer Params : register(b2)
{
	float SoftDistance;		// world units over which the particle fades where it meets the scene, 0 = hard edges
	float EmissiveStrength;	// brightness factor, 0 = 1; above 1 feeds bloom
	float PulseRate;		// pulses per second, 0 = steady
	float PulseMin;			// darkest point of a pulse, 0..1
	float2 ScrollSpeed;		// texture coordinates per second
	float Distortion;		// how far the scene behind is bent, in screen fractions (0.02 is strong), 0 = none
	float DistortionSpeed;	// texture coordinates per second the distortion pattern moves
	float Additive;			// set by the game: 1 for additive particle systems (they fade by colour, not alpha)
};
float4 main(MaterialInput input) : SV_Target
{
	float2 uv = input.tex0.xy + ScrollSpeed * Time;
	float4 base = BaseTexture.Sample(BaseSampler, uv);
	float4 color = base * input.diffuse;
	float fade = 1;
	if (SoftDistance > 0)
		fade = saturate(DepthBehind(input) / SoftDistance);
	if (Distortion != 0 && HasSceneColor())
	{
		// The pattern: the normal map if there is one, else the texture's red and green.
		float2 pattern = HasMap(MAP_NORMAL) ? NormalMap.Sample(NormalSampler, uv + DistortionSpeed * Time).xy : base.rg;
		float strength = color.a * fade;
		float2 screen = ScreenUV(input) + (pattern * 2 - 1) * Distortion * strength;
		return Finish(float4(SceneColorAt(screen), strength), float3(0, 0, 0), input);
	}
	float glow = EmissiveStrength > 0 ? EmissiveStrength : 1;
	if (PulseRate > 0)
		glow *= lerp(saturate(PulseMin), 1, 0.5 + 0.5 * sin(Time * PulseRate * 6.2831853));
	color.a *= fade;
	if (Additive > 0)
		color.rgb *= fade;
	float3 lit = color.rgb * min(glow, 1);
	float3 extra = glow > 1 ? color.rgb * (glow - 1) : float3(0, 0, 0);
	return Finish(float4(lit, color.a), extra * (Additive > 0 ? 1 : color.a), input);
}
)";

// Water: the water texture and colour of the map, with waves from two scrolling normal maps (or built-in
// ones), sun glints, a sky reflection that grows at grazing angles, and, from the scene depth, soft
// shores with foam and deeper water that is darker and more opaque.
static const char s_waterShader[] = R"(
#include "Material.hlsli"
cbuffer Params : register(b2)
{
	float WaveScale;		// world units one wave pattern covers, 0 = 160
	float WaveSpeed;		// world units per second the waves move, 0 = 12
	float WaveStrength;		// how much the waves bend the surface, 0 = 0.25
	float Gloss;			// sharpness of the sun glints, 0 = 180
	float3 SkyColor;		// reflected sky overhead, 0 0 0 = 0.35 0.5 0.7
	float GlintStrength;	// brightness of the glints, 0 = 2; above 1 feeds bloom
	float3 HorizonColor;	// reflected sky at the horizon, 0 0 0 = 0.7 0.8 0.9
	float Reflection;		// how much sky is reflected at grazing angles, 0 = 0.4
	float3 DeepColor;		// colour of deep water, 0 0 0 = 0.02 0.08 0.12
	float DepthScale;		// world units of depth (straight down) until the water is deep, 0 = 40
	float ShoreDistance;	// world units of depth over which the water fades in at the shore, 0 = 3
	float FoamDistance;		// world units of depth with foam along the shore, 0 = 4; -1 = no foam
	float FoamStrength;		// 0 = 0.5
	float DeepOpacity;		// how opaque deep water gets, 0 = 0.7
	float3 SunDirection;	// set by the game
	float River;			// set by the game: 1 for rivers (the alpha edge is the extra map)
	float3 SunColor;		// set by the game
	float WavePad;
	float3 AmbientColor;	// set by the game
};
float3 Or(float3 value, float3 fallback) { return dot(value, value) > 0 ? value : fallback; }
float Or(float value, float fallback) { return value != 0 ? value : fallback; }
// Built-in waves where there is no normal map: three octaves of smooth value noise drifting in different
// directions; the slope comes from differences.
float Hash(float2 p) { return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
float ValueNoise(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	float2 u = f * f * (3 - 2 * f);
	return lerp(lerp(Hash(i), Hash(i + float2(1, 0)), u.x), lerp(Hash(i + float2(0, 1)), Hash(i + float2(1, 1)), u.x), u.y);
}
float WaveHeight(float2 p, float t)
{
	return ValueNoise(p * 2.0 + float2(t, 0.6 * t)) * 0.55
		+ ValueNoise(p * 4.3 + float2(-0.7 * t, 1.1 * t)) * 0.3
		+ ValueNoise(p * 9.1 + float2(1.3 * t, -0.4 * t)) * 0.15;
}
float2 BuiltInSlope(float2 p, float t)
{
	const float e = 0.02;
	float h = WaveHeight(p, t);
	return float2(WaveHeight(p + float2(e, 0), t) - h, WaveHeight(p + float2(0, e), t) - h) / e * 0.08;
}
float4 main(MaterialInput input) : SV_Target
{
	float3 world = WorldPosition(input);
	float scale = Or(WaveScale, 160);
	float speed = Or(WaveSpeed, 12);
	float2 p = world.xy / scale;
	float2 drift = float2(1, 0.6) * speed * Time / scale;

	// Wave normal in world space (z up).
	float2 slope;
	if (HasMap(MAP_NORMAL))
	{
		float2 a = NormalMap.Sample(NormalSampler, p + drift).xy * 2 - 1;
		float2 b = NormalMap.Sample(NormalSampler, p * 1.7 - drift * 0.8).xy * 2 - 1;
		slope = (a + b) * 0.5;
	}
	else
	{
		slope = BuiltInSlope(p, Time * speed / scale);
	}
	float strength = Or(WaveStrength, 0.25);
	float3 worldNormal = normalize(float3(-slope * strength, 1));
	float3 normal = ViewDirection(worldNormal);
	float3 toEye = normalize(-input.viewPosition);
	float3 toSun = ViewDirection(normalize(Or(SunDirection, float3(0.3, 0.3, 0.9))));

	// The map's water: texture, colour and transparency as the game draws it.
	float4 base = BaseTexture.Sample(BaseSampler, input.tex0.xy + slope * strength * 0.02);
	float3 color = base.rgb * input.diffuse.rgb;
	// Standing water takes its transparency from the vertex colour (WaterTransparency), rivers from the
	// texture and the alpha edge.
	float alpha = River > 0 ? base.a : input.diffuse.a;
	if (River > 0 && HasMap(MAP_EXTRA))
		alpha *= ExtraMap.Sample(ExtraSampler, input.tex1.xy).a;

	// Deeper water is darker and more opaque; the shore fades in, with foam. The depth is straight down:
	// the distance behind the surface along the view ray, times how steeply the ray goes down.
	float3 up = ViewDirection(float3(0, 0, 1));
	float depth = DepthBehind(input) * abs(dot(normalize(input.viewPosition), up));
	float deep = saturate(depth / Or(DepthScale, 40));
	color = lerp(color, Or(DeepColor, float3(0.02, 0.08, 0.12)), deep * 0.7);
	alpha = lerp(alpha, max(alpha, Or(DeepOpacity, 0.7)), deep);
	alpha *= saturate(depth / Or(ShoreDistance, 3));
	float foam = 0;
	if (FoamDistance >= 0)
	{
		float band = 1 - saturate(depth / Or(FoamDistance, 4));
		float pattern = HasMap(MAP_SPECULAR) ? SpecularMap.Sample(SpecularSampler, p * 3 + drift * 2).r
			: saturate(0.5 + 0.5 * sin((world.x + world.y) * 0.15 + Time * 2) * cos(world.x * 0.11 - Time));
		foam = band * band * pattern * Or(FoamStrength, 0.5);
	}

	// Sky reflection, stronger at grazing angles.
	float facing = saturate(dot(normal, toEye));
	float fresnel = pow(1 - facing, 4) * Or(Reflection, 0.4);
	float3 sky = lerp(Or(SkyColor, float3(0.35, 0.5, 0.7)), Or(HorizonColor, float3(0.7, 0.8, 0.9)), pow(1 - facing, 2));
	if (HasMap(MAP_EMISSIVE))
	{
		float3 reflected = reflect(-toEye, normal);
		sky = EmissiveMap.Sample(EmissiveSampler, reflected.xy * 0.5 + 0.5).rgb;
	}
	color = lerp(color, sky * (Or(AmbientColor, float3(0.4, 0.4, 0.4)) + Or(SunColor, float3(0.8, 0.8, 0.8))), fresnel);
	alpha = saturate(alpha + fresnel * 0.5);

	// Sun glints.
	float3 halfway = normalize(toSun + toEye);
	float glint = pow(saturate(dot(normal, halfway)), Or(Gloss, 180)) * Or(GlintStrength, 2);
	float3 glow = glint * Or(SunColor, float3(1, 1, 1)) * saturate(alpha * 4);

	color = lerp(color, float3(1, 1, 1), foam);
	alpha = max(alpha, foam);
	return Finish(float4(color, alpha), glow, input);
}
)";

// Terrain: the two blended tiles of the terrain texture lit as the game baked it, with bumps from the
// terrain textures' normal maps (or their brightness), highlights from their specular maps, and the
// clouds and light map as the game draws them.
static const char s_terrainShader[] = R"(
#include "Material.hlsli"
cbuffer Params : register(b2)
{
	float HeightFromColor;	// bumps from the brightness where there is no normal map, 0 = 1; -1 = none
	float NormalStrength;	// scales the bumps, 0 = 1
	float Specular;			// highlight strength where there is no specular map, 0 = none
	float Gloss;			// highlight sharpness, 0 = 32 (with a map: the map's green times this, 0 = 64)
	float3 NoiseMapping;	// set by the game: cloud and light map uv = world xy * x (+ y, z for clouds)
	float ExtraBlend;		// set by the game: 1 for the third tile of three-way blends (one tile, alpha from colour)
	float3 SunDirection;	// set by the game
	float FlipGreen;		// 1 for normal maps with green pointing down (OpenGL convention)
	float3 SunColor;		// set by the game
	float ParamPad;
	float3 AmbientColor;	// set by the game
};
float Brightness(float3 color) { return dot(color, float3(0.299, 0.587, 0.114)); }
float4 SampleBlend(Texture2D map, SamplerState state, MaterialInput input, float blend)
{
	float4 first = map.Sample(state, input.tex0.xy);
	if (ExtraBlend > 0)
		return first;
	return lerp(first, map.Sample(state, input.tex1.xy), blend);
}
float4 main(MaterialInput input) : SV_Target
{
	float blend = ExtraBlend > 0 ? 0 : input.diffuse.a;
	float4 base = SampleBlend(BaseTexture, BaseSampler, input, blend);
	if (ExtraBlend <= 0)
		base.rgb = lerp(BaseTexture.Sample(BaseSampler, input.tex0.xy).rgb, DetailTexture.Sample(DetailSampler, input.tex1.xy).rgb, blend);

	// The surface normal from the triangle (the terrain has no normals), facing the camera.
	float3 viewPos = input.viewPosition;
	float3 surface = normalize(cross(ddx(viewPos), ddy(viewPos)));
	if (dot(surface, viewPos) > 0)
		surface = -surface;

	float3 bump = float3(0, 0, 1);
	bool fromMap = false;
	if (HasMap(MAP_NORMAL))
	{
		float4 map = SampleBlend(NormalMap, NormalSampler, input, blend);
		if (map.a > 0.5)
		{
			bump = map.xyz * 2 - 1;
			if (FlipGreen > 0)
				bump.y = -bump.y;
			fromMap = true;
		}
	}
	if (!fromMap && HeightFromColor >= 0)
	{
		// Brightness differences of neighbouring texels; the atlas is 2048 wide.
		float2 texel = float2(1.0 / 2048, 1.0 / 2048);
		float2 uv = input.tex0.xy;
		float left = Brightness(BaseTexture.Sample(BaseSampler, uv - float2(texel.x, 0)).rgb);
		float right = Brightness(BaseTexture.Sample(BaseSampler, uv + float2(texel.x, 0)).rgb);
		float up = Brightness(BaseTexture.Sample(BaseSampler, uv - float2(0, texel.y)).rgb);
		float down = Brightness(BaseTexture.Sample(BaseSampler, uv + float2(0, texel.y)).rgb);
		bump.xy += float2(left - right, up - down) * (HeightFromColor > 0 ? HeightFromColor : 1) * 4;
	}
	bump.xy *= NormalStrength > 0 ? NormalStrength : 1;
	float3 normal = normalize(mul(normalize(bump), TangentFrame(viewPos, surface, input.tex0.xy)));

	// The baked lighting, changed by as much as the sun on the bumped normal differs from the flat one.
	float3 toSun = normalize(ViewDirection(dot(SunDirection, SunDirection) > 0 ? SunDirection : float3(0.3, 0.3, 0.9)));
	float3 sun = dot(SunColor, SunColor) > 0 ? SunColor : float3(0.8, 0.8, 0.8);
	float3 ambient = dot(AmbientColor, AmbientColor) > 0 ? AmbientColor : float3(0.3, 0.3, 0.3);
	float3 flat = ambient + sun * saturate(dot(surface, toSun));
	float3 bumped = ambient + sun * saturate(dot(normal, toSun));
	float3 light = input.diffuse.rgb * bumped / max(flat, 0.05);
	float4 color = float4(base.rgb * light, ExtraBlend > 0 ? base.a * input.diffuse.a : 1);

	// Highlights of the sun.
	float strength = Specular;
	float power = Gloss > 0 ? Gloss : 32;
	if (HasMap(MAP_SPECULAR))
	{
		float4 map = SampleBlend(SpecularMap, SpecularSampler, input, blend);
		if (map.a > 0.5)
		{
			strength = map.r * (Specular > 0 ? Specular : 1);
			power = 2 + map.g * (Gloss > 0 ? Gloss : 64);
		}
	}
	float3 glow = float3(0, 0, 0);
	if (strength > 0)
	{
		float3 toEye = normalize(-viewPos);
		glow = sun * pow(saturate(dot(normal, normalize(toSun + toEye))), power) * strength;
	}

	// Clouds and the light map, as the terrain shaders lay them over the world.
	float2 world = WorldPosition(input).xy;
	if (HasMap(MAP_EMISSIVE))
		color.rgb *= EmissiveMap.Sample(EmissiveSampler, world * NoiseMapping.x + NoiseMapping.yz).rgb;
	if (HasMap(MAP_EXTRA))
		color.rgb *= ExtraMap.Sample(ExtraSampler, world * NoiseMapping.x).rgb;
	return Finish(color, glow, input);
}
)";

struct BuiltInMaterialShader
{
	const char *name;
	const char *source;
	size_t length;
};

static const BuiltInMaterialShader s_builtInShaders[] =
{
	{ "MAT_Standard", s_standardShader, sizeof(s_standardShader) - 1 },
	{ "MAT_Particle", s_particleShader, sizeof(s_particleShader) - 1 },
	{ "MAT_Water", s_waterShader, sizeof(s_waterShader) - 1 },
	{ "MAT_Terrain", s_terrainShader, sizeof(s_terrainShader) - 1 },
};

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
/** #include in a material shader reads from Data\Shaders\Material. */
//-------------------------------------------------------------------------------------------------
class MaterialShaderIncludes : public ID3DInclude
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

//-------------------------------------------------------------------------------------------------
/** Lower case, without the house color prefix "#color#" and without the extension. */
//-------------------------------------------------------------------------------------------------
static std::string baseTextureName(const char *name)
{
	if (name[0] == '#')
	{
		const char *end = strchr(name + 1, '#');
		if (end != nullptr)
			name = end + 1;
	}
	std::string result(name);
	const size_t dot = result.find_last_of('.');
	if (dot != std::string::npos)
		result.erase(dot);
	for (size_t i = 0; i < result.size(); ++i)
		result[i] = (char)tolower((unsigned char)result[i]);
	return result;
}

#endif // RTS_RENDERER_DX12

//-------------------------------------------------------------------------------------------------
W3DMaterials::W3DMaterials() :
	m_materials(nullptr),
	m_built(FALSE),
	m_enabled(TRUE),
	m_bound(FALSE),
	m_collecting(FALSE),
	m_terrainMaterial(-1),
	m_waterMaterial(-1),
	m_particleColor(FALSE)
{
	for (Int i = 0; i < 4; ++i)
		m_waterMaps[i] = nullptr;
}

//-------------------------------------------------------------------------------------------------
W3DMaterials::~W3DMaterials()
{
	if (TheMeshMaterialBinder == this)
		TheMeshMaterialBinder = nullptr;
	release();
	delete TheMaterialSettings;
	TheMaterialSettings = nullptr;
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::init()
{
	loadSettings();
#ifdef RTS_RENDERER_DX12
	TheMeshMaterialBinder = this;
#endif
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::loadSettings()
{
	delete TheMaterialSettings;
	TheMaterialSettings = nullptr;

	INI ini;
	ini.loadFileDirectory("Data\\INI\\Default\\Material", INI_LOAD_OVERWRITE, nullptr, TRUE, TRUE);
	ini.loadFileDirectory("Data\\INI\\Material", INI_LOAD_OVERWRITE, nullptr, TRUE, TRUE);
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::reload()
{
	release();
	loadSettings();
}

//-------------------------------------------------------------------------------------------------
Bool W3DMaterials::isAvailable()
{
	return DX8Wrapper::Get_Materials() != nullptr;
}

//-------------------------------------------------------------------------------------------------
Bool W3DMaterials::isRunning()
{
	return m_built && m_enabled && !m_builtMaterials.empty() && m_materials == DX8Wrapper::Get_Materials();
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::clearEntries()
{
	for (std::map<TextureClass *, Entry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
		for (Int i = 0; i < 4; ++i)
			REF_PTR_RELEASE(it->second.maps[i]);
	m_entries.clear();
	for (std::map<TextureClass *, Entry>::iterator it = m_particleEntries.begin(); it != m_particleEntries.end(); ++it)
		for (Int i = 0; i < 4; ++i)
			REF_PTR_RELEASE(it->second.maps[i]);
	m_particleEntries.clear();
	for (Int i = 0; i < 4; ++i)
		REF_PTR_RELEASE(m_waterMaps[i]);
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::release()
{
#ifdef RTS_RENDERER_DX12
	if (m_materials != nullptr && m_materials == DX8Wrapper::Get_Materials())
	{
		m_materials->Set_Material(0, nullptr);
		for (size_t i = 0; i < m_builtMaterials.size(); ++i)
			m_materials->Release_Material(m_builtMaterials[i].id);
		for (size_t i = 0; i < m_shaders.size(); ++i)
			m_materials->Release_Shader(m_shaders[i]);
	}
#endif
	clearEntries();
	m_shaders.clear();
	m_builtMaterials.clear();
	m_textureMaterial.clear();
	m_particleMaterial.clear();
	m_terrainMaterial = -1;
	m_waterMaterial = -1;
	m_particleColor = FALSE;
	m_materials = nullptr;
	m_built = FALSE;
	m_bound = FALSE;
	m_error.clear();
}

//-------------------------------------------------------------------------------------------------
/** Compiles the shaders and makes the renderer's materials. A material whose shader does not compile
	* is left out; the error is in the log and in getError(). */
//-------------------------------------------------------------------------------------------------
void W3DMaterials::build(ID3D8On12Materials *materials)
{
#ifdef RTS_RENDERER_DX12
	m_materials = materials;
	m_built = TRUE;
	if (TheMaterialSettings == nullptr)
		return;

	MaterialShaderIncludes includes;
	std::map<std::string, unsigned int> compiled;
	const std::vector<MaterialTemplate*>& templates = TheMaterialSettings->m_materials;
	for (size_t index = 0; index < templates.size(); ++index)
	{
		const MaterialTemplate *settings = templates[index];
		std::string shaderName = settings->m_shader.str();
		unsigned int shader = 0;
		std::map<std::string, unsigned int>::const_iterator found = compiled.find(shaderName);
		if (found != compiled.end())
		{
			shader = found->second;
		}
		else
		{
			// A file of the mod first, else a built-in shader.
			AsciiString path;
			path.format("%s%s.hlsl", SHADER_DIRECTORY, settings->m_shader.str());
			UINT length = 0;
			char *fileSource = readShaderFile(path, &length);
			const char *source = fileSource;
			if (source == nullptr)
			{
				for (size_t i = 0; i < ARRAY_SIZE(s_builtInShaders); ++i)
				{
					if (settings->m_shader.compareNoCase(s_builtInShaders[i].name) == 0)
					{
						source = s_builtInShaders[i].source;
						length = (UINT)s_builtInShaders[i].length;
					}
				}
			}
			if (source == nullptr)
			{
				m_error.format("Material %s: shader %s not found (%s)", settings->m_name.str(), settings->m_shader.str(), path.str());
				DEBUG_LOG(("%s", m_error.str()));
				compiled[shaderName] = 0;
				continue;
			}
			ID3DBlob *errors = nullptr;
			if (FAILED(materials->Create_Shader(source, length, settings->m_shader.str(), &includes, &shader, &errors)))
			{
				m_error.format("Material %s: shader %s: %s", settings->m_name.str(), settings->m_shader.str(),
					errors != nullptr ? (const char *)errors->GetBufferPointer() : "could not compile");
				DEBUG_LOG(("%s", m_error.str()));
				shader = 0;
			}
			else
			{
				m_shaders.push_back(shader);
			}
			if (errors != nullptr)
				errors->Release();
			delete[] fileSource;
			compiled[shaderName] = shader;
		}
		if (shader == 0)
			continue;

		Built built;
		built.settings = settings;
		built.id = 0;
		built.textureCount = 0;
		if (FAILED(materials->Create_Material(shader, &built.id)))
			continue;
		for (size_t i = 0; i < settings->m_parameters.size(); ++i)
		{
			const PostProcessParameter& parameter = settings->m_parameters[i];
			if (FAILED(materials->Set_Material_Parameter(built.id, parameter.m_name.str(), parameter.m_values, parameter.m_count)))
				DEBUG_LOG(("Material %s: the shader has no parameter %s", settings->m_name.str(), parameter.m_name.str()));
		}
		m_builtMaterials.push_back(built);
		const Int builtIndex = (Int)m_builtMaterials.size() - 1;
		switch (settings->m_use)
		{
		case MaterialTemplate::USE_PARTICLES:
			for (size_t i = 0; i < settings->m_textures.size(); ++i)
				m_particleMaterial[baseTextureName(settings->m_textures[i].str())] = builtIndex;
			for (size_t i = 0; i < settings->m_parameters.size(); ++i)
				if (settings->m_parameters[i].m_name.compareNoCase("Distortion") == 0 && settings->m_parameters[i].m_values[0] != 0.0f)
					m_particleColor = TRUE;
			break;
		case MaterialTemplate::USE_TERRAIN:
			m_terrainMaterial = builtIndex;
			break;
		case MaterialTemplate::USE_WATER:
			// The maps are file names.
			m_waterMaterial = builtIndex;
			for (Int i = 0; i < 4; ++i)
			{
				REF_PTR_RELEASE(m_waterMaps[i]);
				if (settings->m_maps[i].isNotEmpty() && settings->m_maps[i].getCharAt(0) != '_')
					m_waterMaps[i] = findMap(settings->m_name, settings->m_maps[i]);
			}
			break;
		default:
			for (size_t i = 0; i < settings->m_textures.size(); ++i)
				m_textureMaterial[baseTextureName(settings->m_textures[i].str())] = builtIndex;
			break;
		}
	}
#endif
}

//-------------------------------------------------------------------------------------------------
/** A map of a base texture: a suffix such as "_n" adds to the base name, anything else is a file name.
	* Null when the file does not exist, so that the shader knows to go without. */
//-------------------------------------------------------------------------------------------------
TextureClass *W3DMaterials::findMap(const AsciiString& baseName, const AsciiString& spec)
{
#ifdef RTS_RENDERER_DX12
	if (spec.isEmpty() || spec.compareNoCase("None") == 0)
		return nullptr;
	std::string name = spec.getCharAt(0) == '_' ? std::string(baseName.str()) + spec.str() : baseTextureName(spec.str());
	static const char *const extensions[] = { ".dds", ".tga" };
	for (size_t i = 0; i < ARRAY_SIZE(extensions); ++i)
	{
		AsciiString path;
		path.format("Art\\Textures\\%s%s", name.c_str(), extensions[i]);
		if (TheFileSystem->doesFileExist(path.str()))
		{
			AsciiString file;
			file.format("%s%s", name.c_str(), extensions[i]);
			DEBUG_LOG(("Material: %s uses %s", baseName.str(), path.str()));
			return WW3DAssetManager::Get_Instance()->Get_Texture(file.str());
		}
	}
#endif
	return nullptr;
}

//-------------------------------------------------------------------------------------------------
const W3DMaterials::Entry& W3DMaterials::lookup(TextureClass *baseTexture, const std::map<std::string, Int>& table,
	std::map<TextureClass *, Entry>& entries)
{
	std::map<TextureClass *, Entry>::iterator found = entries.find(baseTexture);
	if (found != entries.end())
		return found->second;

	Entry entry;
	entry.material = -1;
	for (Int i = 0; i < 4; ++i)
		entry.maps[i] = nullptr;
#ifdef RTS_RENDERER_DX12
	const std::string name = baseTextureName(baseTexture->Get_Texture_Name().str());
	std::map<std::string, Int>::const_iterator material = table.find(name);
	if (material != table.end())
	{
		entry.material = material->second;
		Built& built = m_builtMaterials[entry.material];
		++built.textureCount;
		const AsciiString baseName(name.c_str());
		for (Int i = 0; i < 4; ++i)
			entry.maps[i] = findMap(baseName, built.settings->m_maps[i]);
	}
#endif
	return entries[baseTexture] = entry;
}

//-------------------------------------------------------------------------------------------------
ID3D8On12Materials *W3DMaterials::prepare()
{
#ifdef RTS_RENDERER_DX12
	if (!m_enabled || TheMaterialSettings == nullptr)
		return nullptr;
	ID3D8On12Materials *materials = DX8Wrapper::Get_Materials();
	if (materials == nullptr)
		return nullptr;
	if (materials != m_materials || !m_built)
	{
		// First use, or the device was created again.
		release();
		build(materials);
	}
	return materials;
#else
	return nullptr;
#endif
}

//-------------------------------------------------------------------------------------------------
bool W3DMaterials::bindEntry(const Entry& entry)
{
#ifdef RTS_RENDERER_DX12
	if (entry.material < 0)
		return false;
	// The maps go to the renderer directly: the stage textures of DX8Wrapper can be out of date, as the
	// terrain shaders set some on the device themselves.
	IDirect3DBaseTexture8 *maps[4];
	for (Int i = 0; i < 4; ++i)
	{
		maps[i] = nullptr;
		if (entry.maps[i] != nullptr)
		{
			if (!entry.maps[i]->Is_Initialized())
				entry.maps[i]->Init();
			maps[i] = entry.maps[i]->Peek_D3D_Base_Texture();
		}
	}
	m_materials->Set_Material(m_builtMaterials[entry.material].id, maps);
	m_bound = TRUE;
	return true;
#else
	return false;
#endif
}

//-------------------------------------------------------------------------------------------------
bool W3DMaterials::bindParticle(TextureClass *particleTexture, Int additive)
{
	if (particleTexture == nullptr || prepare() == nullptr || m_particleMaterial.empty())
		return false;
	const Entry& entry = lookup(particleTexture, m_particleMaterial, m_particleEntries);
#ifdef RTS_RENDERER_DX12
	if (entry.material >= 0 && additive >= 0)
	{
		const Real value = (Real)additive;
		m_materials->Set_Material_Parameter(m_builtMaterials[entry.material].id, "Additive", &value, 1);
	}
#endif
	return bindEntry(entry);
}

//-------------------------------------------------------------------------------------------------
/** The sun of the terrain lighting, in world space, for materials that light by themselves. */
//-------------------------------------------------------------------------------------------------
void W3DMaterials::setSunParameters(UINT material)
{
#ifdef RTS_RENDERER_DX12
	const Coord3D& position = TheGlobalData->m_terrainLightPos[0];
	Real length = sqrtf(position.x * position.x + position.y * position.y + position.z * position.z);
	if (length <= 0.0f)
		length = 1.0f;
	// The light position points from the light; towards the sun is the other way.
	const Real toSun[3] = { -position.x / length, -position.y / length, -position.z / length };
	const RGBColor& diffuse = TheGlobalData->m_terrainDiffuse[0];
	const RGBColor& ambient = TheGlobalData->m_terrainAmbient[0];
	const Real sunColor[3] = { diffuse.red, diffuse.green, diffuse.blue };
	const Real ambientColor[3] = { ambient.red, ambient.green, ambient.blue };
	m_materials->Set_Material_Parameter(material, "SunDirection", toSun, 3);
	m_materials->Set_Material_Parameter(material, "SunColor", sunColor, 3);
	m_materials->Set_Material_Parameter(material, "AmbientColor", ambientColor, 3);
#endif
}

//-------------------------------------------------------------------------------------------------
bool W3DMaterials::bindTerrain(IDirect3DBaseTexture8 *const *maps)
{
#ifdef RTS_RENDERER_DX12
	if (prepare() == nullptr || m_terrainMaterial < 0)
		return false;
	const UINT id = m_builtMaterials[m_terrainMaterial].id;
	setSunParameters(id);
	m_materials->Set_Material(id, maps);
	m_bound = TRUE;
	return true;
#else
	return false;
#endif
}

//-------------------------------------------------------------------------------------------------
bool W3DMaterials::bindWater(IDirect3DBaseTexture8 *extraMap)
{
#ifdef RTS_RENDERER_DX12
	if (prepare() == nullptr || m_waterMaterial < 0)
		return false;
	const UINT id = m_builtMaterials[m_waterMaterial].id;
	setSunParameters(id);
	IDirect3DBaseTexture8 *maps[4];
	for (Int i = 0; i < 4; ++i)
	{
		maps[i] = nullptr;
		if (m_waterMaps[i] != nullptr)
		{
			if (!m_waterMaps[i]->Is_Initialized())
				m_waterMaps[i]->Init();
			maps[i] = m_waterMaps[i]->Peek_D3D_Base_Texture();
		}
	}
	if (extraMap != nullptr)
		maps[3] = extraMap;
	m_materials->Set_Material(id, maps);
	m_bound = TRUE;
	return true;
#else
	return false;
#endif
}

//-------------------------------------------------------------------------------------------------
Bool W3DMaterials::hasTerrain()
{
	return prepare() != nullptr && m_terrainMaterial >= 0;
}

//-------------------------------------------------------------------------------------------------
Bool W3DMaterials::hasWater()
{
	return prepare() != nullptr && m_waterMaterial >= 0;
}

//-------------------------------------------------------------------------------------------------
const MaterialTemplate *W3DMaterials::getTerrainSettings()
{
	return hasTerrain() ? m_builtMaterials[m_terrainMaterial].settings : nullptr;
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::setTerrainParameter(const char *name, const Real *values, Int count)
{
#ifdef RTS_RENDERER_DX12
	if (m_materials != nullptr && m_terrainMaterial >= 0)
		m_materials->Set_Material_Parameter(m_builtMaterials[m_terrainMaterial].id, name, values, (UINT)count);
#endif
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::setWaterParameter(const char *name, const Real *values, Int count)
{
#ifdef RTS_RENDERER_DX12
	if (m_materials != nullptr && m_waterMaterial >= 0)
		m_materials->Set_Material_Parameter(m_builtMaterials[m_waterMaterial].id, name, values, (UINT)count);
#endif
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::captureBeforeWater()
{
#ifdef RTS_RENDERER_DX12
	if (hasWater())
		m_materials->Capture_Scene(FALSE);
#endif
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::captureBeforeParticles()
{
#ifdef RTS_RENDERER_DX12
	if (prepare() != nullptr && !m_particleMaterial.empty())
		m_materials->Capture_Scene(m_particleColor);
#endif
}

//-------------------------------------------------------------------------------------------------
/** Before the meshes of a texture category: their base texture decides the material. */
//-------------------------------------------------------------------------------------------------
bool W3DMaterials::Begin(TextureClass *baseTexture)
{
#ifdef RTS_RENDERER_DX12
	if (!m_enabled || baseTexture == nullptr)
		return false;
	ID3D8On12Materials *materials = DX8Wrapper::Get_Materials();
	if (materials == nullptr)
		return false;
	if (m_collecting)
		m_seenTextures.insert(baseTextureName(baseTexture->Get_Texture_Name().str()));
	if (prepare() == nullptr)
		return false;
	return bindEntry(lookup(baseTexture, m_textureMaterial, m_entries));
#else
	return false;
#endif
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::End()
{
#ifdef RTS_RENDERER_DX12
	if (!m_bound)
		return;
	if (m_materials != nullptr)
		m_materials->Set_Material(0, nullptr);
	m_bound = FALSE;
#endif
}

//-------------------------------------------------------------------------------------------------
Bool W3DMaterials::setParameter(const AsciiString& material, const AsciiString& name, const Real* values, Int count)
{
#ifdef RTS_RENDERER_DX12
	if (!isRunning())
		return FALSE;
	for (size_t i = 0; i < m_builtMaterials.size(); ++i)
	{
		if (m_builtMaterials[i].settings->m_name.compareNoCase(material) == 0)
			return SUCCEEDED(m_materials->Set_Material_Parameter(m_builtMaterials[i].id, name.str(), values, (UINT)count));
	}
#endif
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::getMaterials(std::vector<AsciiString>& names, std::vector<Int>& meshTextures, std::vector<AsciiString>& uses)
{
	static const char *const useNames[] = { "meshes", "particles", "terrain", "water" };
	names.clear();
	meshTextures.clear();
	uses.clear();
	for (size_t i = 0; i < m_builtMaterials.size(); ++i)
	{
		names.push_back(m_builtMaterials[i].settings->m_name);
		meshTextures.push_back(m_builtMaterials[i].textureCount);
		const Int use = m_builtMaterials[i].settings->m_use;
		uses.push_back(use >= 0 && use < (Int)ARRAY_SIZE(useNames) ? useNames[use] : "");
	}
}

//-------------------------------------------------------------------------------------------------
void W3DMaterials::collectTextures(std::vector<AsciiString>& names)
{
	names.clear();
	for (std::set<std::string>::const_iterator it = m_seenTextures.begin(); it != m_seenTextures.end(); ++it)
		names.push_back(AsciiString(it->c_str()));
	m_seenTextures.clear();
	m_collecting = TRUE;
}
