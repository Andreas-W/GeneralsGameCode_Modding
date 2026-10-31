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

// Materials.cpp

#include "Materials.h"
#include "Device.h"
#include "PostProcess.h"

#include <string.h>

namespace D3D8On12
{

// What every material shader includes. It describes what Draw_Geometry gives a draw with a material:
// the generated vertex shader's outputs with the view position and normal (VertexShaderKey::materialOutputs),
// the fixed function constants and the textures of stages 0..5.
static const char s_header[] = R"(
struct Light
{
	float4 Diffuse;
	float4 Specular;
	float4 Ambient;
	float4 Position;		// view space, w = 1 point, 2 spot, 3 directional
	float4 Direction;		// view space, towards the light; w = range
	float4 Attenuation;
	float4 Spot;
};
cbuffer VertexConstants : register(b0)
{
	row_major float4x4 World;
	row_major float4x4 WorldView;
	row_major float4x4 Projection;
	row_major float4x4 TexTransform[8];
	float4 Viewport;
	float4 GlobalAmbient;
	float4 MaterialDiffuse;
	float4 MaterialAmbient;
	float4 MaterialSpecular;
	float4 MaterialEmissive;
	float4 MaterialPower;
	float4 Fog;
	int4 LightCount;
	Light Lights[8];
	float4 PointSize;
	float4 PointScale;
};
cbuffer PixelConstants : register(b1)
{
	float4 TextureFactor;
	float4 FogColor;
	float4 BumpMatrix[8];
	float4 BumpLuminance[8];
	float4 AlphaRef;
	float4 ShaderConstants[8];
	float4 MaterialTime;	// x = seconds
	int4 MaterialFlags;		// x = stages with a texture (bits), y = lit, z = alpha test function, w = specular on
};
Texture2D BaseTexture : register(t0);
Texture2D DetailTexture : register(t1);
Texture2D NormalMap : register(t2);
Texture2D SpecularMap : register(t3);
Texture2D EmissiveMap : register(t4);
Texture2D ExtraMap : register(t5);
SamplerState BaseSampler : register(s0);
SamplerState DetailSampler : register(s1);
SamplerState NormalSampler : register(s2);
SamplerState SpecularSampler : register(s3);
SamplerState EmissiveSampler : register(s4);
SamplerState ExtraSampler : register(s5);
Texture2D<float> SceneDepth : register(t6);	// depth drawn before Capture_Scene, 0 (near) .. 1 (far)
Texture2D SceneColor : register(t7);		// the scene drawn before Capture_Scene(TRUE)
SamplerState SceneDepthSampler : register(s6);
SamplerState SceneColorSampler : register(s7);
struct MaterialInput
{
	float4 position : SV_Position;
	float4 diffuse : COLOR0;		// the fixed function vertex lighting
	float4 specular : COLOR1;
	float fog : FOG;				// 1 = no fog
	float4 tex0 : TEXCOORD0;		// texture coordinates of stage 0, with its texture transform
	float4 tex1 : TEXCOORD1;
	float3 viewPosition : VIEWPOS;
	float3 viewNormal : VIEWNORMAL;	// zero for meshes without normals
};
static const uint MAP_BASE = 0;
static const uint MAP_DETAIL = 1;
static const uint MAP_NORMAL = 2;
static const uint MAP_SPECULAR = 3;
static const uint MAP_EMISSIVE = 4;
static const uint MAP_EXTRA = 5;
static const uint MAP_SCENE_DEPTH = 6;
static const uint MAP_SCENE_COLOR = 7;
#define Time (MaterialTime.x)
bool HasMap(uint slot) { return ((uint)MaterialFlags.x >> slot) & 1; }
bool IsLit() { return MaterialFlags.y != 0; }
bool HasSceneDepth() { return HasMap(MAP_SCENE_DEPTH); }
bool HasSceneColor() { return HasMap(MAP_SCENE_COLOR); }
// Where the pixel is on the screen, 0..1, for SceneDepth and SceneColor.
float2 ScreenUV(MaterialInput input)
{
	float width, height;
	SceneColor.GetDimensions(width, height);
	if (width <= 0)
		SceneDepth.GetDimensions(width, height);
	return input.position.xy / max(float2(width, height), 1);
}
// Distance in front of the camera of a view space point (W3D cameras look down -z, so this follows
// the projection's w).
float ViewDistance(float viewZ) { return viewZ * Projection._34 + Projection._44; }
// Distance from the camera of what was drawn at that screen point before the capture.
float SceneViewDepth(float2 uv)
{
	float depth = SceneDepth.SampleLevel(SceneDepthSampler, uv, 0);
	// depth = (z * _33 + _43) / (z * _34 + _44), solved for the view z.
	float viewZ = (Projection._43 - depth * Projection._44) / (depth * Projection._34 - Projection._33);
	return ViewDistance(viewZ);
}
// How far behind the pixel the captured scene is, in view units; large without a capture.
float DepthBehind(MaterialInput input)
{
	return HasSceneDepth() ? SceneViewDepth(ScreenUV(input)) - ViewDistance(input.viewPosition.z) : 1e6;
}
float3 SceneColorAt(float2 uv) { return SceneColor.SampleLevel(SceneColorSampler, uv, 0).rgb; }
// The world position, for draws whose world transform is the identity (terrain, water).
float3 WorldPosition(MaterialInput input)
{
	return mul((float3x3)WorldView, input.viewPosition - WorldView[3].xyz);
}
// A world direction in view space, for draws whose world transform is the identity.
float3 ViewDirection(float3 worldDirection) { return mul(worldDirection, (float3x3)WorldView); }

struct LightSum
{
	float3 ambient;
	float3 diffuse;
	float3 specular;
};
// The lights at a point, as the fixed function vertex shader adds them up, for another normal.
LightSum SumLights(float3 viewPosition, float3 normal, float power)
{
	LightSum sum = (LightSum)0;
	float3 toEye = normalize(-viewPosition);
	[loop] for (int i = 0; i < LightCount.x; ++i)
	{
		Light light = Lights[i];
		float3 toLight = light.Direction.xyz;
		float attenuation = 1;
		if (light.Position.w != 3)
		{
			float3 delta = light.Position.xyz - viewPosition;
			float distance = length(delta);
			toLight = delta / max(distance, 1e-6);
			attenuation = distance > light.Direction.w ? 0 : 1 / max(light.Attenuation.x + light.Attenuation.y * distance + light.Attenuation.z * distance * distance, 1e-6);
			if (light.Position.w == 2)
			{
				float rho = dot(toLight, light.Direction.xyz);
				attenuation *= rho > light.Spot.x ? 1 : (rho <= light.Spot.y ? 0 : pow(saturate((rho - light.Spot.y) / max(light.Spot.x - light.Spot.y, 1e-6)), light.Attenuation.w));
			}
		}
		float nDotL = dot(normal, toLight);
		sum.ambient += attenuation * light.Ambient.rgb;
		if (nDotL > 0)
		{
			sum.diffuse += attenuation * nDotL * light.Diffuse.rgb;
			sum.specular += attenuation * pow(max(dot(normal, normalize(toLight + toEye)), 0), power) * light.Specular.rgb;
		}
	}
	return sum;
}
float3 LitColor(LightSum sum)
{
	return MaterialEmissive.rgb + MaterialAmbient.rgb * (GlobalAmbient.rgb + sum.ambient) + MaterialDiffuse.rgb * sum.diffuse;
}
// The vertex lighting relit for another normal (a normal map): it changes by as much as the light at the
// pixel changes, so with the vertex normal it is exactly the fixed function color.
float3 Relight(MaterialInput input, float3 normal)
{
	if (!IsLit() || dot(input.viewNormal, input.viewNormal) < 1e-6)
		return input.diffuse.rgb;
	float3 before = LitColor(SumLights(input.viewPosition, normalize(input.viewNormal), 1));
	float3 after = LitColor(SumLights(input.viewPosition, normal, 1));
	return saturate(input.diffuse.rgb * after / max(before, 0.001));
}
// Highlights of the lights for a normal; power is the sharpness.
float3 SpecularLight(MaterialInput input, float3 normal, float power)
{
	if (!IsLit())
		return float3(0, 0, 0);
	return SumLights(input.viewPosition, normal, power).specular;
}
// Tangent, bitangent and normal from the screen space derivatives of position and texture coordinates,
// for tangent space normal maps without tangents in the mesh.
float3x3 TangentFrame(float3 viewPosition, float3 normal, float2 uv)
{
	float3 dp1 = ddx(viewPosition);
	float3 dp2 = ddy(viewPosition);
	float2 duv1 = ddx(uv);
	float2 duv2 = ddy(uv);
	float3 dp2perp = cross(dp2, normal);
	float3 dp1perp = cross(normal, dp1);
	float3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
	float3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;
	float scale = rsqrt(max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 1e-12));
	return float3x3(tangent * scale, bitangent * scale, normal);
}
// The fixed function end of the pixel shader: the lit color with the vertex specular, clamped to 0..1 like
// the fixed function stages; then glow, which may go above 1 (for bloom with an HDR scene); fog and the
// alpha test.
float4 Finish(float4 color, float3 glow, MaterialInput input)
{
	if (MaterialFlags.w != 0)
		color.rgb += input.specular.rgb;
	color.rgb = saturate(color.rgb) + glow;
	color.rgb = lerp(FogColor.rgb, color.rgb, saturate(input.fog));
	switch (MaterialFlags.z)
	{
	case 1: discard; break;
	case 2: if (!(color.a < AlphaRef.x)) discard; break;
	case 3: if (!(color.a == AlphaRef.x)) discard; break;
	case 4: if (!(color.a <= AlphaRef.x)) discard; break;
	case 5: if (!(color.a > AlphaRef.x)) discard; break;
	case 6: if (!(color.a != AlphaRef.x)) discard; break;
	case 7: if (!(color.a >= AlphaRef.x)) discard; break;
	default: break;
	}
	return color;
}
)";

MaterialSystem::MaterialSystem(Device* device)
	: m_device(device),
		m_current(0),
		m_sceneDepth(0),
		m_sceneColor(0),
		m_pointSampler(0),
		m_linearSampler(0)
{
	memset(m_maps, 0, sizeof(m_maps));
	QueryPerformanceFrequency(&m_frequency);
	QueryPerformanceCounter(&m_startTime);
}

HRESULT STDMETHODCALLTYPE MaterialSystem::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_ID3D8On12Materials)
	{
		AddRef();
		*object = static_cast<ID3D8On12Materials*>(this);
		return S_OK;
	}
	return m_device->QueryInterface(riid, object);
}

ULONG STDMETHODCALLTYPE MaterialSystem::AddRef()
{
	return m_device->AddRef();
}

ULONG STDMETHODCALLTYPE MaterialSystem::Release()
{
	return m_device->Release();
}

HRESULT STDMETHODCALLTYPE MaterialSystem::Create_Shader(const char* source, UINT length, const char* name, ID3DInclude* include,
	UINT* shader, ID3DBlob** errors)
{
	if (source == nullptr || shader == nullptr)
		return D3DERR_INVALIDCALL;
	*shader = 0;
	Shader compiled;
	compiled.used = true;
	compiled.paramsSize = 0;
	const HRESULT hr = Compile_Pixel_Shader(source, length, name, "Material.hlsli", s_header, include, compiled.code, errors);
	if (FAILED(hr))
		return hr;
	Reflect_Constant_Buffer(compiled.code.Get(), "Params", compiled.paramsSize, compiled.variables);
	// Its own range of hashes, apart from the generated and translated pixel shaders.
	compiled.hash = Hash_Bytes(compiled.code->GetBufferPointer(), compiled.code->GetBufferSize()) ^ 0x2545f4914f6cdd1dull;

	for (size_t i = 0; i < m_shaders.size(); ++i)
	{
		if (!m_shaders[i].used)
		{
			m_shaders[i] = compiled;
			*shader = (UINT)i + 1;
			return D3D_OK;
		}
	}
	m_shaders.push_back(compiled);
	*shader = (UINT)m_shaders.size();
	return D3D_OK;
}

// Pipelines made with the shader stay in the device's cache; they are keyed by the code's hash.
void STDMETHODCALLTYPE MaterialSystem::Release_Shader(UINT shader)
{
	if (shader == 0 || shader > m_shaders.size())
		return;
	Shader released;
	released.used = false;
	released.hash = 0;
	released.paramsSize = 0;
	m_shaders[shader - 1] = released;
}

HRESULT STDMETHODCALLTYPE MaterialSystem::Create_Material(UINT shader, UINT* material)
{
	if (material == nullptr)
		return D3DERR_INVALIDCALL;
	*material = 0;
	if (shader == 0 || shader > m_shaders.size() || !m_shaders[shader - 1].used)
		return D3DERR_INVALIDCALL;
	Material created;
	created.used = true;
	created.shader = shader;
	created.params.assign(m_shaders[shader - 1].paramsSize, 0);
	for (size_t i = 0; i < m_materials.size(); ++i)
	{
		if (!m_materials[i].used)
		{
			m_materials[i] = created;
			*material = (UINT)i + 1;
			return D3D_OK;
		}
	}
	m_materials.push_back(created);
	*material = (UINT)m_materials.size();
	return D3D_OK;
}

void STDMETHODCALLTYPE MaterialSystem::Release_Material(UINT material)
{
	if (material == 0 || material > m_materials.size())
		return;
	m_materials[material - 1].used = false;
	m_materials[material - 1].params.clear();
	if (m_current == material)
		Set_Material(0, nullptr);
}

HRESULT STDMETHODCALLTYPE MaterialSystem::Set_Material_Parameter(UINT material, const char* name, const float* values, UINT count)
{
	if (material == 0 || material > m_materials.size() || !m_materials[material - 1].used || name == nullptr || values == nullptr)
		return D3DERR_INVALIDCALL;
	Material& target = m_materials[material - 1];
	const Shader& shader = m_shaders[target.shader - 1];
	auto found = shader.variables.find(name);
	if (found == shader.variables.end())
		return E_INVALIDARG;
	const UINT size = min(count * (UINT)sizeof(float), found->second.size);
	memcpy(target.params.data() + found->second.offset, values, size);
	return D3D_OK;
}

void STDMETHODCALLTYPE MaterialSystem::Set_Material(UINT material, IDirect3DBaseTexture8* const* maps)
{
	m_current = material <= m_materials.size() && material != 0 && m_materials[material - 1].used ? material : 0;
	for (UINT i = 0; i < 4; ++i)
		m_maps[i] = m_current != 0 && maps != nullptr ? maps[i] : nullptr;
}

bool MaterialSystem::Current(Binding& binding) const
{
	if (m_current == 0)
		return false;
	const Material& material = m_materials[m_current - 1];
	const Shader& shader = m_shaders[material.shader - 1];
	if (!shader.used)
		return false;
	binding.code = shader.code.Get();
	binding.hash = shader.hash;
	binding.params = &material.params;
	binding.maps = m_maps;
	return true;
}

HRESULT STDMETHODCALLTYPE MaterialSystem::Capture_Scene(BOOL color)
{
	UINT depth = 0;
	UINT scene = 0;
	if (!m_device->Get_Post_Processor()->Capture_For_Materials(color != FALSE, depth, scene))
		return E_FAIL;
	m_sceneDepth = depth;
	m_sceneColor = scene;
	return D3D_OK;
}

// Point sampling for the depth (t6), linear for the colour (t7), both clamped.
UINT MaterialSystem::Scene_Sampler(UINT stage)
{
	UINT& sampler = stage == 6 ? m_pointSampler : m_linearSampler;
	if (sampler == 0)
	{
		D3D12_SAMPLER_DESC desc = {};
		desc.Filter = stage == 6 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		desc.AddressU = desc.AddressV = desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		desc.MaxLOD = D3D12_FLOAT32_MAX;
		desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		sampler = m_device->Find_Sampler(desc);
	}
	return sampler;
}

float MaterialSystem::Time() const
{
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (float)((double)(now.QuadPart - m_startTime.QuadPart) / (double)m_frequency.QuadPart);
}

} // namespace D3D8On12
