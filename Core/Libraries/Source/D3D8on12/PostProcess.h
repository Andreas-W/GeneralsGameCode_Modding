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

// PostProcess.h
// The post-processing extension of the device (d3d8on12_ext.h).

#pragma once

#include "Private.h"
#include "ShaderCompiler.h"
#include "d3d8on12_ext.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace D3D8On12
{

class Device;
class Surface;

class PostProcessor : public ID3D8On12PostProcess
{
public:
	explicit PostProcessor(Device* device);
	~PostProcessor();

	// Shares the reference count of the device.
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;

	HRESULT STDMETHODCALLTYPE Create_Shader(const char* source, UINT length, const char* name, ID3DInclude* include,
		UINT* shader, ID3DBlob** errors) override;
	void STDMETHODCALLTYPE Release_Shader(UINT shader) override;
	HRESULT STDMETHODCALLTYPE Set_Chain(const D3D8ON12_PP_PASS* passes, UINT count) override;
	HRESULT STDMETHODCALLTYPE Set_Parameter(UINT pass, const char* name, const float* values, UINT count) override;
	void STDMETHODCALLTYPE Set_Camera(const D3DMATRIX* view, const D3DMATRIX* projection) override;
	HRESULT STDMETHODCALLTYPE Run(float time) override;
	void STDMETHODCALLTYPE Set_HDR_Scene(BOOL enable) override;
	void STDMETHODCALLTYPE Set_Gpu_Timing(BOOL enable) override;
	void STDMETHODCALLTYPE Get_Gpu_Timing(D3D8ON12_GPU_TIMING* timing) override;

	// Releases the textures of the chain; called when the engine releases the device.
	void Clear_Chain();
	void Present_Scene();
	// Copies a rectangle of what is drawn into the back buffer so far into a render target surface of
	// the same format, at point (CopyRects; smudges read the scene this way).
	HRESULT Copy_Back_Buffer(Surface* dest, const RECT& rect, const POINT& point);
	// Darkens the back buffer by a shadow map (ShadowMap.cpp); light_view and light_projection are the
	// camera of the map, view and projection the scene's.
	HRESULT Draw_Shadows(UINT map_srv, UINT map_size, const D3DMATRIX& light_view, const D3DMATRIX& light_projection,
		const D3D8ON12_SHADOW_PARAMS& params, const D3DMATRIX& view, const D3DMATRIX& projection);

private:
	typedef ShaderVariable Variable;

	struct Shader
	{
		bool used;
		ComPtr<ID3DBlob> code;
		UINT paramsSize;	// size of the Params cbuffer, 0 without
		std::unordered_map<std::string, Variable> variables;
		std::unordered_map<UINT64, ComPtr<ID3D12PipelineState>> pipelines;	// by target format and sample count
	};

	struct Target
	{
		GpuTexture gpu;
		UINT rtv;	// view in the device's target RTV heap
		UINT width;
		UINT height;
		DXGI_FORMAT format;
	};

	struct Pass
	{
		D3D8ON12_PP_PASS desc;
		std::vector<unsigned char> params;	// contents of the Params cbuffer
		Target output;						// unused for the last pass, which draws into the back buffer
		Target previous;					// the output of the frame before, for passes with a Previous input
		bool usesPrevious;
		bool previousValid;					// previous holds a frame's output
	};

	ID3D12PipelineState* Get_Pipeline(Shader& shader, DXGI_FORMAT format, UINT samples, bool multiply = false);
	bool Ensure_Target(Target& target, UINT width, UINT height, DXGI_FORMAT format);
	void Release_Target(Target& target);
	bool Bind_Root();
	bool Copy_Scene(UINT width, UINT height);
	bool Copy_Depth(UINT width, UINT height);

	Device* m_device;
	std::vector<Shader> m_shaders;	// handle = index + 1
	std::vector<Pass> m_passes;
	ComPtr<ID3DBlob> m_vertexShader;
	Target m_scene;
	Target m_depth;				// R32_FLOAT copy of the depth buffer, made when a pass reads the depth
	UINT m_depthShaders[2];		// for a depth buffer without and with multisampling
	UINT m_presentShaders[2];	// for an HDR scene target without and with multisampling
	UINT m_copyShader;			// Copy_Back_Buffer
	float m_lastRunTime;		// time given to the last Run, -1 before the first
	UINT m_shadowShader;
	UINT m_shadowSampler;		// comparison sampler for the shadow map
	D3DMATRIX m_view;
	D3DMATRIX m_projection;
	UINT m_linearSampler;
	UINT m_pointSampler;
};

} // namespace D3D8On12
