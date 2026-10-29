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

// Targets.cpp
// Render targets and depth buffers other than the back buffer and the automatic depth buffer: render
// target textures, render target and depth surfaces. Also creates the Direct3D 12 copies of textures
// and reads surfaces back into system memory.

#include "Device.h"
#include "Formats.h"
#include "Resources.h"

#include <string.h>

namespace D3D8On12
{

static const UINT TARGET_RTV_HEAP_SIZE = 1024;
static const UINT TARGET_DSV_HEAP_SIZE = 256;

HRESULT Device::Init_Target_Heaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	desc.NumDescriptors = TARGET_RTV_HEAP_SIZE;
	if (FAILED(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_targetRtvHeap))))
		return D3DERR_OUTOFVIDEOMEMORY;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	desc.NumDescriptors = TARGET_DSV_HEAP_SIZE;
	if (FAILED(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_targetDsvHeap))))
		return D3DERR_OUTOFVIDEOMEMORY;
	m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_nextTargetRtv = 1;	// 0 means no view
	m_nextTargetDsv = 1;
	return D3D_OK;
}

D3D12_CPU_DESCRIPTOR_HANDLE Device::Target_View_Handle(bool depth, UINT index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = depth ? m_targetDsvHeap->GetCPUDescriptorHandleForHeapStart() : m_targetRtvHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += (SIZE_T)index * (depth ? m_dsvDescriptorSize : m_rtvDescriptorSize);
	return handle;
}

// Views are copied into command lists when they are recorded, so they can be reused right away.
void Device::Free_Target_View(bool depth, UINT index)
{
	if (index == 0)
		return;
	(depth ? m_freeTargetDsvs : m_freeTargetRtvs).push_back(index);
}

void Device::Transition(GpuTexture& gpu, D3D12_RESOURCE_STATES state)
{
	if (gpu.resource == nullptr || gpu.state == state)
		return;
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = gpu.resource.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = gpu.state;
	barrier.Transition.StateAfter = state;
	m_commandList->ResourceBarrier(1, &barrier);
	gpu.state = state;
}

// Creates the Direct3D 12 resource of a texture or a standalone surface, with all levels and faces,
// and the shader resource view of textures that can be sampled.
bool Device::Create_Gpu_Resource(GpuTexture& gpu, D3DFORMAT format, UINT width, UINT height, UINT levels, UINT faces, DWORD usage, bool sampled)
{
	const FormatInfo* info = Get_Format_Info(format);
	if (info == nullptr)
		return false;

	DXGI_FORMAT dxgi = info->dxgi;
	UINT swizzle = info->swizzle;
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
	D3D12_CLEAR_VALUE clear = {};
	bool has_clear = false;

	if (usage & D3DUSAGE_RENDERTARGET)
	{
		flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		state = D3D12_RESOURCE_STATE_RENDER_TARGET;
		if (dxgi == DXGI_FORMAT_B8G8R8X8_UNORM)
		{
			// Render into the alpha channel too, but read it as opaque.
			dxgi = DXGI_FORMAT_B8G8R8A8_UNORM;
			swizzle = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, 2, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
		}
		// No clear value: the engine clears render targets to many colors, and a mismatch makes the
		// debug layer warn on every clear.
	}
	else if (usage & D3DUSAGE_DEPTHSTENCIL)
	{
		flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
		state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
		clear.Format = dxgi;
		clear.DepthStencil.Depth = 1.0f;
		has_clear = true;
		sampled = false;	// depth textures are not sampled by the engine
	}

	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	if (info->compressed)
	{
		// Block compressed resources must be a whole number of blocks.
		desc.Width = (desc.Width + 3) & ~3ull;
		desc.Height = (desc.Height + 3) & ~3u;
	}
	desc.DepthOrArraySize = (UINT16)faces;
	desc.MipLevels = (UINT16)levels;
	desc.Format = dxgi;
	desc.SampleDesc.Count = 1;
	desc.Flags = flags;

	// Plain textures are taken from the pool of released ones when one of the same kind is there.
	UINT64 pool_key = 0;
	if (sampled && flags == D3D12_RESOURCE_FLAG_NONE)
	{
		const UINT kind[] = { (UINT)dxgi, swizzle, (UINT)desc.Width, desc.Height, levels, faces };
		pool_key = Hash_Bytes(kind, sizeof(kind)) | 1;
		for (size_t i = m_texturePool.size(); i-- > 0;)
		{
			if (m_texturePool[i].key == pool_key)
			{
				gpu.resource = m_texturePool[i].resource;
				gpu.srv = m_texturePool[i].srv;
				gpu.state = m_texturePool[i].state;
				gpu.poolKey = pool_key;
				m_texturePool.erase(m_texturePool.begin() + i);
				return true;
			}
		}
	}

	if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, has_clear ? &clear : nullptr, IID_PPV_ARGS(&gpu.resource))))
	{
		Log("could not create a %ux%u texture of format %d", width, height, (int)format);
		return false;
	}
	gpu.state = state;
	gpu.poolKey = pool_key;

	if (sampled)
	{
		gpu.srv = Allocate_Srv();
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Format = dxgi;
		srv.Shader4ComponentMapping = swizzle;
		if (faces == 6)
		{
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srv.TextureCube.MipLevels = levels;
		}
		else
		{
			srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srv.Texture2D.MipLevels = levels;
		}
		m_device->CreateShaderResourceView(gpu.resource.Get(), &srv, Srv_Cpu_Handle(gpu.srv));
	}
	return true;
}

// Creates the resource a render target or depth surface draws into, and its view.
bool Device::Ensure_Target(Surface* surface)
{
	GpuTexture* gpu = surface->Gpu_Target();
	if (gpu->resource == nullptr)
	{
		const D3DSURFACE_DESC& desc = surface->Desc();
		IDirect3DBaseTexture8* texture = nullptr;
		if (surface->Has_Container() && SUCCEEDED(surface->GetContainer(IID_IDirect3DBaseTexture8, (void**)&texture)))
		{
			// A level of a render target texture: create the whole texture.
			D3DSURFACE_DESC top;
			UINT faces = 1;
			if (texture->GetType() == D3DRTYPE_CUBETEXTURE)
			{
				static_cast<IDirect3DCubeTexture8*>(texture)->GetLevelDesc(0, &top);
				faces = 6;
			}
			else
			{
				static_cast<IDirect3DTexture8*>(texture)->GetLevelDesc(0, &top);
			}
			const UINT levels = texture->GetLevelCount();
			texture->Release();
			if (!Create_Gpu_Resource(*gpu, top.Format, top.Width, top.Height, levels, faces, top.Usage, true))
				return false;
		}
		else if (!Create_Gpu_Resource(*gpu, desc.Format, desc.Width, desc.Height, 1, 1, desc.Usage, false))
		{
			return false;
		}
	}

	if (surface->Target_View() == 0)
	{
		const bool depth = surface->Get_Kind() == Surface::KIND_DEPTH_STENCIL;
		std::vector<UINT>& free_list = depth ? m_freeTargetDsvs : m_freeTargetRtvs;
		UINT& next = depth ? m_nextTargetDsv : m_nextTargetRtv;
		UINT index = 0;
		if (!free_list.empty())
		{
			index = free_list.back();
			free_list.pop_back();
		}
		else if (next < (depth ? TARGET_DSV_HEAP_SIZE : TARGET_RTV_HEAP_SIZE))
		{
			index = next++;
		}
		if (index == 0)
		{
			D3D8ON12_UNSUPPORTED("too many render targets");
			return false;
		}

		const D3D12_RESOURCE_DESC resource_desc = gpu->resource->GetDesc();
		const UINT levels = resource_desc.MipLevels;
		const UINT level = surface->Subresource() % levels;
		const UINT face = surface->Subresource() / levels;
		if (depth)
		{
			D3D12_DEPTH_STENCIL_VIEW_DESC view = {};
			view.Format = resource_desc.Format;
			view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			view.Texture2D.MipSlice = level;
			m_device->CreateDepthStencilView(gpu->resource.Get(), &view, Target_View_Handle(true, index));
		}
		else
		{
			D3D12_RENDER_TARGET_VIEW_DESC view = {};
			view.Format = resource_desc.Format;
			if (resource_desc.DepthOrArraySize > 1)
			{
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
				view.Texture2DArray.MipSlice = level;
				view.Texture2DArray.FirstArraySlice = face;
				view.Texture2DArray.ArraySize = 1;
			}
			else
			{
				view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
				view.Texture2D.MipSlice = level;
			}
			m_device->CreateRenderTargetView(gpu->resource.Get(), &view, Target_View_Handle(false, index));
		}
		surface->Set_Target_View(index);
	}
	return true;
}

// Makes the current render target and depth buffer ready to draw into. A depth buffer of another
// size than the render target can not be used with it in Direct3D 12, so it is left out.
bool Device::Prepare_Targets(TargetState& targets)
{
	UINT width = 0;
	UINT height = 0;
	if (m_renderTarget == m_backBufferSurface)
	{
		targets.rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		targets.rtv.ptr += (m_multisampleTarget != nullptr ? FRAME_COUNT : m_backBufferIndex) * m_rtvDescriptorSize;
		targets.rtvFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		targets.sampleCount = m_sampleCount;
	}
	else
	{
		if (!Ensure_Target(m_renderTarget))
			return false;
		GpuTexture* gpu = m_renderTarget->Gpu_Target();
		Transition(*gpu, D3D12_RESOURCE_STATE_RENDER_TARGET);
		targets.rtv = Target_View_Handle(false, m_renderTarget->Target_View());
		targets.rtvFormat = gpu->resource->GetDesc().Format;
		targets.sampleCount = 1;
	}
	width = m_renderTarget->Desc().Width;
	height = m_renderTarget->Desc().Height;

	targets.dsvFormat = DXGI_FORMAT_UNKNOWN;
	if (m_depthStencil != nullptr && m_depthStencil->Desc().Width == width && m_depthStencil->Desc().Height == height)
	{
		// Only the automatic depth buffer is multisampled, like the back buffer.
		const bool auto_depth = m_depthStencil == m_autoDepthSurface && m_depthBuffer != nullptr;
		const UINT depth_samples = auto_depth ? m_depthBuffer->GetDesc().SampleDesc.Count : 1;
		if (depth_samples != targets.sampleCount)
		{
			D3D8ON12_UNSUPPORTED("a depth buffer with another sample count than the render target");
		}
		else if (auto_depth)
		{
			targets.dsv = m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
			targets.dsvFormat = m_depthBuffer->GetDesc().Format;
		}
		else if (m_depthStencil->Get_Kind() == Surface::KIND_DEPTH_STENCIL && Ensure_Target(m_depthStencil))
		{
			GpuTexture* gpu = m_depthStencil->Gpu_Target();
			Transition(*gpu, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			targets.dsv = Target_View_Handle(true, m_depthStencil->Target_View());
			targets.dsvFormat = gpu->resource->GetDesc().Format;
		}
	}
	else if (m_depthStencil != nullptr)
	{
		D3D8ON12_UNSUPPORTED("a depth buffer of another size than the render target");
	}
	return true;
}

// Copies a surface the GPU draws into to a surface in system memory of the same size and format.
HRESULT Device::Read_Surface(Surface* source, Surface* dest)
{
	if (source->Get_Kind() == Surface::KIND_BACK_BUFFER)
		return Read_Back_Buffer(dest);

	const D3DSURFACE_DESC& desc = source->Desc();
	if (dest->Desc().Width != desc.Width || dest->Desc().Height != desc.Height || dest->Desc().Format != desc.Format)
		return D3DERR_INVALIDCALL;
	if (source->Get_Kind() != Surface::KIND_RENDER_TARGET || !Ensure_Target(source))
		return D3DERR_INVALIDCALL;

	Begin_Frame();
	GpuTexture* gpu = source->Gpu_Target();
	const D3D12_RESOURCE_DESC resource_desc = gpu->resource->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
	UINT64 total_size = 0;
	m_device->GetCopyableFootprints(&resource_desc, source->Subresource(), 1, 0, &footprint, nullptr, nullptr, &total_size);
	if (!Ensure_Readback_Buffer(total_size))
		return D3DERR_OUTOFVIDEOMEMORY;

	const D3D12_RESOURCE_STATES state = gpu->state;
	Transition(*gpu, D3D12_RESOURCE_STATE_COPY_SOURCE);
	D3D12_TEXTURE_COPY_LOCATION from = {};
	from.pResource = gpu->resource.Get();
	from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	from.SubresourceIndex = source->Subresource();
	D3D12_TEXTURE_COPY_LOCATION to = {};
	to.pResource = m_readbackBuffer.Get();
	to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	to.PlacedFootprint = footprint;
	m_commandList->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
	Transition(*gpu, state);
	Flush_Commands();

	void* mapped = nullptr;
	const D3D12_RANGE range = { 0, (SIZE_T)total_size };
	if (FAILED(m_readbackBuffer->Map(0, &range, &mapped)))
		return D3DERR_INVALIDCALL;
	const UINT row_bytes = min(dest->Pitch(), (UINT)footprint.Footprint.RowPitch);
	for (UINT y = 0; y < desc.Height; ++y)
		memcpy(dest->Data() + y * dest->Pitch(), (unsigned char*)mapped + footprint.Offset + y * footprint.Footprint.RowPitch, row_bytes);
	const D3D12_RANGE written = { 0, 0 };
	m_readbackBuffer->Unmap(0, &written);
	dest->Set_Dirty(true);
	return D3D_OK;
}

bool Device::Ensure_Readback_Buffer(UINT64 size)
{
	if (m_readbackBuffer != nullptr && m_readbackBuffer->GetDesc().Width >= size)
		return true;
	m_readbackBuffer.Reset();
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	return SUCCEEDED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readbackBuffer)));
}

} // namespace D3D8On12
