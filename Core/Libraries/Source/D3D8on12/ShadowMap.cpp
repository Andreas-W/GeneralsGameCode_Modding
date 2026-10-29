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
		m_dsv(0),
		m_size(0),
		m_drawing(false),
		m_ready(false),
		m_depthBias(0),
		m_slopeBias(0.0f)
{
	memset(&m_view, 0, sizeof(m_view));
	memset(&m_projection, 0, sizeof(m_projection));
}

// The device is idle when it deletes the shadow mapper, so the resources can go right away.
ShadowMapper::~ShadowMapper()
{
	if (m_map.resource != nullptr || m_map.srv != 0)
		m_device->Release_Gpu_Texture(m_map);
	m_device->Free_Target_View(true, m_dsv);
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
	return m_device->Target_View_Handle(true, m_dsv);
}

bool ShadowMapper::Ensure_Map(UINT size)
{
	if (m_map.resource != nullptr && m_size == size)
		return true;
	if (m_map.resource != nullptr || m_map.srv != 0)
		m_device->Release_Gpu_Texture(m_map);
	m_device->Free_Target_View(true, m_dsv);
	m_dsv = 0;
	m_size = 0;
	m_ready = false;

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
		&clear, IID_PPV_ARGS(&m_map.resource))))
	{
		Log("could not create a %ux%u shadow map", size, size);
		return false;
	}
	m_map.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;

	m_dsv = m_device->Allocate_Target_View(true);
	if (m_dsv == 0)
	{
		m_device->Release_Gpu_Texture(m_map);
		return false;
	}
	D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
	dsv.Format = DXGI_FORMAT_D32_FLOAT;
	dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	m_device->m_device->CreateDepthStencilView(m_map.resource.Get(), &dsv, Depth_View());

	D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
	srv.Format = DXGI_FORMAT_R32_FLOAT;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srv.Texture2D.MipLevels = 1;
	m_map.srv = m_device->Allocate_Srv();
	m_device->m_device->CreateShaderResourceView(m_map.resource.Get(), &srv, m_device->Srv_Cpu_Handle(m_map.srv));
	m_size = size;
	Log("shadow map %ux%u", size, size);
	return true;
}

HRESULT STDMETHODCALLTYPE ShadowMapper::Begin_Shadow_Map(UINT size, INT depthBias, float slopeBias)
{
	if (m_drawing || size < 16 || size > 8192)
		return D3DERR_INVALIDCALL;
	m_device->Begin_Frame();
	if (!Ensure_Map(size))
		return E_FAIL;
	m_device->Transition(m_map, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	m_depthBias = depthBias;
	m_slopeBias = slopeBias;
	m_drawing = true;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE ShadowMapper::End_Shadow_Map(const D3DMATRIX* view, const D3DMATRIX* projection)
{
	if (!m_drawing || view == nullptr || projection == nullptr)
		return D3DERR_INVALIDCALL;
	m_drawing = false;
	m_device->Transition(m_map, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	m_view = *view;
	m_projection = *projection;
	m_ready = true;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE ShadowMapper::Apply_Shadows(const D3D8ON12_SHADOW_PARAMS* params, const D3DMATRIX* view,
	const D3DMATRIX* projection)
{
	if (params == nullptr || view == nullptr || projection == nullptr || m_drawing)
		return D3DERR_INVALIDCALL;
	if (!m_ready)
		return D3D_OK;
	return m_device->Get_Post_Processor()->Draw_Shadows(m_map.srv, m_size, m_view, m_projection, *params, *view, *projection);
}

} // namespace D3D8On12
