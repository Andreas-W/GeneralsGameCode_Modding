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

// Gpu.cpp
// Direct3D 12 objects shared by all draws: the root signature, the shader visible descriptor heaps,
// the per-frame upload memory, and the GPU copies of textures and their samplers.

#include "Device.h"
#include "Formats.h"
#include "Resources.h"

#include <string.h>

namespace D3D8On12
{

static const UINT SRV_HEAP_SIZE = 65536;
static const UINT SAMPLER_HEAP_SIZE = 2048;
static const UINT64 UPLOAD_BUFFER_SIZE = 64 * 1024 * 1024;

// Descriptors 0 and 1 of the shader visible heap read nothing: they stand in for stages without a
// 2D texture and without a cube texture.
static const UINT NULL_SRV_2D = 0;
static const UINT NULL_SRV_CUBE = 1;

// Root signature layout.
static const UINT ROOT_VERTEX_CONSTANTS = 0;
static const UINT ROOT_PIXEL_CONSTANTS = 1;
static const UINT ROOT_TEXTURES = 2;							// one table per stage
static const UINT ROOT_SAMPLERS = ROOT_TEXTURES + FF_STAGES;	// one table per stage
static const UINT ROOT_MATERIAL_CONSTANTS = ROOT_SAMPLERS + FF_STAGES;	// b2, the Params of material shaders

static ComPtr<ID3D12Resource> Create_Buffer(ID3D12Device* device, D3D12_HEAP_TYPE heap_type, UINT64 size, D3D12_RESOURCE_STATES state)
{
	D3D12_HEAP_PROPERTIES heap = {};
	heap.Type = heap_type;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ComPtr<ID3D12Resource> buffer;
	if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&buffer))))
		return nullptr;
	return buffer;
}

HRESULT Device::Init_Gpu_Objects()
{
	// Root signature: vertex and pixel constants, then a texture table and a sampler table per stage,
	// then the material parameters.
	D3D12_DESCRIPTOR_RANGE ranges[2 * FF_STAGES] = {};
	D3D12_ROOT_PARAMETER params[ROOT_MATERIAL_CONSTANTS + 1] = {};

	params[ROOT_VERTEX_CONSTANTS].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[ROOT_VERTEX_CONSTANTS].Descriptor.ShaderRegister = 0;
	params[ROOT_VERTEX_CONSTANTS].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;	// also read by the point sprite geometry shader
	params[ROOT_PIXEL_CONSTANTS].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[ROOT_PIXEL_CONSTANTS].Descriptor.ShaderRegister = 1;
	params[ROOT_PIXEL_CONSTANTS].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	for (UINT stage = 0; stage < FF_STAGES; ++stage)
	{
		D3D12_DESCRIPTOR_RANGE& texture_range = ranges[stage];
		texture_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		texture_range.NumDescriptors = 1;
		texture_range.BaseShaderRegister = stage;
		D3D12_ROOT_PARAMETER& texture_param = params[ROOT_TEXTURES + stage];
		texture_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		texture_param.DescriptorTable.NumDescriptorRanges = 1;
		texture_param.DescriptorTable.pDescriptorRanges = &texture_range;
		texture_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_DESCRIPTOR_RANGE& sampler_range = ranges[FF_STAGES + stage];
		sampler_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		sampler_range.NumDescriptors = 1;
		sampler_range.BaseShaderRegister = stage;
		D3D12_ROOT_PARAMETER& sampler_param = params[ROOT_SAMPLERS + stage];
		sampler_param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		sampler_param.DescriptorTable.NumDescriptorRanges = 1;
		sampler_param.DescriptorTable.pDescriptorRanges = &sampler_range;
		sampler_param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

	params[ROOT_MATERIAL_CONSTANTS].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[ROOT_MATERIAL_CONSTANTS].Descriptor.ShaderRegister = 2;
	params[ROOT_MATERIAL_CONSTANTS].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC root_desc = {};
	root_desc.NumParameters = ROOT_MATERIAL_CONSTANTS + 1;
	root_desc.pParameters = params;
	root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> errors;
	if (FAILED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors)))
	{
		Log("root signature: %s", errors != nullptr ? (const char*)errors->GetBufferPointer() : "failed");
		return D3DERR_NOTAVAILABLE;
	}
	if (FAILED(m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
		return D3DERR_NOTAVAILABLE;

	D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
	heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heap_desc.NumDescriptors = SRV_HEAP_SIZE;
	heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (FAILED(m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_srvHeap))))
		return D3DERR_OUTOFVIDEOMEMORY;
	m_srvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_srvGpuStart = m_srvHeap->GetGPUDescriptorHandleForHeapStart();

	heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
	heap_desc.NumDescriptors = SAMPLER_HEAP_SIZE;
	if (FAILED(m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_samplerHeap))))
		return D3DERR_OUTOFVIDEOMEMORY;
	m_samplerDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
	m_samplerGpuStart = m_samplerHeap->GetGPUDescriptorHandleForHeapStart();

	D3D12_SHADER_RESOURCE_VIEW_DESC null_desc = {};
	null_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	null_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	null_desc.Texture2D.MipLevels = 1;
	m_device->CreateShaderResourceView(nullptr, &null_desc, Srv_Cpu_Handle(NULL_SRV_2D));
	null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	null_desc.TextureCube.MipLevels = 1;
	m_device->CreateShaderResourceView(nullptr, &null_desc, Srv_Cpu_Handle(NULL_SRV_CUBE));
	m_nextSrv = 2;

	// Sampler 0 is for stages without a texture.
	D3D12_SAMPLER_DESC default_sampler = {};
	default_sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
	default_sampler.AddressU = default_sampler.AddressV = default_sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	default_sampler.MaxAnisotropy = 1;
	default_sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	default_sampler.MaxLOD = D3D12_FLOAT32_MAX;
	m_device->CreateSampler(&default_sampler, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());

	for (UINT i = 0; i < FRAME_COUNT; ++i)
	{
		m_uploadBuffers[i] = Create_Buffer(m_device.Get(), D3D12_HEAP_TYPE_UPLOAD, UPLOAD_BUFFER_SIZE, D3D12_RESOURCE_STATE_GENERIC_READ);
		if (m_uploadBuffers[i] == nullptr)
			return D3DERR_OUTOFVIDEOMEMORY;
		const D3D12_RANGE no_read = { 0, 0 };
		m_uploadBuffers[i]->Map(0, &no_read, (void**)&m_uploadCpu[i]);
	}
	m_uploadOffset = 0;
	return D3D_OK;
}

D3D12_CPU_DESCRIPTOR_HANDLE Device::Srv_Cpu_Handle(UINT index) const
{
	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += (SIZE_T)index * m_srvDescriptorSize;
	return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE Device::Srv_Gpu_Handle(UINT index) const
{
	D3D12_GPU_DESCRIPTOR_HANDLE handle = m_srvGpuStart;
	handle.ptr += (UINT64)index * m_srvDescriptorSize;
	return handle;
}

UINT Device::Allocate_Srv()
{
	if (!m_freeSrvs.empty())
	{
		const UINT index = m_freeSrvs.back();
		m_freeSrvs.pop_back();
		return index;
	}
	if (m_nextSrv >= SRV_HEAP_SIZE)
	{
		D3D8ON12_UNSUPPORTED("more than 65536 textures");
		return 0;
	}
	return m_nextSrv++;
}

// Upload memory of the frame being recorded. Allocations that do not fit get a buffer of their own,
// which is released with the frame.
Device::UploadAllocation Device::Allocate_Upload(UINT64 size, UINT64 alignment)
{
	UploadAllocation allocation = {};
	const UINT64 offset = (m_uploadOffset + alignment - 1) & ~(alignment - 1);
	if (offset + size <= UPLOAD_BUFFER_SIZE)
	{
		ID3D12Resource* buffer = m_uploadBuffers[m_backBufferIndex].Get();
		allocation.cpu = m_uploadCpu[m_backBufferIndex] + offset;
		allocation.gpu = buffer->GetGPUVirtualAddress() + offset;
		allocation.resource = buffer;
		allocation.offset = offset;
		m_uploadOffset = offset + size;
		return allocation;
	}

	ComPtr<ID3D12Resource> buffer = Create_Buffer(m_device.Get(), D3D12_HEAP_TYPE_UPLOAD, size, D3D12_RESOURCE_STATE_GENERIC_READ);
	if (buffer == nullptr)
		return allocation;
	const D3D12_RANGE no_read = { 0, 0 };
	buffer->Map(0, &no_read, (void**)&allocation.cpu);
	allocation.gpu = buffer->GetGPUVirtualAddress();
	allocation.resource = buffer.Get();
	allocation.offset = 0;
	Garbage garbage = { m_nextFenceValue, buffer, 0 };
	m_garbage.push_back(garbage);
	return allocation;
}

// Work recorded so far is signaled with m_nextFenceValue at the latest, so that is when the texture
// is no longer in use.
void Device::Release_Gpu_Texture(GpuTexture& gpu)
{
	if (gpu.resource != nullptr || gpu.srv != 0)
	{
		Garbage garbage = { m_nextFenceValue, gpu.resource, gpu.srv, gpu.poolKey, gpu.state };
		m_garbage.push_back(garbage);
	}
	gpu.resource.Reset();
	gpu.srv = 0;
	gpu.poolKey = 0;
}

void Device::Release_Gpu_Resource(ComPtr<ID3D12Resource>& resource)
{
	if (resource != nullptr)
	{
		Garbage garbage = { m_nextFenceValue, resource, 0 };
		m_garbage.push_back(garbage);
	}
	resource.Reset();
}

// Creates the Direct3D 12 copy of a buffer on first use and uploads the range changed since the last
// upload. Returns its GPU address.
D3D12_GPU_VIRTUAL_ADDRESS Device::Prepare_Buffer(GpuBuffer& gpu, const unsigned char* data, UINT size, D3D12_RESOURCE_STATES read_state)
{
	if (gpu.resource == nullptr)
	{
		gpu.resource = Create_Buffer(m_device.Get(), D3D12_HEAP_TYPE_DEFAULT, size, D3D12_RESOURCE_STATE_COPY_DEST);
		if (gpu.resource == nullptr)
			return 0;
		gpu.state = D3D12_RESOURCE_STATE_COPY_DEST;
		gpu.dirtyBegin = 0;
		gpu.dirtyEnd = size;
	}

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = gpu.resource.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	if (gpu.dirtyBegin < gpu.dirtyEnd)
	{
		if (gpu.state != D3D12_RESOURCE_STATE_COPY_DEST)
		{
			barrier.Transition.StateBefore = gpu.state;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			m_commandList->ResourceBarrier(1, &barrier);
			gpu.state = D3D12_RESOURCE_STATE_COPY_DEST;
		}

		const UINT bytes = gpu.dirtyEnd - gpu.dirtyBegin;
		UploadAllocation upload = Allocate_Upload(bytes, 16);
		if (upload.cpu == nullptr)
			return 0;
		memcpy(upload.cpu, data + gpu.dirtyBegin, bytes);
		m_commandList->CopyBufferRegion(gpu.resource.Get(), gpu.dirtyBegin, upload.resource, upload.offset, bytes);
		gpu.dirtyBegin = gpu.dirtyEnd = 0;
	}

	if (gpu.state != read_state)
	{
		barrier.Transition.StateBefore = gpu.state;
		barrier.Transition.StateAfter = read_state;
		m_commandList->ResourceBarrier(1, &barrier);
		gpu.state = read_state;
	}
	return gpu.resource->GetGPUVirtualAddress();
}

void Device::Release_Finished_Resources()
{
	if (m_fence == nullptr)
		return;
	const UINT64 completed = m_fence->GetCompletedValue();
	size_t kept = 0;
	for (size_t i = 0; i < m_garbage.size(); ++i)
	{
		if (m_garbage[i].fence <= completed)
		{
			if (m_garbage[i].poolKey != 0 && m_garbage[i].resource != nullptr)
			{
				PooledTexture pooled = { m_garbage[i].poolKey, m_garbage[i].fence, m_garbage[i].resource, m_garbage[i].srv, m_garbage[i].state };
				m_texturePool.push_back(pooled);
			}
			else if (m_garbage[i].srv != 0)
			{
				m_freeSrvs.push_back(m_garbage[i].srv);
			}
		}
		else
		{
			m_garbage[kept++] = m_garbage[i];
		}
	}
	m_garbage.resize(kept);

	// Pooled textures go when not used again for a while, the oldest first when there are too many.
	const size_t TEXTURE_POOL_SIZE = 64;
	const UINT64 TEXTURE_POOL_FRAMES = 120;
	const size_t first = m_texturePool.size() > TEXTURE_POOL_SIZE ? m_texturePool.size() - TEXTURE_POOL_SIZE : 0;
	kept = 0;
	for (size_t i = 0; i < m_texturePool.size(); ++i)
	{
		if (i >= first && m_texturePool[i].fence + TEXTURE_POOL_FRAMES > completed)
			m_texturePool[kept++] = m_texturePool[i];
		else if (m_texturePool[i].srv != 0)
			m_freeSrvs.push_back(m_texturePool[i].srv);
	}
	m_texturePool.resize(kept);
}

// Creates the Direct3D 12 texture on first use and uploads the levels that changed since the last
// upload. Returns the descriptor to sample it with.
UINT Device::Prepare_Texture(IDirect3DBaseTexture8* base_texture, TextureType& type)
{
	type = TEXTURE_2D;
	if (base_texture == nullptr)
	{
		type = TEXTURE_NONE;
		return NULL_SRV_2D;
	}

	GpuTexture* gpu = nullptr;
	std::vector<Surface*>& subresources = m_textureSubresources;	// in Direct3D 12 subresource order
	subresources.clear();
	UINT levels = 0;
	UINT faces = 1;
	if (base_texture->GetType() == D3DRTYPE_CUBETEXTURE)
	{
		type = TEXTURE_CUBE;
		CubeTexture* cube = static_cast<CubeTexture*>(static_cast<IDirect3DCubeTexture8*>(base_texture));
		gpu = &cube->Gpu();
		if (gpu->resource != nullptr && gpu->checkedGeneration == Surface::Dirty_Generation())
			return gpu->srv;
		levels = cube->GetLevelCount();
		faces = 6;
		for (UINT face = 0; face < 6; ++face)
			for (UINT level = 0; level < levels; ++level)
				subresources.push_back(cube->Face((D3DCUBEMAP_FACES)face, level));
	}
	else if (base_texture->GetType() == D3DRTYPE_TEXTURE)
	{
		Texture* texture = static_cast<Texture*>(static_cast<IDirect3DTexture8*>(base_texture));
		gpu = &texture->Gpu();
		// Nothing became dirty anywhere since this texture was last found up to date.
		if (gpu->resource != nullptr && gpu->checkedGeneration == Surface::Dirty_Generation())
			return gpu->srv;
		levels = texture->GetLevelCount();
		for (UINT level = 0; level < levels; ++level)
			subresources.push_back(texture->Level(level));
	}
	else
	{
		return NULL_SRV_2D;
	}

	Surface* top = subresources[0];
	const D3DSURFACE_DESC& top_desc = top->Desc();
	const FormatInfo* info = Get_Format_Info(top_desc.Format);
	if (info == nullptr)
		return type == TEXTURE_CUBE ? NULL_SRV_CUBE : NULL_SRV_2D;

	if (top->Get_Kind() == Surface::KIND_RENDER_TARGET)
	{
		// Drawn into by the GPU, nothing to upload.
		if (!Ensure_Target(top) || gpu->srv == 0)
			return type == TEXTURE_CUBE ? NULL_SRV_CUBE : NULL_SRV_2D;
		Transition(*gpu, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		return gpu->srv;
	}
	if (!top->Has_Data())
		return type == TEXTURE_CUBE ? NULL_SRV_CUBE : NULL_SRV_2D;

	if (gpu->resource == nullptr)
	{
		if (!Create_Gpu_Resource(*gpu, top_desc.Format, top_desc.Width, top_desc.Height, levels, faces, 0, true))
			return type == TEXTURE_CUBE ? NULL_SRV_CUBE : NULL_SRV_2D;
		for (Surface* surface : subresources)
			surface->Set_Dirty(true);
	}

	bool any_dirty = false;
	for (Surface* surface : subresources)
		any_dirty = any_dirty || surface->Is_Dirty();
	bool complete = true;

	if (any_dirty)
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = gpu->resource.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		if (gpu->state != D3D12_RESOURCE_STATE_COPY_DEST)
		{
			barrier.Transition.StateBefore = gpu->state;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
			m_commandList->ResourceBarrier(1, &barrier);
		}

		const D3D12_RESOURCE_DESC desc = gpu->resource->GetDesc();
		for (UINT index = 0; index < subresources.size(); ++index)
		{
			Surface* surface = subresources[index];
			if (!surface->Is_Dirty())
				continue;

			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
			UINT rows = 0;
			UINT64 row_size = 0;
			UINT64 total = 0;
			m_device->GetCopyableFootprints(&desc, index, 1, 0, &footprint, &rows, &row_size, &total);

			UploadAllocation upload = Allocate_Upload(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
			if (upload.cpu == nullptr)
			{
				complete = false;
				break;
			}

			const UINT source_rows = Get_Row_Count(surface->Desc().Format, surface->Desc().Height);
			for (UINT row = 0; row < rows; ++row)
			{
				unsigned char* dest = upload.cpu + row * footprint.Footprint.RowPitch;
				if (row < source_rows)
					Convert_Row(*info, surface->Data() + row * surface->Pitch(), dest, surface->Desc().Width);
				else
					memset(dest, 0, (size_t)row_size);	// padding of a block compressed texture
			}

			D3D12_TEXTURE_COPY_LOCATION to = {};
			to.pResource = gpu->resource.Get();
			to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			to.SubresourceIndex = index;
			D3D12_TEXTURE_COPY_LOCATION from = {};
			from.pResource = upload.resource;
			from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			from.PlacedFootprint = footprint;
			from.PlacedFootprint.Offset = upload.offset;
			m_commandList->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
			surface->Set_Dirty(false);
		}

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		m_commandList->ResourceBarrier(1, &barrier);
		gpu->state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
	if (complete)
		gpu->checkedGeneration = Surface::Dirty_Generation();

	return gpu->srv;
}

static D3D12_TEXTURE_ADDRESS_MODE Get_Address_Mode(DWORD mode)
{
	if (mode >= D3DTADDRESS_WRAP && mode <= D3DTADDRESS_MIRRORONCE)
		return (D3D12_TEXTURE_ADDRESS_MODE)mode;	// same values
	return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

// The sampler descriptor for a stage's sampler states. Samplers are created once and kept.
UINT Device::Get_Sampler(DWORD stage)
{
	const DWORD* tss = m_state.textureStageStates[stage];
	// Stages mostly keep their sampler states from draw to draw.
	const DWORD states[10] = { tss[D3DTSS_MINFILTER], tss[D3DTSS_MAGFILTER], tss[D3DTSS_MIPFILTER], tss[D3DTSS_MAXANISOTROPY],
		tss[D3DTSS_ADDRESSU], tss[D3DTSS_ADDRESSV], tss[D3DTSS_ADDRESSW], tss[D3DTSS_MIPMAPLODBIAS], tss[D3DTSS_BORDERCOLOR],
		tss[D3DTSS_MAXMIPLEVEL] };
	if (stage < FF_STAGES && m_samplerIndex[stage] != 0 && memcmp(states, m_samplerStates[stage], sizeof(states)) == 0)
		return m_samplerIndex[stage];

	D3D12_SAMPLER_DESC desc = {};
	const DWORD min_filter = tss[D3DTSS_MINFILTER];
	const DWORD mag_filter = tss[D3DTSS_MAGFILTER];
	const DWORD mip_filter = tss[D3DTSS_MIPFILTER];
	if (min_filter == D3DTEXF_ANISOTROPIC || mag_filter == D3DTEXF_ANISOTROPIC)
	{
		desc.Filter = D3D12_FILTER_ANISOTROPIC;
		desc.MaxAnisotropy = max(1u, min(16u, (UINT)tss[D3DTSS_MAXANISOTROPY]));
	}
	else
	{
		const bool min_linear = min_filter >= D3DTEXF_LINEAR;
		const bool mag_linear = mag_filter >= D3DTEXF_LINEAR;
		const bool mip_linear = mip_filter >= D3DTEXF_LINEAR;
		desc.Filter = D3D12_ENCODE_BASIC_FILTER(
			min_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
			mag_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
			mip_linear ? D3D12_FILTER_TYPE_LINEAR : D3D12_FILTER_TYPE_POINT,
			D3D12_FILTER_REDUCTION_TYPE_STANDARD);
		desc.MaxAnisotropy = 1;
	}
	desc.AddressU = Get_Address_Mode(tss[D3DTSS_ADDRESSU]);
	desc.AddressV = Get_Address_Mode(tss[D3DTSS_ADDRESSV]);
	desc.AddressW = Get_Address_Mode(tss[D3DTSS_ADDRESSW]);
	memcpy(&desc.MipLODBias, &tss[D3DTSS_MIPMAPLODBIAS], sizeof(float));
	desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	const DWORD border = tss[D3DTSS_BORDERCOLOR];
	desc.BorderColor[0] = ((border >> 16) & 0xFF) / 255.0f;
	desc.BorderColor[1] = ((border >> 8) & 0xFF) / 255.0f;
	desc.BorderColor[2] = (border & 0xFF) / 255.0f;
	desc.BorderColor[3] = ((border >> 24) & 0xFF) / 255.0f;
	// D3D8 MAXMIPLEVEL is the largest level used, which is the smallest level of detail.
	desc.MinLOD = (float)tss[D3DTSS_MAXMIPLEVEL];
	desc.MaxLOD = mip_filter == D3DTEXF_NONE ? desc.MinLOD : D3D12_FLOAT32_MAX;

	const UINT index = Find_Sampler(desc);
	if (stage < FF_STAGES)
	{
		memcpy(m_samplerStates[stage], states, sizeof(states));
		m_samplerIndex[stage] = index;
	}
	return index;
}

// The index of a sampler with this description in the sampler heap, created on first use.
UINT Device::Find_Sampler(const D3D12_SAMPLER_DESC& desc)
{
	const UINT64 hash = Hash_Bytes(&desc, sizeof(desc));
	auto found = m_samplers.find(hash);
	if (found != m_samplers.end())
		return found->second;

	const UINT index = (UINT)m_samplers.size() + 1;
	if (index >= SAMPLER_HEAP_SIZE)
	{
		D3D8ON12_UNSUPPORTED("more than 2048 different samplers");
		return 0;
	}
	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_samplerHeap->GetCPUDescriptorHandleForHeapStart();
	handle.ptr += (SIZE_T)index * m_samplerDescriptorSize;
	m_device->CreateSampler(&desc, handle);
	m_samplers[hash] = index;
	return index;
}

} // namespace D3D8On12
