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

// ShadowMap.h
// The shadow map extension of the device (d3d8on12_ext.h).

#pragma once

#include "Private.h"
#include "d3d8on12_ext.h"

namespace D3D8On12
{

class Device;

class ShadowMapper : public ID3D8On12ShadowMap
{
public:
	explicit ShadowMapper(Device* device);
	~ShadowMapper();

	// Shares the reference count of the device.
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;

	HRESULT STDMETHODCALLTYPE Begin_Shadow_Map(UINT size, INT depthBias, float slopeBias) override;
	HRESULT STDMETHODCALLTYPE Begin_Shadow_Cascade(UINT cascade, UINT size, INT depthBias, float slopeBias) override;
	HRESULT STDMETHODCALLTYPE End_Shadow_Map(const D3DMATRIX* view, const D3DMATRIX* projection) override;
	HRESULT STDMETHODCALLTYPE Apply_Shadows(const D3D8ON12_SHADOW_PARAMS* params, const D3DMATRIX* view,
		const D3DMATRIX* projection) override;

	// While the casters are drawn: the device draws into the map only.
	bool Is_Drawing() const { return m_drawing; }
	UINT Size() const { return m_cascades[m_current].size; }
	INT Depth_Bias() const { return m_depthBias; }
	float Slope_Bias() const { return m_slopeBias; }
	D3D12_CPU_DESCRIPTOR_HANDLE Depth_View() const;

	static const UINT CASCADES = 2;

private:
	struct Cascade
	{
		GpuTexture map;			// R32_TYPELESS: drawn as D32_FLOAT, read as R32_FLOAT
		UINT dsv;				// view in the device's target DSV heap
		UINT size;
		bool ready;				// drawn since it was made (cascade 1: since the last cascade 0)
		D3DMATRIX view;			// the light camera of the map
		D3DMATRIX projection;
	};
	bool Ensure_Map(Cascade& cascade, UINT size);
	void Release_Map(Cascade& cascade);

	Device* m_device;
	Cascade m_cascades[CASCADES];
	UINT m_current;				// the cascade being drawn
	bool m_drawing;
	INT m_depthBias;
	float m_slopeBias;
};

} // namespace D3D8On12
