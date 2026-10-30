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

// Resources.h
// Surfaces, textures, vertex and index buffers.
//
// Every lockable resource keeps its contents in system memory, in the D3D8 layout. That is what locks
// read and write, and what is uploaded to Direct3D 12 when the resource is drawn with.

#pragma once

#include "Private.h"

namespace D3D8On12
{

// The methods every resource shares, except the reference counting.
template <class Interface>
class DeviceChild : public RefCounted<Interface>
{
public:
	explicit DeviceChild(Device* device);
	virtual ~DeviceChild() override;

	HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice8** device) override;
	HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, CONST void*, DWORD, DWORD) override { D3D8ON12_UNSUPPORTED("SetPrivateData"); return E_NOTIMPL; }
	HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return D3DERR_NOTFOUND; }
	HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3DERR_NOTFOUND; }

protected:
	// Not reference counted, which would make cycles with the resources the device holds. The device
	// counts its live resources instead and stays until the last one is gone, as in D3D8: the engine
	// releases some textures only after the device.
	Device* m_device;
};

template <class Interface, D3DRESOURCETYPE Type>
class Resource : public DeviceChild<Interface>
{
public:
	explicit Resource(Device* device) : DeviceChild<Interface>(device) {}

	DWORD STDMETHODCALLTYPE SetPriority(DWORD priority) override { DWORD old = m_priority; m_priority = priority; return old; }
	DWORD STDMETHODCALLTYPE GetPriority() override { return m_priority; }
	void STDMETHODCALLTYPE PreLoad() override {}
	D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return Type; }

protected:
	DWORD m_priority = 0;
};

class Surface : public DeviceChild<IDirect3DSurface8>
{
public:
	enum Kind
	{
		KIND_MEMORY,			// image surface or texture level, contents in system memory
		KIND_RENDER_TARGET,
		KIND_DEPTH_STENCIL,
		KIND_BACK_BUFFER,
	};

	// A surface with a container (a texture level) shares the reference count of the container.
	Surface(Device* device, IUnknown* container, Kind kind, UINT width, UINT height, D3DFORMAT format,
		DWORD usage, D3DPOOL pool, D3DMULTISAMPLE_TYPE multisample);
	virtual ~Surface() override;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
	ULONG STDMETHODCALLTYPE AddRef() override;
	ULONG STDMETHODCALLTYPE Release() override;

	HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void** container) override;
	HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* desc) override;
	HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* locked_rect, CONST RECT* rect, DWORD flags) override;
	HRESULT STDMETHODCALLTYPE UnlockRect() override;

	Kind Get_Kind() const { return m_kind; }
	const D3DSURFACE_DESC& Desc() const { return m_desc; }
	bool Has_Data() const { return !m_data.empty(); }
	unsigned char* Data() { return m_data.data(); }
	unsigned Pitch() const { return m_pitch; }

	// Set whenever the contents were changed through a lock or a copy.
	bool Is_Dirty() const { return m_dirty; }
	void Set_Dirty(bool dirty) { m_dirty = dirty; if (dirty) ++s_dirtyGeneration; }
	// Counts up whenever any surface becomes dirty, so textures can tell that none of theirs did.
	static UINT Dirty_Generation() { return s_dirtyGeneration; }

	// The resource the GPU draws into for render targets and depth buffers: the one of the texture
	// for texture levels, an own one for standalone surfaces.
	bool Has_Container() const { return m_container != nullptr; }
	GpuTexture* Gpu_Target() { return m_gpu; }
	UINT Subresource() const { return m_subresource; }
	void Set_Gpu_Target(GpuTexture* gpu, UINT subresource) { m_gpu = gpu; m_subresource = subresource; }
	UINT Target_View() const { return m_targetView; }	// render target or depth view, 0 for none
	void Set_Target_View(UINT view) { m_targetView = view; }

private:
	GpuTexture m_ownGpu;
	GpuTexture* m_gpu;
	UINT m_subresource;
	UINT m_targetView;
	IUnknown* m_container;
	Kind m_kind;
	D3DSURFACE_DESC m_desc;
	std::vector<unsigned char> m_data;
	unsigned m_pitch;
	bool m_locked;
	bool m_dirty;
	static UINT s_dirtyGeneration;
};

class Texture : public Resource<IDirect3DTexture8, D3DRTYPE_TEXTURE>
{
public:
	Texture(Device* device, UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool);
	virtual ~Texture() override;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;

	DWORD STDMETHODCALLTYPE SetLOD(DWORD lod) override { DWORD old = m_lod; m_lod = lod; return old; }
	DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
	DWORD STDMETHODCALLTYPE GetLevelCount() override { return (DWORD)m_levels.size(); }

	HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override;
	HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface8** surface) override;
	HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT* locked_rect, CONST RECT* rect, DWORD flags) override;
	HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override;
	HRESULT STDMETHODCALLTYPE AddDirtyRect(CONST RECT*) override { return D3D_OK; }

	Surface* Level(UINT level) { return m_levels[level]; }
	GpuTexture& Gpu() { return m_gpu; }

private:
	std::vector<Surface*> m_levels;
	DWORD m_lod;
	GpuTexture m_gpu;
};

class CubeTexture : public Resource<IDirect3DCubeTexture8, D3DRTYPE_CUBETEXTURE>
{
public:
	CubeTexture(Device* device, UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool);
	virtual ~CubeTexture() override;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;

	DWORD STDMETHODCALLTYPE SetLOD(DWORD lod) override { DWORD old = m_lod; m_lod = lod; return old; }
	DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
	DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levelCount; }

	HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override;
	HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface8** surface) override;
	HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT* locked_rect, CONST RECT* rect, DWORD flags) override;
	HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES face, UINT level) override;
	HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES, CONST RECT*) override { return D3D_OK; }

	Surface* Face(D3DCUBEMAP_FACES face, UINT level) { return m_surfaces[face * m_levelCount + level]; }
	GpuTexture& Gpu() { return m_gpu; }

private:
	GpuTexture m_gpu;
	std::vector<Surface*> m_surfaces;	// all levels of face 0, then of face 1, ...
	UINT m_levelCount;
	DWORD m_lod;
};

class VertexBuffer : public Resource<IDirect3DVertexBuffer8, D3DRTYPE_VERTEXBUFFER>
{
public:
	VertexBuffer(Device* device, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool);
	virtual ~VertexBuffer() override;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
	HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, BYTE** data, DWORD flags) override;
	HRESULT STDMETHODCALLTYPE Unlock() override;
	HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* desc) override;

	const unsigned char* Data() const { return m_data.data(); }
	UINT Size() const { return (UINT)m_data.size(); }

	// Dynamic buffers and buffers in system memory are copied into upload memory with every draw.
	bool Has_Gpu_Copy() const { return (m_desc.Usage & D3DUSAGE_DYNAMIC) == 0 && m_desc.Pool != D3DPOOL_SYSTEMMEM; }
	GpuBuffer& Gpu() { return m_gpu; }

private:
	D3DVERTEXBUFFER_DESC m_desc;
	std::vector<unsigned char> m_data;
	GpuBuffer m_gpu;
};

class IndexBuffer : public Resource<IDirect3DIndexBuffer8, D3DRTYPE_INDEXBUFFER>
{
public:
	IndexBuffer(Device* device, UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool);
	virtual ~IndexBuffer() override;

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
	HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT size, BYTE** data, DWORD flags) override;
	HRESULT STDMETHODCALLTYPE Unlock() override;
	HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* desc) override;

	const unsigned char* Data() const { return m_data.data(); }
	UINT Size() const { return (UINT)m_data.size(); }

	// Dynamic buffers and buffers in system memory are copied into upload memory with every draw.
	bool Has_Gpu_Copy() const { return (m_desc.Usage & D3DUSAGE_DYNAMIC) == 0 && m_desc.Pool != D3DPOOL_SYSTEMMEM; }
	GpuBuffer& Gpu() { return m_gpu; }

private:
	D3DINDEXBUFFER_DESC m_desc;
	std::vector<unsigned char> m_data;
	GpuBuffer m_gpu;
};

} // namespace D3D8On12
