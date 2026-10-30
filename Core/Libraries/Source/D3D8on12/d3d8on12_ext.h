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

#define D3D8ON12_PP_INPUTS 6
#define D3D8ON12_PP_SCALE_PIXEL 0xFFFF

// Where a post-processing pass reads an input from.
enum D3D8ON12_PP_INPUT
{
	D3D8ON12_PP_INPUT_NONE = 0,
	D3D8ON12_PP_INPUT_SCENE = 1,	// the 3D scene as drawn so far
	D3D8ON12_PP_INPUT_TEXTURE = 2,	// the texture of the same slot in D3D8ON12_PP_PASS::textures
	D3D8ON12_PP_INPUT_DEPTH = 3,	// depth of the scene, 0 (near) .. 1 (far), one float per pixel
	D3D8ON12_PP_INPUT_PREVIOUS = 4,	// this pass's own output of the previous frame; its InputSize is 0 when there is none
	D3D8ON12_PP_INPUT_PASS = 16,	// + n: the output of the earlier pass n
};

#define D3D8ON12_TIMED_PASSES 16

// Average GPU milliseconds per frame since the last Get_Gpu_Timing.
struct D3D8ON12_GPU_TIMING
{
	BOOL enabled;
	UINT frames;							// frames measured
	float frame;							// the GPU's work for a whole frame
	float shadowMap;						// drawing the shadow casters into the shadow map
	float shadows;							// darkening the scene with the shadow map
	float postProcess;						// the post-processing chain
	UINT passCount;
	float passes[D3D8ON12_TIMED_PASSES];	// each pass of the chain
	float draws;							// draw calls per frame
	float pipelineChanges;					// of them, how many needed another pipeline state
	float tableChanges;						// texture and sampler tables set per frame
};

struct D3D8ON12_PP_PASS
{
	UINT shader;							// from Create_Shader
	UINT inputs[D3D8ON12_PP_INPUTS];		// D3D8ON12_PP_INPUT values for Input0..Input5 of the shader
	IDirect3DBaseTexture8* textures[D3D8ON12_PP_INPUTS];	// for inputs set to D3D8ON12_PP_INPUT_TEXTURE, such as a color lookup table
	UINT scale;								// the output is the screen size divided by this (1, 2, 4 or 8), or one pixel with
											// D3D8ON12_PP_SCALE_PIXEL (for averages); the last pass draws into the screen
	BOOL hdr;								// 16 bit float output instead of 8 bit
};

// A chain of full screen pixel shader passes over the 3D scene. The last pass draws into the back buffer,
// so what is drawn afterwards (the interface) is not processed.
//
// Shaders are HLSL pixel shaders (ps_5_0) with the entry point main. They include "PostProcess.hlsli",
// which the device provides: Input0..Input5, the samplers LinearClamp and PointClamp, the screen and
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

	// Measures the GPU time of frames and of their parts (timestamp queries), while enabled.
	virtual void STDMETHODCALLTYPE Set_Gpu_Timing(BOOL enable) = 0;
	// The averages since the last call, which starts the next average.
	virtual void STDMETHODCALLTYPE Get_Gpu_Timing(D3D8ON12_GPU_TIMING* timing) = 0;
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

// {3E7A9C15-D24B-4F68-8E09-B5C1A47D6E23}
DEFINE_GUID(IID_ID3D8On12Materials, 0x3e7a9c15, 0xd24b, 0x4f68, 0x8e, 0x09, 0xb5, 0xc1, 0xa4, 0x7d, 0x6e, 0x23);

// Pixel shaders that replace the fixed function look of draws, such as model meshes with normal maps.
//
// Shaders are HLSL pixel shaders (ps_5_0) with the entry point main. They include "Material.hlsli",
// which the device provides: the struct MaterialInput (the vertex shader's colors, two texture coordinate
// sets, view space position and normal), the fixed function constants (transforms, lights, material
// colors, fog, alpha test), the textures of stages 0..5 with their samplers (BaseTexture, DetailTexture,
// NormalMap, SpecularMap, EmissiveMap, ExtraMap), Time, and helpers for lighting, tangent frames, fog and
// the alpha test. Parameters are the variables of a cbuffer named Params (register b2).
//
// A material applies to the draws after Set_Material, except pretransformed and point sprite draws, draws
// with D3D8 shaders and the shadow map pass, which keep the fixed function pipeline.
struct ID3D8On12Materials : public IUnknown
{
	// Compiles a shader. Other #include files are read through include, which may be null. On failure the
	// compiler messages are returned in errors (when not null) and the result is a failure code.
	virtual HRESULT STDMETHODCALLTYPE Create_Shader(const char* source, UINT length, const char* name, ID3DInclude* include,
		UINT* shader, ID3DBlob** errors) = 0;
	virtual void STDMETHODCALLTYPE Release_Shader(UINT shader) = 0;

	// A material is a shader with its own parameter values; all start at 0.
	virtual HRESULT STDMETHODCALLTYPE Create_Material(UINT shader, UINT* material) = 0;
	virtual void STDMETHODCALLTYPE Release_Material(UINT material) = 0;
	// Sets a variable of the Params cbuffer of a material; E_INVALIDARG for an unknown name.
	virtual HRESULT STDMETHODCALLTYPE Set_Material_Parameter(UINT material, const char* name, const float* values, UINT count) = 0;

	// The material of the following draws; 0 for the fixed function pipeline. maps (4 entries, or null) are
	// the normal, specular, emissive and extra maps for stages 2..5, each null where there is none; they
	// are not referenced and must stay alive while the material is set.
	virtual void STDMETHODCALLTYPE Set_Material(UINT material, IDirect3DBaseTexture8* const* maps) = 0;
	// Copies what is drawn so far: the depth (always) and the scene colour (with color), which the material
	// draws after it read as SceneDepth (t6) and SceneColor (t7), until the frame is presented.
	virtual HRESULT STDMETHODCALLTYPE Capture_Scene(BOOL color) = 0;
};
