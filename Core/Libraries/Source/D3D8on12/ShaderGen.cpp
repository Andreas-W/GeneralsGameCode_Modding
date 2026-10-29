/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 Electronic Arts Inc.
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

// ShaderGen.cpp
// Generates the HLSL that emulates the D3D8 fixed function pipeline: transform, lighting, texture
// coordinate generation and fog in the vertex shader, the texture stage combiners, specular, fog and
// the alpha test in the pixel shader.

#include "Pipeline.h"

#include <stdarg.h>
#include <stdio.h>

namespace D3D8On12
{

static UINT32 Hash_Mix(UINT32 hash, UINT32 word)
{
	word *= 0xcc9e2d51u;
	word = _rotl(word, 15);
	word *= 0x1b873593u;
	hash ^= word;
	hash = _rotl(hash, 13);
	return hash * 5 + 0xe6546b64u;
}

static UINT32 Hash_Finish(UINT32 hash)
{
	hash ^= hash >> 16;
	hash *= 0x85ebca6bu;
	hash ^= hash >> 13;
	hash *= 0xc2b2ae35u;
	hash ^= hash >> 16;
	return hash;
}

// MurmurHash3 words with two seeds for 64 bits. It runs for every draw, and works on 32 bit words as
// x86 has no fast 64 bit multiply.
UINT64 Hash_Bytes(const void* data, size_t size)
{
	const unsigned char* bytes = (const unsigned char*)data;
	UINT32 low = 0x9e3779b9u;
	UINT32 high = 0x7f4a7c15u;
	size_t i = 0;
	for (; i + 4 <= size; i += 4)
	{
		UINT32 word;
		memcpy(&word, bytes + i, 4);
		low = Hash_Mix(low, word);
		high = Hash_Mix(high, word ^ 0x5bd1e995u);
	}
	UINT32 tail = 0;
	for (size_t shift = 0; i < size; ++i, shift += 8)
		tail |= (UINT32)bytes[i] << shift;
	low = Hash_Mix(low, tail ^ (UINT32)size);
	high = Hash_Mix(high, tail + (UINT32)size);
	return ((UINT64)Hash_Finish(high) << 32) | Hash_Finish(low ^ high);
}

static void Append(std::string& text, const char* format, ...)
{
	char line[512];
	va_list args;
	va_start(args, format);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
	va_end(args);
	text += line;
}

// ----------------------------------------------------------------------------
// Vertex formats
// ----------------------------------------------------------------------------

static UINT Get_Position_Floats(DWORD fvf)
{
	switch (fvf & D3DFVF_POSITION_MASK)
	{
	case D3DFVF_XYZ:		return 3;
	case D3DFVF_XYZRHW:		return 4;
	case D3DFVF_XYZB1:		return 4;
	case D3DFVF_XYZB2:		return 5;
	case D3DFVF_XYZB3:		return 6;
	case D3DFVF_XYZB4:		return 7;
	case D3DFVF_XYZB5:		return 8;
	}
	return 0;
}

static UINT Get_Tex_Coord_Count(DWORD fvf)
{
	return (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
}

static UINT Get_Tex_Coord_Size(DWORD fvf, UINT set)
{
	switch ((fvf >> (set * 2 + 16)) & 3)
	{
	case D3DFVF_TEXTUREFORMAT1:	return 1;
	case D3DFVF_TEXTUREFORMAT3:	return 3;
	case D3DFVF_TEXTUREFORMAT4:	return 4;
	}
	return 2;
}

UINT Get_FVF_Size(DWORD fvf)
{
	UINT size = Get_Position_Floats(fvf) * 4;
	if (fvf & D3DFVF_NORMAL)
		size += 12;
	if (fvf & D3DFVF_PSIZE)
		size += 4;
	if (fvf & D3DFVF_DIFFUSE)
		size += 4;
	if (fvf & D3DFVF_SPECULAR)
		size += 4;
	for (UINT set = 0; set < Get_Tex_Coord_Count(fvf); ++set)
		size += Get_Tex_Coord_Size(fvf, set) * 4;
	return size;
}

static const char* s_texCoordSemantics[] = { "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD", "TEXCOORD" };

void Get_Input_Layout(DWORD fvf, std::vector<D3D12_INPUT_ELEMENT_DESC>& elements)
{
	elements.clear();
	UINT offset = 0;

	D3D12_INPUT_ELEMENT_DESC element = {};
	element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

	const bool rhw = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
	element.SemanticName = "POSITION";
	element.Format = rhw ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R32G32B32_FLOAT;
	element.AlignedByteOffset = offset;
	elements.push_back(element);
	offset += Get_Position_Floats(fvf) * 4;	// blend weights are skipped

	if (fvf & D3DFVF_NORMAL)
	{
		element.SemanticName = "NORMAL";
		element.Format = DXGI_FORMAT_R32G32B32_FLOAT;
		element.AlignedByteOffset = offset;
		elements.push_back(element);
		offset += 12;
	}
	if (fvf & D3DFVF_PSIZE)
	{
		element.SemanticName = "PSIZE";
		element.Format = DXGI_FORMAT_R32_FLOAT;
		element.AlignedByteOffset = offset;
		elements.push_back(element);
		offset += 4;
	}
	if (fvf & D3DFVF_DIFFUSE)
	{
		element.SemanticName = "COLOR";
		element.SemanticIndex = 0;
		element.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		element.AlignedByteOffset = offset;
		elements.push_back(element);
		offset += 4;
	}
	if (fvf & D3DFVF_SPECULAR)
	{
		element.SemanticName = "COLOR";
		element.SemanticIndex = 1;
		element.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		element.AlignedByteOffset = offset;
		elements.push_back(element);
		offset += 4;
	}
	static const DXGI_FORMAT float_formats[] =
	{
		DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT
	};
	for (UINT set = 0; set < Get_Tex_Coord_Count(fvf); ++set)
	{
		const UINT size = Get_Tex_Coord_Size(fvf, set);
		element.SemanticName = s_texCoordSemantics[set];
		element.SemanticIndex = set;
		element.Format = float_formats[size - 1];
		element.AlignedByteOffset = offset;
		elements.push_back(element);
		offset += size * 4;
	}
}

// ----------------------------------------------------------------------------
// Vertex shader
// ----------------------------------------------------------------------------

static const char* s_vertexShaderHeader =
	"struct Light\n"
	"{\n"
	"	float4 Diffuse;\n"
	"	float4 Specular;\n"
	"	float4 Ambient;\n"
	"	float4 Position;\n"
	"	float4 Direction;\n"
	"	float4 Attenuation;\n"
	"	float4 Spot;\n"
	"};\n"
	"cbuffer VertexConstants : register(b0)\n"
	"{\n"
	"	row_major float4x4 World;\n"
	"	row_major float4x4 WorldView;\n"
	"	row_major float4x4 Projection;\n"
	"	row_major float4x4 TexTransform[8];\n"
	"	float4 Viewport;\n"
	"	float4 GlobalAmbient;\n"
	"	float4 MaterialDiffuse;\n"
	"	float4 MaterialAmbient;\n"
	"	float4 MaterialSpecular;\n"
	"	float4 MaterialEmissive;\n"
	"	float4 MaterialPower;\n"
	"	float4 Fog;\n"
	"	int4 LightCount;\n"
	"	Light Lights[8];\n"
	"	float4 PointSize;\n"
	"	float4 PointScale;\n"
	"};\n";

// The outputs of the vertex shader, which are also the inputs of the geometry shader.
static void Append_Vertex_Output(std::string& text, const VertexShaderKey& key)
{
	text += "struct VSOutput\n{\n	float4 position : SV_Position;\n	float4 diffuse : COLOR0;\n	float4 specular : COLOR1;\n	float fog : FOG;\n";
	for (UINT stage = 0; stage < key.stageCount; ++stage)
		Append(text, "	float4 tex%u : TEXCOORD%u;\n", stage, stage);
	if (key.pointSprite)
		text += "	float pointSize : POINTSIZE;\n";
	text += "};\n";
}

static const char* Get_Material_Source(BYTE source, const char* material, bool has_color0, bool has_color1)
{
	if (source == D3DMCS_COLOR1 && has_color0)
		return "vertexDiffuse";
	if (source == D3DMCS_COLOR2 && has_color1)
		return "vertexSpecular";
	return material;
}

std::string Generate_Vertex_Shader(const VertexShaderKey& key)
{
	const DWORD fvf = key.fvf;
	const bool rhw = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
	const bool has_normal = (fvf & D3DFVF_NORMAL) != 0;
	const bool has_color0 = (fvf & D3DFVF_DIFFUSE) != 0;
	const bool has_color1 = (fvf & D3DFVF_SPECULAR) != 0;
	const UINT tex_count = Get_Tex_Coord_Count(fvf);

	std::string text = s_vertexShaderHeader;

	text += "struct VSInput\n{\n";
	text += "	float4 position : POSITION;\n";
	if (has_normal)
		text += "	float3 normal : NORMAL;\n";
	if (fvf & D3DFVF_PSIZE)
		text += "	float pointSize : PSIZE;\n";
	if (has_color0)
		text += "	float4 color0 : COLOR0;\n";
	if (has_color1)
		text += "	float4 color1 : COLOR1;\n";
	for (UINT set = 0; set < tex_count; ++set)
	{
		const UINT size = Get_Tex_Coord_Size(fvf, set);
		if (size == 1)
			Append(text, "	float tex%u : TEXCOORD%u;\n", set, set);
		else
			Append(text, "	float%u tex%u : TEXCOORD%u;\n", size, set, set);
	}
	text += "};\n";

	Append_Vertex_Output(text, key);

	text += "VSOutput main(VSInput input)\n{\n	VSOutput output;\n";
	text += "	float3 viewPosition = float3(0, 0, 0);\n	float3 viewNormal = float3(0, 0, -1);\n";

	if (rhw)
	{
		// Pretransformed vertices are in screen space; undo the viewport transform.
		text +=
			"	float w = input.position.w != 0 ? 1.0 / input.position.w : 1.0;\n"
			"	float2 ndc = float2((input.position.x - Viewport.x) / Viewport.z * 2 - 1, 1 - (input.position.y - Viewport.y) / Viewport.w * 2);\n"
			"	output.position = float4(ndc, input.position.z, 1) * w;\n";
	}
	else
	{
		text +=
			"	float4 viewPosition4 = mul(float4(input.position.xyz, 1), WorldView);\n"
			"	viewPosition = viewPosition4.xyz;\n"
			"	output.position = mul(viewPosition4, Projection);\n";
		if (has_normal)
			text += "	viewNormal = normalize(mul(input.normal, (float3x3)WorldView));\n";
	}
	// D3D8 pixel centers are on integer coordinates, Direct3D 12 ones half a pixel further.
	text += "	output.position.xy += float2(1.0 / Viewport.z, -1.0 / Viewport.w) * output.position.w;\n";

	text += has_color0 ? "	float4 vertexDiffuse = input.color0;\n" : "	float4 vertexDiffuse = float4(1, 1, 1, 1);\n";
	text += has_color1 ? "	float4 vertexSpecular = input.color1;\n" : "	float4 vertexSpecular = float4(0, 0, 0, 0);\n";

	if (key.lighting && !rhw)
	{
		Append(text, "	float4 matDiffuse = %s;\n", Get_Material_Source(key.diffuseSource, "MaterialDiffuse", has_color0, has_color1));
		Append(text, "	float4 matAmbient = %s;\n", Get_Material_Source(key.ambientSource, "MaterialAmbient", has_color0, has_color1));
		Append(text, "	float4 matSpecular = %s;\n", Get_Material_Source(key.specularSource, "MaterialSpecular", has_color0, has_color1));
		Append(text, "	float4 matEmissive = %s;\n", Get_Material_Source(key.emissiveSource, "MaterialEmissive", has_color0, has_color1));
		text +=
			"	float3 diffuseSum = float3(0, 0, 0);\n"
			"	float3 ambientSum = float3(0, 0, 0);\n"
			"	float3 specularSum = float3(0, 0, 0);\n"
			"	float3 toEye = normalize(-viewPosition);\n"
			"	[loop] for (int i = 0; i < LightCount.x; ++i)\n"
			"	{\n"
			"		Light light = Lights[i];\n"
			"		float3 toLight = light.Direction.xyz;\n"
			"		float attenuation = 1;\n"
			"		if (light.Position.w != 3)\n"	// not directional
			"		{\n"
			"			float3 delta = light.Position.xyz - viewPosition;\n"
			"			float distance = length(delta);\n"
			"			toLight = delta / max(distance, 1e-6);\n"
			"			attenuation = distance > light.Direction.w ? 0 : 1 / max(light.Attenuation.x + light.Attenuation.y * distance + light.Attenuation.z * distance * distance, 1e-6);\n"
			"			if (light.Position.w == 2)\n"	// spot
			"			{\n"
			"				float rho = dot(toLight, light.Direction.xyz);\n"
			"				float spot = rho > light.Spot.x ? 1 : (rho <= light.Spot.y ? 0 : pow(saturate((rho - light.Spot.y) / max(light.Spot.x - light.Spot.y, 1e-6)), light.Attenuation.w));\n"
			"				attenuation *= spot;\n"
			"			}\n"
			"		}\n"
			"		float nDotL = dot(viewNormal, toLight);\n"
			"		ambientSum += attenuation * light.Ambient.rgb;\n"
			"		if (nDotL > 0)\n"
			"		{\n"
			"			diffuseSum += attenuation * nDotL * light.Diffuse.rgb;\n"
			"			float3 halfway = normalize(toLight + toEye);\n"
			"			specularSum += attenuation * pow(max(dot(viewNormal, halfway), 0), MaterialPower.x) * light.Specular.rgb;\n"
			"		}\n"
			"	}\n"
			"	output.diffuse.rgb = saturate(matEmissive.rgb + matAmbient.rgb * (GlobalAmbient.rgb + ambientSum) + matDiffuse.rgb * diffuseSum);\n"
			"	output.diffuse.a = saturate(matDiffuse.a);\n"
			"	output.specular = float4(saturate(matSpecular.rgb * specularSum), vertexSpecular.a);\n";
	}
	else
	{
		text += "	output.diffuse = vertexDiffuse;\n	output.specular = vertexSpecular;\n";
	}

	if (key.fogMode == D3DFOG_NONE)
		text += "	output.fog = 1;\n";
	else if (rhw)
		text += "	output.fog = vertexSpecular.a;\n";	// pretransformed vertices carry their fog in the specular alpha
	else
	{
		text += key.rangeFog ? "	float fogDistance = length(viewPosition);\n" : "	float fogDistance = abs(viewPosition.z);\n";
		if (key.fogMode == D3DFOG_LINEAR)
			text += "	output.fog = saturate((Fog.y - fogDistance) / max(Fog.y - Fog.x, 1e-6));\n";
		else if (key.fogMode == D3DFOG_EXP)
			text += "	output.fog = saturate(exp(-Fog.z * fogDistance));\n";
		else
			text += "	output.fog = saturate(exp(-(Fog.z * fogDistance) * (Fog.z * fogDistance)));\n";
	}

	for (UINT stage = 0; stage < key.stageCount; ++stage)
	{
		const VertexShaderKey::Stage& s = key.stages[stage];
		Append(text, "	{\n		float4 coords = float4(0, 0, 0, 1);\n");
		switch (s.texGen)
		{
		case D3DTSS_TCI_CAMERASPACENORMAL >> 16:
			text += "		coords = float4(viewNormal, 1);\n";
			break;
		case D3DTSS_TCI_CAMERASPACEPOSITION >> 16:
			text += "		coords = float4(viewPosition, 1);\n";
			break;
		case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR >> 16:
			text += "		coords = float4(reflect(normalize(viewPosition), viewNormal), 1);\n";
			break;
		default:
			if (s.coordIndex < tex_count)
			{
				// Missing components: the one after the last is 1, the rest 0.
				static const char* padding[] = { "", "%s, 1, 0, 0", "%s, 1, 0", "%s, 1", "%s" };
				const UINT size = Get_Tex_Coord_Size(fvf, s.coordIndex);
				char source[32];
				_snprintf_s(source, sizeof(source), _TRUNCATE, "input.tex%u", s.coordIndex);
				char value[64];
				_snprintf_s(value, sizeof(value), _TRUNCATE, padding[size], source);
				Append(text, "		coords = float4(%s);\n", value);
			}
			break;
		}
		if (s.transformCount != 0)
			Append(text, "		coords = mul(coords, TexTransform[%u]);\n", stage);
		Append(text, "		output.tex%u = coords;\n	}\n", stage);
	}

	if (key.pointSprite)
	{
		text += (fvf & D3DFVF_PSIZE) ? "	float pointSize = input.pointSize;\n" : "	float pointSize = PointSize.x;\n";
		if (key.pointScale)
		{
			text +=
				"	float eyeDistance = length(viewPosition);\n"
				"	pointSize = Viewport.w * pointSize * sqrt(1 / max(PointScale.x + PointScale.y * eyeDistance + PointScale.z * eyeDistance * eyeDistance, 1e-6));\n";
		}
		text += "	output.pointSize = clamp(pointSize, PointSize.y, PointSize.z);\n";
	}

	text += "	return output;\n}\n";
	return text;
}

// Expands each point into a square of its size in pixels, with texture coordinates from (0, 0) at the
// top left to (1, 1) at the bottom right in all stages.
std::string Generate_Geometry_Shader(const VertexShaderKey& key)
{
	std::string text = s_vertexShaderHeader;
	Append_Vertex_Output(text, key);

	text += "struct GSOutput\n{\n	float4 position : SV_Position;\n	float4 diffuse : COLOR0;\n	float4 specular : COLOR1;\n	float fog : FOG;\n";
	for (UINT stage = 0; stage < key.stageCount; ++stage)
		Append(text, "	float4 tex%u : TEXCOORD%u;\n", stage, stage);
	text += "};\n";

	text +=
		"[maxvertexcount(4)]\n"
		"void main(point VSOutput input[1], inout TriangleStream<GSOutput> stream)\n"
		"{\n"
		"	float2 extent = float2(input[0].pointSize / Viewport.z, input[0].pointSize / Viewport.w) * input[0].position.w;\n"
		"	const float2 corners[4] = { float2(-1, 1), float2(1, 1), float2(-1, -1), float2(1, -1) };\n"
		"	const float2 coords[4] = { float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1) };\n"
		"	[unroll] for (int i = 0; i < 4; ++i)\n"
		"	{\n"
		"		GSOutput output;\n"
		"		output.position = input[0].position;\n"
		"		output.position.xy += corners[i] * extent;\n"
		"		output.diffuse = input[0].diffuse;\n"
		"		output.specular = input[0].specular;\n"
		"		output.fog = input[0].fog;\n";
	for (UINT stage = 0; stage < key.stageCount; ++stage)
		Append(text, "		output.tex%u = float4(coords[i], 0, 1);\n", stage);
	text +=
		"		stream.Append(output);\n"
		"	}\n"
		"}\n";
	return text;
}

// ----------------------------------------------------------------------------
// Pixel shader
// ----------------------------------------------------------------------------

static std::string Get_Argument(DWORD arg, UINT stage)
{
	std::string value;
	switch (arg & D3DTA_SELECTMASK)
	{
	case D3DTA_DIFFUSE:		value = "input.diffuse"; break;
	case D3DTA_CURRENT:		value = "current"; break;
	case D3DTA_TEXTURE:		{ char name[16]; _snprintf_s(name, sizeof(name), _TRUNCATE, "texture%u", stage); value = name; } break;
	case D3DTA_TFACTOR:		value = "TextureFactor"; break;
	case D3DTA_SPECULAR:	value = "input.specular"; break;
	case D3DTA_TEMP:		value = "temp"; break;
	default:				value = "current"; break;
	}
	if (arg & D3DTA_ALPHAREPLICATE)
		value = "(" + value + ").aaaa";
	if (arg & D3DTA_COMPLEMENT)
		value = "(1 - " + value + ")";
	return value;
}

static bool Uses_Texture(DWORD arg)
{
	return (arg & D3DTA_SELECTMASK) == D3DTA_TEXTURE;
}

// Expression of a texture stage operation on float4 arguments a1, a2, a0.
static std::string Get_Operation(BYTE op, UINT stage)
{
	char texture[16];
	_snprintf_s(texture, sizeof(texture), _TRUNCATE, "texture%u", stage);
	const std::string t = texture;

	switch (op)
	{
	case D3DTOP_SELECTARG1:					return "a1";
	case D3DTOP_SELECTARG2:					return "a2";
	case D3DTOP_MODULATE:					return "a1 * a2";
	case D3DTOP_MODULATE2X:					return "a1 * a2 * 2";
	case D3DTOP_MODULATE4X:					return "a1 * a2 * 4";
	case D3DTOP_ADD:						return "a1 + a2";
	case D3DTOP_ADDSIGNED:					return "a1 + a2 - 0.5";
	case D3DTOP_ADDSIGNED2X:				return "(a1 + a2 - 0.5) * 2";
	case D3DTOP_SUBTRACT:					return "a1 - a2";
	case D3DTOP_ADDSMOOTH:					return "a1 + a2 - a1 * a2";
	case D3DTOP_BLENDDIFFUSEALPHA:			return "lerp(a2, a1, input.diffuse.a)";
	case D3DTOP_BLENDTEXTUREALPHA:			return "lerp(a2, a1, " + t + ".a)";
	case D3DTOP_BLENDFACTORALPHA:			return "lerp(a2, a1, TextureFactor.a)";
	case D3DTOP_BLENDTEXTUREALPHAPM:		return "a1 + a2 * (1 - " + t + ".a)";
	case D3DTOP_BLENDCURRENTALPHA:			return "lerp(a2, a1, current.a)";
	case D3DTOP_PREMODULATE:				return "a1";
	case D3DTOP_MODULATEALPHA_ADDCOLOR:		return "a1 + a1.a * a2";
	case D3DTOP_MODULATECOLOR_ADDALPHA:		return "a1 * a2 + a1.a";
	case D3DTOP_MODULATEINVALPHA_ADDCOLOR:	return "(1 - a1.a) * a2 + a1";
	case D3DTOP_MODULATEINVCOLOR_ADDALPHA:	return "(1 - a1) * a2 + a1.a";
	case D3DTOP_DOTPRODUCT3:				return "dot(a1.rgb - 0.5, a2.rgb - 0.5) * 4";
	case D3DTOP_MULTIPLYADD:				return "a0 + a1 * a2";
	case D3DTOP_LERP:						return "lerp(a2, a1, a0)";
	case D3DTOP_BUMPENVMAP:
	case D3DTOP_BUMPENVMAPLUMINANCE:		return "current";
	}
	return "a1";
}

static bool Op_Uses_Texture(BYTE op)
{
	return op == D3DTOP_BLENDTEXTUREALPHA || op == D3DTOP_BLENDTEXTUREALPHAPM || op == D3DTOP_BUMPENVMAP || op == D3DTOP_BUMPENVMAPLUMINANCE;
}

std::string Generate_Pixel_Shader(const PixelShaderKey& key)
{
	std::string text =
		"cbuffer PixelConstants : register(b1)\n"
		"{\n"
		"	float4 TextureFactor;\n"
		"	float4 FogColor;\n"
		"	float4 BumpMatrix[8];\n"
		"	float4 BumpLuminance[8];\n"
		"	float4 AlphaRef;\n"
		"};\n";

	for (UINT stage = 0; stage < key.stageCount; ++stage)
	{
		const PixelShaderKey::Stage& s = key.stages[stage];
		if (s.textureType == TEXTURE_2D)
			Append(text, "Texture2D Texture%u : register(t%u);\nSamplerState Sampler%u : register(s%u);\n", stage, stage, stage, stage);
		else if (s.textureType == TEXTURE_CUBE)
			Append(text, "TextureCube Texture%u : register(t%u);\nSamplerState Sampler%u : register(s%u);\n", stage, stage, stage, stage);
	}

	text += "struct PSInput\n{\n	float4 position : SV_Position;\n	float4 diffuse : COLOR0;\n	float4 specular : COLOR1;\n	float fog : FOG;\n";
	for (UINT stage = 0; stage < key.stageCount; ++stage)
		Append(text, "	float4 tex%u : TEXCOORD%u;\n", stage, stage);
	text += "};\n";

	text += "float4 main(PSInput input) : SV_Target\n{\n";
	text += "	float4 current = input.diffuse;\n	float4 temp = float4(0, 0, 0, 0);\n	float2 bumpOffset = float2(0, 0);\n	float bumpScale = 1;\n";

	for (UINT stage = 0; stage < key.stageCount; ++stage)
	{
		const PixelShaderKey::Stage& s = key.stages[stage];
		Append(text, "	// stage %u\n	{\n", stage);

		const bool needs_texture = Uses_Texture(s.colorArg0) || Uses_Texture(s.colorArg1) || Uses_Texture(s.colorArg2)
			|| Uses_Texture(s.alphaArg0) || Uses_Texture(s.alphaArg1) || Uses_Texture(s.alphaArg2)
			|| Op_Uses_Texture(s.colorOp) || Op_Uses_Texture(s.alphaOp);

		if (!needs_texture)
		{
			Append(text, "		float4 texture%u = float4(1, 1, 1, 1);\n", stage);
		}
		else if (s.textureType == TEXTURE_CUBE)
		{
			Append(text, "		float4 texture%u = Texture%u.Sample(Sampler%u, input.tex%u.xyz);\n", stage, stage, stage, stage);
		}
		else if (s.textureType == TEXTURE_2D)
		{
			if (s.projected && s.coordCount >= 2)
			{
				static const char* components = "xyzw";
				Append(text, "		float2 coords = input.tex%u.xy / input.tex%u.%c + bumpOffset;\n", stage, stage, components[s.coordCount - 1]);
			}
			else
			{
				Append(text, "		float2 coords = input.tex%u.xy + bumpOffset;\n", stage);
			}
			Append(text, "		float4 texture%u = Texture%u.Sample(Sampler%u, coords);\n", stage, stage, stage);
			Append(text, "		texture%u.rgb *= bumpScale;\n", stage);
		}
		else
		{
			// A stage without a texture reads the current color as its texture: the engine's screen
			// fades select the texture of an empty stage 0 to get the vertex color.
			Append(text, "		float4 texture%u = current;\n", stage);
		}

		if (s.colorOp == D3DTOP_BUMPENVMAP || s.colorOp == D3DTOP_BUMPENVMAPLUMINANCE)
		{
			// Offsets the texture coordinates of the next stage.
			Append(text, "		bumpOffset = float2(dot(texture%u.rg, BumpMatrix[%u].xz), dot(texture%u.rg, BumpMatrix[%u].yw));\n", stage, stage, stage, stage);
			if (s.colorOp == D3DTOP_BUMPENVMAPLUMINANCE)
				Append(text, "		bumpScale = saturate(texture%u.b * BumpLuminance[%u].x + BumpLuminance[%u].y);\n", stage, stage, stage);
			text += "	}\n";
			continue;
		}
		text += "		bumpOffset = float2(0, 0);\n		bumpScale = 1;\n";

		Append(text, "		float4 result = current;\n");
		{
			Append(text, "		{\n			float4 a0 = %s;\n			float4 a1 = %s;\n			float4 a2 = %s;\n",
				Get_Argument(s.colorArg0, stage).c_str(), Get_Argument(s.colorArg1, stage).c_str(), Get_Argument(s.colorArg2, stage).c_str());
			if (s.colorOp == D3DTOP_DOTPRODUCT3)
				Append(text, "			result = saturate(%s).xxxx;\n", Get_Operation(s.colorOp, stage).c_str());
			else
				Append(text, "			result.rgb = saturate((%s).rgb);\n", Get_Operation(s.colorOp, stage).c_str());
			text += "		}\n";
		}
		if (s.alphaOp != D3DTOP_DISABLE && s.colorOp != D3DTOP_DOTPRODUCT3)
		{
			Append(text, "		{\n			float4 a0 = %s;\n			float4 a1 = %s;\n			float4 a2 = %s;\n",
				Get_Argument(s.alphaArg0, stage).c_str(), Get_Argument(s.alphaArg1, stage).c_str(), Get_Argument(s.alphaArg2, stage).c_str());
			if (s.alphaOp == D3DTOP_DOTPRODUCT3)
				Append(text, "			result.a = saturate(%s);\n", Get_Operation(s.alphaOp, stage).c_str());
			else
				Append(text, "			result.a = saturate((%s).a);\n", Get_Operation(s.alphaOp, stage).c_str());
			text += "		}\n";
		}
		text += (s.resultArg & D3DTA_SELECTMASK) == D3DTA_TEMP ? "		temp = result;\n" : "		current = result;\n";
		text += "	}\n";
	}

	if (key.specular)
		text += "	current.rgb = saturate(current.rgb + input.specular.rgb);\n";
	if (key.fog)
		text += "	current.rgb = lerp(FogColor.rgb, current.rgb, saturate(input.fog));\n";

	switch (key.alphaFunc)
	{
	case D3DCMP_NEVER:			text += "	discard;\n"; break;
	case D3DCMP_LESS:			text += "	if (!(current.a < AlphaRef.x)) discard;\n"; break;
	case D3DCMP_EQUAL:			text += "	if (!(current.a == AlphaRef.x)) discard;\n"; break;
	case D3DCMP_LESSEQUAL:		text += "	if (!(current.a <= AlphaRef.x)) discard;\n"; break;
	case D3DCMP_GREATER:		text += "	if (!(current.a > AlphaRef.x)) discard;\n"; break;
	case D3DCMP_NOTEQUAL:		text += "	if (!(current.a != AlphaRef.x)) discard;\n"; break;
	case D3DCMP_GREATEREQUAL:	text += "	if (!(current.a >= AlphaRef.x)) discard;\n"; break;
	default:					break;
	}

	text += "	return current;\n}\n";
	return text;
}

} // namespace D3D8On12
