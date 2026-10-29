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

// Pipeline.h
// Fixed function emulation: the parts of the D3D8 state that decide which shaders and which pipeline
// state object a draw uses, and the constants the generated shaders read.
//
// Keys are compared and hashed as raw bytes, so they are always cleared before they are filled in.

#pragma once

#include "Private.h"

#include <string>

namespace D3D8On12
{

const unsigned FF_STAGES = 8;
const unsigned FF_LIGHTS = 8;

struct VertexShaderKey
{
	DWORD fvf;
	BYTE lighting;				// lighting on and the vertices have normals
	BYTE diffuseSource;			// D3DMCS_*, already resolved to MATERIAL when the vertex color is missing
	BYTE ambientSource;
	BYTE specularSource;
	BYTE emissiveSource;
	BYTE fogMode;				// D3DFOG_*, D3DFOG_NONE when fog is off
	BYTE rangeFog;
	BYTE stageCount;			// stages whose texture coordinates are needed
	BYTE pointSprite;			// points are drawn as textured squares
	BYTE pointScale;			// their size depends on the distance
	BYTE pad[2];
	struct Stage
	{
		BYTE coordIndex;		// texture coordinate set of the vertex
		BYTE texGen;			// D3DTSS_TCI_* >> 16
		BYTE transformCount;	// D3DTTFF_COUNT*, 0 when the texture transform is off
		BYTE pad;
	} stages[FF_STAGES];
};

struct PixelShaderKey
{
	struct Stage
	{
		BYTE colorOp;
		BYTE colorArg0;
		BYTE colorArg1;
		BYTE colorArg2;
		BYTE alphaOp;
		BYTE alphaArg0;
		BYTE alphaArg1;
		BYTE alphaArg2;
		BYTE resultArg;
		BYTE textureType;		// TEXTURE_*
		BYTE projected;			// divide the coordinates by component coordCount - 1
		BYTE coordCount;
	} stages[FF_STAGES];
	BYTE stageCount;			// enabled stages
	BYTE alphaFunc;				// D3DCMP_*, 0 for no alpha test
	BYTE fog;
	BYTE specular;
};

enum TextureType
{
	TEXTURE_NONE,
	TEXTURE_2D,
	TEXTURE_CUBE,
};

struct PipelineKey
{
	UINT64 vertexShader;		// hashes of the shader keys
	UINT64 pixelShader;
	DWORD fvf;
	BYTE topology;				// D3D12_PRIMITIVE_TOPOLOGY_TYPE
	BYTE blendEnable;
	BYTE srcBlend;
	BYTE destBlend;
	BYTE blendOp;
	BYTE colorWrite;
	BYTE depthEnable;
	BYTE depthWrite;
	BYTE depthFunc;
	BYTE stencilEnable;
	BYTE stencilFunc;
	BYTE stencilFail;
	BYTE stencilDepthFail;
	BYTE stencilPass;
	BYTE stencilReadMask;
	BYTE stencilWriteMask;
	BYTE cullMode;
	BYTE fillMode;
	BYTE depthBias;
	BYTE hasDepthBuffer;
	BYTE sampleCount;
	BYTE shadowPass;			// drawn into the shadow map, with its depth bias
	DXGI_FORMAT targetFormat;
	DXGI_FORMAT depthFormat;
	INT shadowDepthBias;
	float shadowSlopeBias;
};

// Constants of the generated vertex shaders, laid out like the cbuffer in ShaderGen.cpp.
struct FFLight
{
	float diffuse[4];
	float specular[4];
	float ambient[4];
	float position[4];			// view space, w = D3DLIGHTTYPE
	float direction[4];			// view space, towards the light; w = range
	float attenuation[4];		// attenuation 0, 1, 2, falloff
	float spot[4];				// cos(theta / 2), cos(phi / 2)
};

struct FFVertexConstants
{
	float world[16];
	float worldView[16];
	float projection[16];
	float texTransform[FF_STAGES][16];
	float viewport[4];			// x, y, width, height of the viewport, for pretransformed vertices
	float globalAmbient[4];
	float materialDiffuse[4];
	float materialAmbient[4];
	float materialSpecular[4];
	float materialEmissive[4];
	float materialPower[4];
	float fog[4];				// start, end, density
	int lightCount[4];
	FFLight lights[FF_LIGHTS];
	float pointSize[4];			// size, minimum, maximum
	float pointScale[4];		// A, B, C
};

struct FFPixelConstants
{
	float textureFactor[4];
	float fogColor[4];
	float bumpMatrix[FF_STAGES][4];	// 00, 01, 10, 11
	float bumpLuminance[FF_STAGES][4];	// scale, offset
	float alphaRef[4];
	float shaderConstants[8][4];	// c0-c7 of pixel shaders
};

// Constants of translated vertex shaders.
struct ShaderVertexConstants
{
	float c[96][4];
	float viewport[4];
};

// What a translated pixel shader depends on besides its byte code.
struct PixelShaderTranslation
{
	struct Stage
	{
		BYTE textureType;		// TEXTURE_*
		BYTE projected;
		BYTE coordCount;
		BYTE pad;
	} stages[FF_STAGES];
	BYTE fog;					// fog and the alpha test follow the shader
	BYTE alphaFunc;
	BYTE pad[2];
	UINT64 function;			// hash of the byte code
};

struct TranslatedShader
{
	bool ok;
	std::string hlsl;
	std::vector<D3D12_INPUT_ELEMENT_DESC> layout;	// vertex shaders
};

UINT64 Hash_Bytes(const void* data, size_t size);

// HLSL of the shaders that emulate the fixed function pipeline for a key.
std::string Generate_Vertex_Shader(const VertexShaderKey& key);
std::string Generate_Geometry_Shader(const VertexShaderKey& key);	// for point sprites
std::string Generate_Pixel_Shader(const PixelShaderKey& key);

// HLSL of D3D8 vertex and pixel shader byte code; ShaderTranslate.cpp. Translated vertex shaders write
// all eight texture coordinates and read c0-c95; translated pixel shaders read all eight.
bool Translate_Vertex_Shader(const DWORD* declaration, const DWORD* function, std::string& hlsl, std::vector<D3D12_INPUT_ELEMENT_DESC>& layout, std::string& error);
bool Translate_Pixel_Shader(const DWORD* function, const PixelShaderTranslation& info, std::string& hlsl, std::string& error);

// The input layout of a flexible vertex format, and the size of one vertex.
void Get_Input_Layout(DWORD fvf, std::vector<D3D12_INPUT_ELEMENT_DESC>& elements);
UINT Get_FVF_Size(DWORD fvf);

} // namespace D3D8On12
