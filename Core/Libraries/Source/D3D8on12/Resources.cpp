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

// Resources.cpp
// Surfaces, textures, vertex and index buffers.

#include "Resources.h"
#include "Device.h"
#include "Formats.h"

namespace D3D8On12
{

template <class Interface>
HRESULT STDMETHODCALLTYPE DeviceChild<Interface>::GetDevice(IDirect3DDevice8** device)
{
	if (device == nullptr)
		return D3DERR_INVALIDCALL;
	m_device->AddRef();
	*device = m_device;
	return D3D_OK;
}

template class DeviceChild<IDirect3DSurface8>;
template class DeviceChild<IDirect3DTexture8>;
template class DeviceChild<IDirect3DCubeTexture8>;
template class DeviceChild<IDirect3DVertexBuffer8>;
template class DeviceChild<IDirect3DIndexBuffer8>;

// Size of the next smaller mip level.
static UINT Half(UINT size)
{
	return size > 1 ? size / 2 : 1;
}

static unsigned Get_Mip_Level_Count(UINT width, UINT height)
{
	unsigned levels = 1;
	while (width > 1 || height > 1)
	{
		width = Half(width);
		height = Half(height);
		++levels;
	}
	return levels;
}

// ----------------------------------------------------------------------------
// Surface
// ----------------------------------------------------------------------------

Surface::Surface(Device* device, IUnknown* container, Kind kind, UINT width, UINT height, D3DFORMAT format,
	DWORD usage, D3DPOOL pool, D3DMULTISAMPLE_TYPE multisample)
	:	DeviceChild<IDirect3DSurface8>(device),
		m_container(container),
		m_kind(kind),
		m_pitch(Get_Pitch(format, width)),
		m_locked(false),
		m_dirty(true)
{
	m_desc.Format = format;
	m_desc.Type = D3DRTYPE_SURFACE;
	m_desc.Usage = usage;
	m_desc.Pool = pool;
	m_desc.Size = m_pitch * Get_Row_Count(format, height);
	m_desc.MultiSampleType = multisample;
	m_desc.Width = width;
	m_desc.Height = height;

	if (kind == KIND_MEMORY)
		m_data.resize(m_desc.Size);
}

HRESULT STDMETHODCALLTYPE Surface::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IDirect3DSurface8)
	{
		AddRef();
		*object = static_cast<IDirect3DSurface8*>(this);
		return S_OK;
	}
	*object = nullptr;
	return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE Surface::AddRef()
{
	if (m_container != nullptr)
		return m_container->AddRef();
	return RefCounted<IDirect3DSurface8>::AddRef();
}

ULONG STDMETHODCALLTYPE Surface::Release()
{
	if (m_container != nullptr)
		return m_container->Release();
	return RefCounted<IDirect3DSurface8>::Release();
}

HRESULT STDMETHODCALLTYPE Surface::GetContainer(REFIID riid, void** container)
{
	if (container == nullptr)
		return D3DERR_INVALIDCALL;
	if (m_container == nullptr)
		return m_device->QueryInterface(riid, container);
	return m_container->QueryInterface(riid, container);
}

HRESULT STDMETHODCALLTYPE Surface::GetDesc(D3DSURFACE_DESC* desc)
{
	if (desc == nullptr)
		return D3DERR_INVALIDCALL;
	*desc = m_desc;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Surface::LockRect(D3DLOCKED_RECT* locked_rect, CONST RECT* rect, DWORD flags)
{
	if (locked_rect == nullptr || m_locked)
		return D3DERR_INVALIDCALL;
	if (m_data.empty())
	{
		// Reading back render targets and the back buffer is not implemented yet.
		D3D8ON12_UNSUPPORTED("LockRect on a render target or depth buffer");
		return D3DERR_INVALIDCALL;
	}

	unsigned offset = 0;
	if (rect != nullptr)
	{
		const FormatInfo* info = Get_Format_Info(m_desc.Format);
		if (info->compressed)
			offset = (rect->top / 4) * m_pitch + (rect->left / 4) * info->bytes;
		else
			offset = rect->top * m_pitch + rect->left * info->bytes;
	}

	locked_rect->Pitch = m_pitch;
	locked_rect->pBits = m_data.data() + offset;
	m_locked = true;
	if ((flags & D3DLOCK_READONLY) == 0)
		m_dirty = true;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Surface::UnlockRect()
{
	if (!m_locked)
		return D3DERR_INVALIDCALL;
	m_locked = false;
	return D3D_OK;
}

// ----------------------------------------------------------------------------
// Texture
// ----------------------------------------------------------------------------

Texture::Texture(Device* device, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool)
	:	Resource<IDirect3DTexture8, D3DRTYPE_TEXTURE>(device),
		m_lod(0)
{
	const unsigned max_levels = Get_Mip_Level_Count(width, height);
	if (levels == 0 || levels > max_levels)
		levels = max_levels;

	Surface::Kind kind = Surface::KIND_MEMORY;
	if (usage & D3DUSAGE_RENDERTARGET)
		kind = Surface::KIND_RENDER_TARGET;
	else if (usage & D3DUSAGE_DEPTHSTENCIL)
		kind = Surface::KIND_DEPTH_STENCIL;

	for (UINT i = 0; i < levels; ++i)
	{
		m_levels.push_back(new Surface(device, this, kind, width, height, format, usage, pool, D3DMULTISAMPLE_NONE));
		width = Half(width);
		height = Half(height);
	}
}

Texture::~Texture()
{
	m_device->Release_Gpu_Texture(m_gpu);
	for (Surface* surface : m_levels)
		delete surface;
}

HRESULT STDMETHODCALLTYPE Texture::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IDirect3DResource8 || riid == IID_IDirect3DBaseTexture8 || riid == IID_IDirect3DTexture8)
	{
		AddRef();
		*object = static_cast<IDirect3DTexture8*>(this);
		return S_OK;
	}
	*object = nullptr;
	return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Texture::GetLevelDesc(UINT level, D3DSURFACE_DESC* desc)
{
	if (level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	return m_levels[level]->GetDesc(desc);
}

HRESULT STDMETHODCALLTYPE Texture::GetSurfaceLevel(UINT level, IDirect3DSurface8** surface)
{
	if (surface == nullptr || level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	m_levels[level]->AddRef();
	*surface = m_levels[level];
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Texture::LockRect(UINT level, D3DLOCKED_RECT* locked_rect, CONST RECT* rect, DWORD flags)
{
	if (level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	return m_levels[level]->LockRect(locked_rect, rect, flags);
}

HRESULT STDMETHODCALLTYPE Texture::UnlockRect(UINT level)
{
	if (level >= m_levels.size())
		return D3DERR_INVALIDCALL;
	return m_levels[level]->UnlockRect();
}

// ----------------------------------------------------------------------------
// CubeTexture
// ----------------------------------------------------------------------------

CubeTexture::CubeTexture(Device* device, UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool)
	:	Resource<IDirect3DCubeTexture8, D3DRTYPE_CUBETEXTURE>(device),
		m_lod(0)
{
	const unsigned max_levels = Get_Mip_Level_Count(edge_length, edge_length);
	if (levels == 0 || levels > max_levels)
		levels = max_levels;
	m_levelCount = levels;

	const Surface::Kind kind = (usage & D3DUSAGE_RENDERTARGET) ? Surface::KIND_RENDER_TARGET : Surface::KIND_MEMORY;
	for (int face = 0; face < 6; ++face)
	{
		UINT size = edge_length;
		for (UINT i = 0; i < levels; ++i)
		{
			m_surfaces.push_back(new Surface(device, this, kind, size, size, format, usage, pool, D3DMULTISAMPLE_NONE));
			size = Half(size);
		}
	}
}

CubeTexture::~CubeTexture()
{
	m_device->Release_Gpu_Texture(m_gpu);
	for (Surface* surface : m_surfaces)
		delete surface;
}

HRESULT STDMETHODCALLTYPE CubeTexture::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IDirect3DResource8 || riid == IID_IDirect3DBaseTexture8 || riid == IID_IDirect3DCubeTexture8)
	{
		AddRef();
		*object = static_cast<IDirect3DCubeTexture8*>(this);
		return S_OK;
	}
	*object = nullptr;
	return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE CubeTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC* desc)
{
	if (level >= m_levelCount)
		return D3DERR_INVALIDCALL;
	return m_surfaces[level]->GetDesc(desc);
}

HRESULT STDMETHODCALLTYPE CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface8** surface)
{
	if (surface == nullptr || face > D3DCUBEMAP_FACE_NEGATIVE_Z || level >= m_levelCount)
		return D3DERR_INVALIDCALL;
	Surface* face_surface = Face(face, level);
	face_surface->AddRef();
	*surface = face_surface;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE CubeTexture::LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT* locked_rect, CONST RECT* rect, DWORD flags)
{
	if (face > D3DCUBEMAP_FACE_NEGATIVE_Z || level >= m_levelCount)
		return D3DERR_INVALIDCALL;
	return Face(face, level)->LockRect(locked_rect, rect, flags);
}

HRESULT STDMETHODCALLTYPE CubeTexture::UnlockRect(D3DCUBEMAP_FACES face, UINT level)
{
	if (face > D3DCUBEMAP_FACE_NEGATIVE_Z || level >= m_levelCount)
		return D3DERR_INVALIDCALL;
	return Face(face, level)->UnlockRect();
}

// ----------------------------------------------------------------------------
// VertexBuffer
// ----------------------------------------------------------------------------

VertexBuffer::VertexBuffer(Device* device, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool)
	:	Resource<IDirect3DVertexBuffer8, D3DRTYPE_VERTEXBUFFER>(device),
		m_data(length)
{
	m_gpu.dirtyEnd = length;
	m_desc.Format = D3DFMT_VERTEXDATA;
	m_desc.Type = D3DRTYPE_VERTEXBUFFER;
	m_desc.Usage = usage;
	m_desc.Pool = pool;
	m_desc.Size = length;
	m_desc.FVF = fvf;
}

HRESULT STDMETHODCALLTYPE VertexBuffer::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IDirect3DResource8 || riid == IID_IDirect3DVertexBuffer8)
	{
		AddRef();
		*object = static_cast<IDirect3DVertexBuffer8*>(this);
		return S_OK;
	}
	*object = nullptr;
	return E_NOINTERFACE;
}

VertexBuffer::~VertexBuffer()
{
	m_device->Release_Gpu_Resource(m_gpu.resource);
}

HRESULT STDMETHODCALLTYPE VertexBuffer::Lock(UINT offset, UINT size, BYTE** data, DWORD flags)
{
	if (data == nullptr || offset > m_data.size() || (size != 0 && offset + size > m_data.size()))
		return D3DERR_INVALIDCALL;
	*data = m_data.data() + offset;

	if ((flags & D3DLOCK_READONLY) == 0)
	{
		const UINT end = size != 0 ? offset + size : (UINT)m_data.size();
		if (m_gpu.dirtyBegin == m_gpu.dirtyEnd)
		{
			m_gpu.dirtyBegin = offset;
			m_gpu.dirtyEnd = end;
		}
		else
		{
			m_gpu.dirtyBegin = min(m_gpu.dirtyBegin, offset);
			m_gpu.dirtyEnd = max(m_gpu.dirtyEnd, end);
		}
	}
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE VertexBuffer::Unlock()
{
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE VertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC* desc)
{
	if (desc == nullptr)
		return D3DERR_INVALIDCALL;
	*desc = m_desc;
	return D3D_OK;
}

// ----------------------------------------------------------------------------
// IndexBuffer
// ----------------------------------------------------------------------------

IndexBuffer::IndexBuffer(Device* device, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool)
	:	Resource<IDirect3DIndexBuffer8, D3DRTYPE_INDEXBUFFER>(device),
		m_data(length)
{
	m_gpu.dirtyEnd = length;
	m_desc.Format = format;
	m_desc.Type = D3DRTYPE_INDEXBUFFER;
	m_desc.Usage = usage;
	m_desc.Pool = pool;
	m_desc.Size = length;
}

HRESULT STDMETHODCALLTYPE IndexBuffer::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IDirect3DResource8 || riid == IID_IDirect3DIndexBuffer8)
	{
		AddRef();
		*object = static_cast<IDirect3DIndexBuffer8*>(this);
		return S_OK;
	}
	*object = nullptr;
	return E_NOINTERFACE;
}

IndexBuffer::~IndexBuffer()
{
	m_device->Release_Gpu_Resource(m_gpu.resource);
}

HRESULT STDMETHODCALLTYPE IndexBuffer::Lock(UINT offset, UINT size, BYTE** data, DWORD flags)
{
	if (data == nullptr || offset > m_data.size() || (size != 0 && offset + size > m_data.size()))
		return D3DERR_INVALIDCALL;
	*data = m_data.data() + offset;

	if ((flags & D3DLOCK_READONLY) == 0)
	{
		const UINT end = size != 0 ? offset + size : (UINT)m_data.size();
		if (m_gpu.dirtyBegin == m_gpu.dirtyEnd)
		{
			m_gpu.dirtyBegin = offset;
			m_gpu.dirtyEnd = end;
		}
		else
		{
			m_gpu.dirtyBegin = min(m_gpu.dirtyBegin, offset);
			m_gpu.dirtyEnd = max(m_gpu.dirtyEnd, end);
		}
	}
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE IndexBuffer::Unlock()
{
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE IndexBuffer::GetDesc(D3DINDEXBUFFER_DESC* desc)
{
	if (desc == nullptr)
		return D3DERR_INVALIDCALL;
	*desc = m_desc;
	return D3D_OK;
}

} // namespace D3D8On12
