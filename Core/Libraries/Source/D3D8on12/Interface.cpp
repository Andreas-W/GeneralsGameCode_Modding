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

// Interface.cpp
// IDirect3D8 on DXGI and Direct3D 12: adapters, display modes, capabilities and device creation.

#include "Interface.h"
#include "Device.h"
#include "Formats.h"
#include "d3d8on12.h"

#include <share.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace D3D8On12
{

void Log(const char* format, ...)
{
	char text[1024];
	va_list args;
	va_start(args, format);
	_vsnprintf_s(text, sizeof(text), _TRUNCATE, format, args);
	va_end(args);

	char line[1100];
	_snprintf_s(line, sizeof(line), _TRUNCATE, "D3D8on12: %s\n", text);
	OutputDebugStringA(line);

	static FILE* s_file = nullptr;
	static bool s_checked = false;
	if (!s_checked)
	{
		s_checked = true;
		char value[8];
		if (GetEnvironmentVariableA("D3D8ON12_LOG", value, sizeof(value)) > 0)
			s_file = _fsopen("d3d8on12.log", "w", _SH_DENYWR);
	}
	if (s_file != nullptr)
	{
		fputs(line, s_file);
		fflush(s_file);
	}
}

static void Enable_Debug_Layer()
{
	char value[8];
	if (GetEnvironmentVariableA("D3D8ON12_DEBUG", value, sizeof(value)) == 0)
		return;
	ComPtr<ID3D12Debug> debug;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
	{
		debug->EnableDebugLayer();
		Log("Direct3D 12 debug layer enabled");
	}
}

// The display modes of the adapter's first output, in the 32-bit format D3D8 calls X8R8G8B8.
static void Enumerate_Modes(AdapterInfo& info)
{
	ComPtr<IDXGIOutput> output;
	if (FAILED(info.adapter->EnumOutputs(0, &output)))
		return;

	UINT count = 0;
	output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, nullptr);
	std::vector<DXGI_MODE_DESC> modes(count);
	if (count == 0 || FAILED(output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &count, modes.data())))
		return;

	for (const DXGI_MODE_DESC& mode : modes)
	{
		D3DDISPLAYMODE d3d_mode;
		d3d_mode.Width = mode.Width;
		d3d_mode.Height = mode.Height;
		d3d_mode.RefreshRate = mode.RefreshRate.Denominator != 0 ? (mode.RefreshRate.Numerator + mode.RefreshRate.Denominator / 2) / mode.RefreshRate.Denominator : 0;
		d3d_mode.Format = D3DFMT_X8R8G8B8;

		// DXGI lists scaling and scanline variants separately.
		bool known = false;
		for (const D3DDISPLAYMODE& existing : info.modes)
		{
			if (existing.Width == d3d_mode.Width && existing.Height == d3d_mode.Height && existing.RefreshRate == d3d_mode.RefreshRate)
			{
				known = true;
				break;
			}
		}
		if (!known)
			info.modes.push_back(d3d_mode);
	}
}

bool Direct3D::Init()
{
	Enable_Debug_Layer();

	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&m_factory))))
	{
		Log("CreateDXGIFactory1 failed");
		return false;
	}

	ComPtr<IDXGIAdapter1> adapter;
	for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
	{
		AdapterInfo info;
		info.adapter = adapter;
		adapter->GetDesc1(&info.desc);
		adapter.Reset();

		if ((info.desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
			continue;
		if (FAILED(D3D12CreateDevice(info.adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
			continue;

		Enumerate_Modes(info);
		char name[128];
		WideCharToMultiByte(CP_ACP, 0, info.desc.Description, -1, name, sizeof(name), nullptr, nullptr);
		Log("adapter %u: %s, %u display modes", (unsigned)m_adapters.size(), name, (unsigned)info.modes.size());
		m_adapters.push_back(info);
	}

	return !m_adapters.empty();
}

HRESULT STDMETHODCALLTYPE Direct3D::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IDirect3D8)
	{
		AddRef();
		*object = static_cast<IDirect3D8*>(this);
		return S_OK;
	}
	*object = nullptr;
	return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE Direct3D::RegisterSoftwareDevice(void*)
{
	return D3DERR_NOTAVAILABLE;
}

UINT STDMETHODCALLTYPE Direct3D::GetAdapterCount()
{
	return (UINT)m_adapters.size();
}

HRESULT STDMETHODCALLTYPE Direct3D::GetAdapterIdentifier(UINT adapter, DWORD, D3DADAPTER_IDENTIFIER8* identifier)
{
	if (identifier == nullptr || adapter >= m_adapters.size())
		return D3DERR_INVALIDCALL;

	const AdapterInfo& info = m_adapters[adapter];
	memset(identifier, 0, sizeof(*identifier));
	WideCharToMultiByte(CP_ACP, 0, info.desc.Description, -1, identifier->Description, sizeof(identifier->Description), nullptr, nullptr);
	strcpy_s(identifier->Driver, "d3d8on12");
	// No vendor or device: the engine picks render paths and driver workarounds from known DX8 era
	// cards by these IDs, which would override what this device reports in its caps.
	identifier->VendorId = 0;
	identifier->DeviceId = 0;
	memcpy(&identifier->DeviceIdentifier, &info.desc.AdapterLuid, sizeof(info.desc.AdapterLuid));
	identifier->WHQLLevel = 1;

	LARGE_INTEGER version;
	if (SUCCEEDED(info.adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version)))
		identifier->DriverVersion = version;
	return D3D_OK;
}

UINT STDMETHODCALLTYPE Direct3D::GetAdapterModeCount(UINT adapter)
{
	if (adapter >= m_adapters.size())
		return 0;
	return (UINT)m_adapters[adapter].modes.size();
}

HRESULT STDMETHODCALLTYPE Direct3D::EnumAdapterModes(UINT adapter, UINT mode, D3DDISPLAYMODE* display_mode)
{
	if (display_mode == nullptr || adapter >= m_adapters.size() || mode >= m_adapters[adapter].modes.size())
		return D3DERR_INVALIDCALL;
	*display_mode = m_adapters[adapter].modes[mode];
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Direct3D::GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE* mode)
{
	if (mode == nullptr || adapter >= m_adapters.size())
		return D3DERR_INVALIDCALL;

	DEVMODEA settings = {};
	settings.dmSize = sizeof(settings);
	EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &settings);
	mode->Width = settings.dmPelsWidth;
	mode->Height = settings.dmPelsHeight;
	mode->RefreshRate = settings.dmDisplayFrequency;
	mode->Format = D3DFMT_X8R8G8B8;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Direct3D::CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT display_format, D3DFORMAT back_buffer_format, BOOL)
{
	if (adapter >= m_adapters.size() || type != D3DDEVTYPE_HAL)
		return D3DERR_NOTAVAILABLE;
	if (display_format != D3DFMT_X8R8G8B8)
		return D3DERR_NOTAVAILABLE;
	if (back_buffer_format != D3DFMT_X8R8G8B8 && back_buffer_format != D3DFMT_A8R8G8B8 && back_buffer_format != D3DFMT_UNKNOWN)
		return D3DERR_NOTAVAILABLE;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Direct3D::CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT, DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format)
{
	if (adapter >= m_adapters.size() || type != D3DDEVTYPE_HAL)
		return D3DERR_NOTAVAILABLE;

	const FormatInfo* info = Get_Format_Info(format);
	if (info == nullptr)
		return D3DERR_NOTAVAILABLE;

	unsigned needed = 0;
	if (usage & D3DUSAGE_RENDERTARGET)
		needed |= FORMAT_RENDER_TARGET;
	if (usage & D3DUSAGE_DEPTHSTENCIL)
		needed |= FORMAT_DEPTH_STENCIL;
	if (resource_type == D3DRTYPE_TEXTURE || resource_type == D3DRTYPE_CUBETEXTURE)
	{
		if ((usage & D3DUSAGE_DEPTHSTENCIL) == 0)
			needed |= FORMAT_TEXTURE;
	}
	if (resource_type == D3DRTYPE_VOLUMETEXTURE)
		return D3DERR_NOTAVAILABLE;
	if (needed == 0)
		needed = info->usage != 0 ? info->usage : FORMAT_TEXTURE;

	return (info->usage & needed) == needed ? D3D_OK : D3DERR_NOTAVAILABLE;
}

HRESULT STDMETHODCALLTYPE Direct3D::CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE multisample)
{
	if (adapter >= m_adapters.size() || type != D3DDEVTYPE_HAL)
		return D3DERR_NOTAVAILABLE;
	// Multisampling is not implemented yet.
	return multisample == D3DMULTISAMPLE_NONE ? D3D_OK : D3DERR_NOTAVAILABLE;
}

HRESULT STDMETHODCALLTYPE Direct3D::CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT, D3DFORMAT, D3DFORMAT depth_stencil_format)
{
	if (adapter >= m_adapters.size() || type != D3DDEVTYPE_HAL)
		return D3DERR_NOTAVAILABLE;
	const FormatInfo* info = Get_Format_Info(depth_stencil_format);
	return (info != nullptr && (info->usage & FORMAT_DEPTH_STENCIL) != 0) ? D3D_OK : D3DERR_NOTAVAILABLE;
}

HRESULT STDMETHODCALLTYPE Direct3D::GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS8* caps)
{
	if (caps == nullptr || adapter >= m_adapters.size() || type != D3DDEVTYPE_HAL)
		return D3DERR_INVALIDCALL;
	Fill_Device_Caps(adapter, caps);
	return D3D_OK;
}

HMONITOR STDMETHODCALLTYPE Direct3D::GetAdapterMonitor(UINT adapter)
{
	if (adapter >= m_adapters.size())
		return nullptr;
	ComPtr<IDXGIOutput> output;
	DXGI_OUTPUT_DESC desc;
	if (FAILED(m_adapters[adapter].adapter->EnumOutputs(0, &output)) || FAILED(output->GetDesc(&desc)))
		return MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
	return desc.Monitor;
}

HRESULT STDMETHODCALLTYPE Direct3D::CreateDevice(UINT adapter, D3DDEVTYPE type, HWND focus_window, DWORD behavior_flags, D3DPRESENT_PARAMETERS* params, IDirect3DDevice8** device)
{
	if (device == nullptr || params == nullptr)
		return D3DERR_INVALIDCALL;
	*device = nullptr;
	if (adapter >= m_adapters.size() || type != D3DDEVTYPE_HAL)
		return D3DERR_NOTAVAILABLE;

	Device* new_device = new Device(this, m_adapters[adapter], adapter, type, focus_window, behavior_flags);
	const HRESULT hr = new_device->Init(params);
	if (FAILED(hr))
	{
		new_device->Release();
		return hr;
	}
	*device = new_device;
	return D3D_OK;
}

} // namespace D3D8On12

IDirect3D8* WINAPI Direct3DCreate8On12(UINT sdk_version)
{
	if (sdk_version != D3D_SDK_VERSION)
		return nullptr;

	D3D8On12::Direct3D* d3d = new D3D8On12::Direct3D();
	if (!d3d->Init())
	{
		D3D8On12::Log("no adapter supports Direct3D 12 feature level 11");
		d3d->Release();
		return nullptr;
	}
	return d3d;
}
