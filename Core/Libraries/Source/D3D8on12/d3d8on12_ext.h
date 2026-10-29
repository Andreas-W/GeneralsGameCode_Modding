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

// Features of the Direct3D 12 device beyond D3D8. The engine asks the IDirect3DDevice8 for them with
// QueryInterface; a device of d3d8.dll does not have them, so the engine knows to go without.

#pragma once

#include <d3d8.h>
#include <d3dcommon.h>

// {6C2E4D1A-8B3F-4E57-9A61-3F0D2B7C9E41}
DEFINE_GUID(IID_ID3D8On12PostProcess, 0x6c2e4d1a, 0x8b3f, 0x4e57, 0x9a, 0x61, 0x3f, 0x0d, 0x2b, 0x7c, 0x9e, 0x41);

// Where a post-processing pass reads an input from.
enum D3D8ON12_PP_INPUT
{
	D3D8ON12_PP_INPUT_NONE = 0,
	D3D8ON12_PP_INPUT_SCENE = 1,	// the 3D scene as drawn so far
	D3D8ON12_PP_INPUT_TEXTURE = 2,	// the texture of the same slot in D3D8ON12_PP_PASS::textures
	D3D8ON12_PP_INPUT_DEPTH = 3,	// depth of the scene, 0 (near) .. 1 (far), one float per pixel
	D3D8ON12_PP_INPUT_PASS = 16,	// + n: the output of the earlier pass n
};

struct D3D8ON12_PP_PASS
{
	UINT shader;							// from Create_Shader
	UINT inputs[4];							// D3D8ON12_PP_INPUT values for Input0..Input3 of the shader
	IDirect3DBaseTexture8* textures[4];		// for inputs set to D3D8ON12_PP_INPUT_TEXTURE, such as a color lookup table
	UINT scale;								// the output is the screen size divided by this (1, 2, 4 or 8); the last pass draws into the screen
	BOOL hdr;								// 16 bit float output instead of 8 bit
};

// A chain of full screen pixel shader passes over the 3D scene. The last pass draws into the back buffer,
// so what is drawn afterwards (the interface) is not processed.
//
// Shaders are HLSL pixel shaders (ps_5_0) with the entry point main. They include "PostProcess.hlsli",
// which the device provides: Input0..Input3, the samplers LinearClamp and PointClamp, the screen and
// output sizes, the time, the camera (View, Projection and their inverses, as set with Set_Camera),
// helpers to get view and world positions from depth, and the struct PostProcessInput with the
// texture coordinate uv. Parameters are the variables of a cbuffer named Params.
struct ID3D8On12PostProcess : public IUnknown
{
	// Compiles a shader. Other #include files are read through include, which may be null. On failure the
	// compiler messages are returned in errors (when not null) and the result is a failure code.
	virtual HRESULT STDMETHODCALLTYPE Create_Shader(const char* source, UINT length, const char* name, ID3DInclude* include,
		UINT* shader, ID3DBlob** errors) = 0;
	virtual void STDMETHODCALLTYPE Release_Shader(UINT shader) = 0;

	// Replaces the chain; no passes turn post-processing off. Textures are referenced while they are in the chain.
	virtual HRESULT STDMETHODCALLTYPE Set_Chain(const D3D8ON12_PP_PASS* passes, UINT count) = 0;

	// Sets a variable of the Params cbuffer of a pass in the chain; E_INVALIDARG for an unknown name.
	virtual HRESULT STDMETHODCALLTYPE Set_Parameter(UINT pass, const char* name, const float* values, UINT count) = 0;

	// The camera of the scene for the next Run: view and projection matrices as given to SetTransform
	// (row major, row vectors).
	virtual void STDMETHODCALLTYPE Set_Camera(const D3DMATRIX* view, const D3DMATRIX* projection) = 0;

	// Runs the chain over the back buffer, which must be the current render target. time is in seconds.
	virtual HRESULT STDMETHODCALLTYPE Run(float time) = 0;

	// Draws the back buffer in 16 bit float from the next frame on, so that the scene keeps brightness
	// above 1 (additive effects) for the passes, such as a tone curve. The Scene input is then float too;
	// the last pass should write 0..1. Present and back buffer reads clamp it to 8 bit.
	virtual void STDMETHODCALLTYPE Set_HDR_Scene(BOOL enable) = 0;
};

// {9B4F2C71-5E0A-4D83-B6C2-71A8E3D05F16}
DEFINE_GUID(IID_ID3D8On12ShadowMap, 0x9b4f2c71, 0x5e0a, 0x4d83, 0xb6, 0xc2, 0x71, 0xa8, 0xe3, 0xd0, 0x5f, 0x16);

struct D3D8ON12_SHADOW_PARAMS
{
	float color[4];		// r, g, b, a the back buffer is multiplied with where it is fully in shadow
	float toLight[3];	// world space direction towards the light, normalized
	float bias;			// world units a point is moved towards the light before the depth test
	float softness;		// radius of the soft edge in shadow map texels
	BOOL debug;			// write the shadow amount as grey instead of darkening
};

// A depth map of shadow casters seen from the light, which then darkens the scene.
//
// Between Begin_Shadow_Map and End_Shadow_Map, draws go into the shadow map only: depth, no color, the
// whole map as viewport (clears too). Draws that do not write depth, or blend without an alpha test,
// are left out, so glows and glass do not cast shadows. Apply_Shadows then multiplies the back buffer
// where the depth buffer's points are hidden from the light.
struct ID3D8On12ShadowMap : public IUnknown
{
	// depthBias and slopeBias are the rasterizer depth bias of the casters, against shadow acne.
	virtual HRESULT STDMETHODCALLTYPE Begin_Shadow_Map(UINT size, INT depthBias, float slopeBias) = 0;

	// The camera the casters were drawn with: view and projection as given to SetTransform.
	virtual HRESULT STDMETHODCALLTYPE End_Shadow_Map(const D3DMATRIX* view, const D3DMATRIX* projection) = 0;

	// Darkens the back buffer, which must be the current render target, by the last shadow map. view and
	// projection are the scene camera's, to find the world positions of the depth buffer.
	virtual HRESULT STDMETHODCALLTYPE Apply_Shadows(const D3D8ON12_SHADOW_PARAMS* params, const D3DMATRIX* view,
		const D3DMATRIX* projection) = 0;
};
