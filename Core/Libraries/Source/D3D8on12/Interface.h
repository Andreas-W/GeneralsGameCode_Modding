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

// Interface.h
// IDirect3D8 on DXGI and Direct3D 12: adapters, display modes, capabilities and device creation.

#pragma once

#include "Private.h"

namespace D3D8On12
{

class Direct3D : public RefCounted<IDirect3D8>
{
public:
	// Enumerates the adapters. Returns false if none can run Direct3D 12 at feature level 11.
	bool Init();

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;

	HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* initialize_function) override;
	UINT STDMETHODCALLTYPE GetAdapterCount() override;
	HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER8* identifier) override;
	UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter) override;
	HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, UINT mode, D3DDISPLAYMODE* display_mode) override;
	HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE* mode) override;
	HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT display_format, D3DFORMAT back_buffer_format, BOOL windowed) override;
	HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format) override;
	HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT format, BOOL windowed, D3DMULTISAMPLE_TYPE multisample) override;
	HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format, D3DFORMAT render_target_format, D3DFORMAT depth_stencil_format) override;
	HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS8* caps) override;
	HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter) override;
	HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter, D3DDEVTYPE type, HWND focus_window, DWORD behavior_flags, D3DPRESENT_PARAMETERS* params, IDirect3DDevice8** device) override;

private:
	ComPtr<IDXGIFactory2> m_factory;
	std::vector<AdapterInfo> m_adapters;
};

// Fills in what the device can do; Caps.cpp.
void Fill_Device_Caps(UINT adapter, D3DCAPS8* caps);

} // namespace D3D8On12
