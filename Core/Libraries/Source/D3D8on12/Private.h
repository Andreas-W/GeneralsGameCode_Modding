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

// Private.h
// Shared declarations of the D3D8-on-12 implementation.

#pragma once

#include <windows.h>
#include <d3d8.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <vector>

namespace D3D8On12
{

using Microsoft::WRL::ComPtr;

class Direct3D;
class Device;

// Writes a line to the debugger output, and to d3d8on12.log when D3D8ON12_LOG is set.
void Log(const char* format, ...);

// Logs a D3D8 call that this implementation does not support, once per call site.
#define D3D8ON12_UNSUPPORTED(name) \
	do { static bool s_logged = false; if (!s_logged) { s_logged = true; D3D8On12::Log("unsupported: %s", name); } } while (0)

// Reference counting shared by all objects that implement a D3D8 interface.
template <class Interface>
class RefCounted : public Interface
{
public:
	virtual ~RefCounted() {}

	ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG count = InterlockedDecrement(&m_refCount);
		if (count == 0)
			delete this;
		return count;
	}

protected:
	LONG m_refCount = 1;
};

// The Direct3D 12 copy of a texture. It is created and uploaded when the texture is first drawn with.
struct GpuTexture
{
	ComPtr<ID3D12Resource> resource;
	UINT srv = 0;	// descriptor in the shader visible heap, 0 while there is none
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
};

// The Direct3D 12 copy of a vertex or index buffer that is not dynamic. Locks mark the range they
// can change; it is uploaded before the next draw that uses the buffer.
struct GpuBuffer
{
	ComPtr<ID3D12Resource> resource;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
	UINT dirtyBegin = 0;
	UINT dirtyEnd = 0;
};

// The adapters Direct3D 12 can create a device on, in the order IDirect3D8 reports them.
struct AdapterInfo
{
	ComPtr<IDXGIAdapter1> adapter;
	DXGI_ADAPTER_DESC1 desc;
	std::vector<D3DDISPLAYMODE> modes;
};

} // namespace D3D8On12
