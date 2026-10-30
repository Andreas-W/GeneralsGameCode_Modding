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

// ShadowMap.cpp
// A depth map of the shadow casters seen from the light. The device draws into it between
// Begin_Shadow_Map and End_Shadow_Map (Prepare_Targets, Draw); the post-processor applies it.

#include "ShadowMap.h"
#include "Device.h"
#include "PostProcess.h"

namespace D3D8On12
{

ShadowMapper::ShadowMapper(Device* device)
	: m_device(device),
		m_current(0),
		m_drawing(false),
		m_depthBias(0),
		m_slopeBias(0.0f)
{
	for (Cascade& cascade : m_cascades)
	{
		cascade.dsv = 0;
		cascade.size = 0;
		cascade.ready = false;
		memset(&cascade.view, 0, sizeof(cascade.view));
		memset(&cascade.projection, 0, sizeof(cascade.projection));
	}
}

// The device is idle when it deletes the shadow mapper, so the resources can go right away.
ShadowMapper::~ShadowMapper()
{
	for (Cascade& cascade : m_cascades)
		Release_Map(cascade);
}

void ShadowMapper::Release_Map(Cascade& cascade)
{
	if (cascade.map.resource != nullptr || cascade.map.srv != 0)
		m_device->Release_Gpu_Texture(cascade.map);
	m_device->Free_Target_View(true, cascade.dsv);
	cascade.dsv = 0;
	cascade.size = 0;
	cascade.ready = false;
}

HRESULT STDMETHODCALLTYPE ShadowMapper::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_ID3D8On12ShadowMap)
	{
		AddRef();
		*object = static_cast<ID3D8On12ShadowMap*>(this);
		return S_OK;
	}
	return m_device->QueryInterface(riid, object);
}

ULONG STDMETHODCALLTYPE ShadowMapper::AddRef()
{
	return m_device->AddRef();
}

ULONG STDMETHODCALLTYPE ShadowMapper::Release()
{
	return m_device->Release();
}

D3D12_CPU_DESCRIPTOR_HANDLE ShadowMapper::Depth_View() const
{
	return m_device->Target_View_Handle(true, m_cascades[m_current].dsv);
}

bool ShadowMapper::Ensure_Map(Cascade& cascade, UINT size)
{
	if (cascade.map.resource != nullptr && cascade.size == size)
		return true;
	Release_Map(cascade);

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = size;
	desc.Height = size;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R32_TYPELESS;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = DXGI_FORMAT_D32_FLOAT;
	clear.DepthStencil.Depth = 1.0f;
	if (FAILED(m_device->m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&clear, IID_PPV_ARGS(&cascade.map.resource))))
	{
		Log("could not create a %ux%u shadow map", size, size);
		return false;
	}
	cascade.map.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	cascade.dsv = m_device->Allocate_Target_View(true);
	if (cascade.dsv == 0)
	{
		m_device->Release_Gpu_Texture(cascade.map);
		return false;
	}
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	m_device->m_device->CreateDepthStencilView(cascade.map.resource.Get(), &dsv, m_device->Target_View_Handle(true, cascade.dsv));

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	cascade.map.srv = m_device->Allocate_Srv();
	m_device->m_device->CreateShaderResourceView(cascade.map.resource.Get(), &srv, m_device->Srv_Cpu_Handle(cascade.map.srv));
	cascade.size = size;
	Log("shadow map %ux%u", size, size);
	return true;
}

HRESULT STDMETHODCALLTYPE ShadowMapper::Begin_Shadow_Map(UINT size, INT depthBias, float slopeBias)
{
	return Begin_Shadow_Cascade(0, size, depthBias, slopeBias);
}

HRESULT STDMETHODCALLTYPE ShadowMapper::Begin_Shadow_Cascade(UINT index, UINT size, INT depthBias, float slopeBias)
{
	if (m_drawing || index >= CASCADES || size < 16 || size > 8192)
		return D3DERR_INVALIDCALL;
	m_device->Begin_Frame();
	Cascade& cascade = m_cascades[index];
	if (!Ensure_Map(cascade, size))
		return E_FAIL;
	if (index == 0)
		for (UINT other = 1; other < CASCADES; ++other)
			m_cascades[other].ready = false;
	m_current = index;
	m_device->Transition(cascade.map, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_depthBias = depthBias;
	m_slopeBias = slopeBias;
	m_drawing = true;
	m_device->Mark_Time(Device::TIMING_SHADOW_MAP_BEGIN);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE ShadowMapper::End_Shadow_Map(const D3DMATRIX* view, const D3DMATRIX* projection)
{
	if (!m_drawing || view == nullptr || projection == nullptr)
		return D3DERR_INVALIDCALL;
	m_drawing = false;
	m_device->Mark_Time(Device::TIMING_SHADOW_MAP_END);
	Cascade& cascade = m_cascades[m_current];
	m_device->Transition(cascade.map, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cascade.view = *view;
	cascade.projection = *projection;
	cascade.ready = true;
	m_current = 0;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE ShadowMapper::Apply_Shadows(const D3D8ON12_SHADOW_PARAMS* params, const D3DMATRIX* view,
	const D3DMATRIX* projection)
{
	if (params == nullptr || view == nullptr || projection == nullptr || m_drawing)
		return D3DERR_INVALIDCALL;
	const Cascade& main = m_cascades[0];
	if (!main.ready)
		return D3D_OK;
	const Cascade& detail = m_cascades[1];
	PostProcessor::ShadowCascade cascades[CASCADES] =
	{
		{ main.map.srv, main.size, main.view, main.projection },
		{ detail.ready ? detail.map.srv : 0, detail.size, detail.view, detail.projection },
	};
	return m_device->Get_Post_Processor()->Draw_Shadows(cascades, *params, *view, *projection);
}

} // namespace D3D8On12
