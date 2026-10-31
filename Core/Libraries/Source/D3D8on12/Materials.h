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

// Materials.h
// The material extension of the device (d3d8on12_ext.h): pixel shaders that replace the fixed function
// ones of the draws they are set for. Draw_Geometry asks Current() which one applies.

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

class MaterialSystem : public ID3D8On12Materials
{
public:
	explicit MaterialSystem(Device* device);

	// Shares the reference count of the device.
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;

	HRESULT STDMETHODCALLTYPE Create_Shader(const char* source, UINT length, const char* name, ID3DInclude* include,
		UINT* shader, ID3DBlob** errors) override;
	void STDMETHODCALLTYPE Release_Shader(UINT shader) override;
	HRESULT STDMETHODCALLTYPE Create_Material(UINT shader, UINT* material) override;
	void STDMETHODCALLTYPE Release_Material(UINT material) override;
	HRESULT STDMETHODCALLTYPE Set_Material_Parameter(UINT material, const char* name, const float* values, UINT count) override;
	void STDMETHODCALLTYPE Set_Material(UINT material, IDirect3DBaseTexture8* const* maps) override;
	HRESULT STDMETHODCALLTYPE Capture_Scene(BOOL color) override;

	// What the current material draws with; false for the fixed function pipeline.
	struct Binding
	{
		ID3DBlob* code;
		UINT64 hash;							// of the code, as the pixel shader hash of pipelines
		const std::vector<unsigned char>* params;	// contents of the Params cbuffer, empty without one
		IDirect3DBaseTexture8* const* maps;		// textures of stages 2..5, entries may be null
	};
	bool Current(Binding& binding) const;

	// Seconds since the device was created, for animated materials.
	float Time() const;

	// The copies of Capture_Scene for t6 and t7, 0 without; samplers for them.
	UINT Scene_Depth() const { return m_sceneDepth; }
	UINT Scene_Color() const { return m_sceneColor; }
	UINT Scene_Sampler(UINT stage);
	// Forgets the copies; when the frame is presented and when the device is reset.
	void Release_Scene() { m_sceneDepth = m_sceneColor = 0; }

private:
	struct Shader
	{
		bool used;
		ComPtr<ID3DBlob> code;
		UINT64 hash;
		UINT paramsSize;
		std::unordered_map<std::string, ShaderVariable> variables;
	};

	struct Material
	{
		bool used;
		UINT shader;
		std::vector<unsigned char> params;
	};

	Device* m_device;
	std::vector<Shader> m_shaders;		// handle = index + 1
	std::vector<Material> m_materials;	// handle = index + 1
	UINT m_current;
	IDirect3DBaseTexture8* m_maps[4];
	UINT m_sceneDepth;
	UINT m_sceneColor;
	UINT m_pointSampler;
	UINT m_linearSampler;
	LARGE_INTEGER m_startTime;
	LARGE_INTEGER m_frequency;
};

} // namespace D3D8On12
