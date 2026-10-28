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

// Device.cpp
// IDirect3DDevice8 on Direct3D 12.
//
// So far the device keeps all state, creates all resources, clears and presents the back buffer.
// Drawing is not implemented yet: draw calls are accepted and dropped.

#include "Device.h"
#include "Formats.h"
#include "Interface.h"
#include "Resources.h"

#include <string.h>

namespace D3D8On12
{

static DWORD Float_Bits(float value)
{
	DWORD bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void Set_Identity(D3DMATRIX& matrix)
{
	memset(&matrix, 0, sizeof(matrix));
	matrix._11 = matrix._22 = matrix._33 = matrix._44 = 1.0f;
}

// Index into StateValues::transforms, or -1 for transforms that are not stored.
static int Get_Transform_Index(D3DTRANSFORMSTATETYPE state)
{
	if (state == D3DTS_VIEW)
		return 0;
	if (state == D3DTS_PROJECTION)
		return 1;
	if (state >= D3DTS_TEXTURE0 && state <= D3DTS_TEXTURE7)
		return 2 + (state - D3DTS_TEXTURE0);
	if (state >= D3DTS_WORLD && state <= D3DTS_WORLDMATRIX(3))
		return 10 + (state - D3DTS_WORLD);
	return -1;
}

// Shader handles have the lowest bit set, so that they can not be mistaken for FVF codes.
static DWORD Vertex_Shader_Handle(size_t index) { return (DWORD)((index << 1) | 1); }
static bool Is_Vertex_Shader_Handle(DWORD handle) { return (handle & 1) != 0; }
static size_t Vertex_Shader_Index(DWORD handle) { return handle >> 1; }

Device::Device(Direct3D* d3d, const AdapterInfo& adapter, UINT adapter_index, D3DDEVTYPE device_type, HWND focus_window, DWORD behavior_flags)
	:	m_d3d(d3d),
		m_adapter(adapter),
		m_window(nullptr),
		m_rtvDescriptorSize(0),
		m_fenceEvent(nullptr),
		m_nextFenceValue(1),
		m_backBufferIndex(0),
		m_recording(false),
		m_deviceRemoved(false),
		m_backBufferSurface(nullptr),
		m_autoDepthSurface(nullptr),
		m_renderTarget(nullptr),
		m_depthStencil(nullptr),
		m_indices(nullptr),
		m_baseVertexIndex(0),
		m_recordingStateBlock(nullptr),
		m_cursor(nullptr),
		m_cursorVisible(false),
		m_currentPalette(0)
{
	m_d3d->AddRef();

	m_creationParams.AdapterOrdinal = adapter_index;
	m_creationParams.DeviceType = device_type;
	m_creationParams.hFocusWindow = focus_window;
	m_creationParams.BehaviorFlags = behavior_flags;

	memset(&m_presentParams, 0, sizeof(m_presentParams));
	memset(m_frameFenceValues, 0, sizeof(m_frameFenceValues));
	memset(m_textures, 0, sizeof(m_textures));
	memset(m_streams, 0, sizeof(m_streams));
	memset(m_palettes, 0, sizeof(m_palettes));

	for (int i = 0; i < 256; ++i)
		m_gammaRamp.red[i] = m_gammaRamp.green[i] = m_gammaRamp.blue[i] = (WORD)(i * 257);
}

Device::~Device()
{
	Wait_For_GPU();

	for (IDirect3DBaseTexture8*& texture : m_textures)
	{
		if (texture != nullptr)
			texture->Release();
		texture = nullptr;
	}
	for (StreamBinding& stream : m_streams)
	{
		if (stream.buffer != nullptr)
			stream.buffer->Release();
		stream.buffer = nullptr;
	}
	if (m_indices != nullptr)
		m_indices->Release();

	Replace_Surface(m_renderTarget, nullptr);
	Replace_Surface(m_depthStencil, nullptr);
	Release_Swap_Chain_Buffers();

	for (StateValues* block : m_stateBlocks)
		delete block;
	delete m_recordingStateBlock;

	if (m_cursor != nullptr)
		DestroyIcon(m_cursor);
	if (m_fenceEvent != nullptr)
		CloseHandle(m_fenceEvent);

	m_d3d->Release();
}

HRESULT Device::Init(D3DPRESENT_PARAMETERS* params)
{
	m_presentParams = *params;
	m_window = params->hDeviceWindow != nullptr ? params->hDeviceWindow : m_creationParams.hFocusWindow;
	if (m_presentParams.BackBufferWidth == 0 || m_presentParams.BackBufferHeight == 0)
	{
		RECT client;
		GetClientRect(m_window, &client);
		m_presentParams.BackBufferWidth = max(client.right - client.left, 1L);
		m_presentParams.BackBufferHeight = max(client.bottom - client.top, 1L);
	}
	if (m_presentParams.BackBufferFormat == D3DFMT_UNKNOWN)
		m_presentParams.BackBufferFormat = D3DFMT_X8R8G8B8;
	m_presentParams.BackBufferCount = 1;
	*params = m_presentParams;

	if (FAILED(D3D12CreateDevice(m_adapter.adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
	{
		Log("D3D12CreateDevice failed");
		return D3DERR_NOTAVAILABLE;
	}

	m_device.As(&m_infoQueue);

	D3D12_COMMAND_QUEUE_DESC queue_desc = {};
	queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	if (FAILED(m_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&m_queue))))
		return D3DERR_NOTAVAILABLE;

	for (UINT i = 0; i < FRAME_COUNT; ++i)
	{
		if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocators[i]))))
			return D3DERR_OUTOFVIDEOMEMORY;
	}
	if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr, IID_PPV_ARGS(&m_commandList))))
		return D3DERR_OUTOFVIDEOMEMORY;
	m_commandList->Close();

	if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
		return D3DERR_NOTAVAILABLE;
	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

	ComPtr<IDXGIFactory2> factory;
	if (FAILED(m_adapter.adapter->GetParent(IID_PPV_ARGS(&factory))))
		return D3DERR_NOTAVAILABLE;

	DXGI_SWAP_CHAIN_DESC1 swap_desc = {};
	swap_desc.Width = m_presentParams.BackBufferWidth;
	swap_desc.Height = m_presentParams.BackBufferHeight;
	swap_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swap_desc.SampleDesc.Count = 1;
	swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_desc.BufferCount = FRAME_COUNT;
	swap_desc.Scaling = DXGI_SCALING_STRETCH;
	swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	ComPtr<IDXGISwapChain1> swap_chain;
	if (FAILED(factory->CreateSwapChainForHwnd(m_queue.Get(), m_window, &swap_desc, nullptr, nullptr, &swap_chain)))
	{
		Log("CreateSwapChainForHwnd failed");
		return D3DERR_NOTAVAILABLE;
	}
	swap_chain.As(&m_swapChain);
	// The engine switches between windowed and fullscreen itself.
	factory->MakeWindowAssociation(m_window, DXGI_MWA_NO_ALT_ENTER);

	D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
	heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	heap_desc.NumDescriptors = FRAME_COUNT;
	if (FAILED(m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_rtvHeap))))
		return D3DERR_OUTOFVIDEOMEMORY;
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	heap_desc.NumDescriptors = 1;
	if (FAILED(m_device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&m_dsvHeap))))
		return D3DERR_OUTOFVIDEOMEMORY;

	HRESULT hr = Create_Swap_Chain_Buffers();
	if (FAILED(hr))
		return hr;

	Set_Default_State();
	Log("device created: %ux%u, %s", m_presentParams.BackBufferWidth, m_presentParams.BackBufferHeight,
		m_presentParams.Windowed ? "windowed" : "fullscreen");
	return D3D_OK;
}

HRESULT Device::Create_Swap_Chain_Buffers()
{
	D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < FRAME_COUNT; ++i)
	{
		if (FAILED(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]))))
			return D3DERR_NOTAVAILABLE;
		m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtv);
		rtv.ptr += m_rtvDescriptorSize;
	}
	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	const UINT width = m_presentParams.BackBufferWidth;
	const UINT height = m_presentParams.BackBufferHeight;
	m_backBufferSurface = new Surface(this, nullptr, Surface::KIND_BACK_BUFFER, width, height,
		m_presentParams.BackBufferFormat, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE);

	if (m_presentParams.EnableAutoDepthStencil)
	{
		const FormatInfo* info = Get_Format_Info(m_presentParams.AutoDepthStencilFormat);
		const DXGI_FORMAT format = info != nullptr ? info->dxgi : DXGI_FORMAT_D24_UNORM_S8_UINT;

		D3D12_HEAP_PROPERTIES heap = {};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = width;
		desc.Height = height;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = format;
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		D3D12_CLEAR_VALUE clear = {};
		clear.Format = format;
		clear.DepthStencil.Depth = 1.0f;
		if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&m_depthBuffer))))
			return D3DERR_OUTOFVIDEOMEMORY;
		m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());

		m_autoDepthSurface = new Surface(this, nullptr, Surface::KIND_DEPTH_STENCIL, width, height,
			m_presentParams.AutoDepthStencilFormat, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT, D3DMULTISAMPLE_NONE);
	}

	Replace_Surface(m_renderTarget, m_backBufferSurface);
	Replace_Surface(m_depthStencil, m_autoDepthSurface);
	return D3D_OK;
}

void Device::Release_Swap_Chain_Buffers()
{
	for (ComPtr<ID3D12Resource>& buffer : m_backBuffers)
		buffer.Reset();
	m_depthBuffer.Reset();

	if (m_backBufferSurface != nullptr)
		m_backBufferSurface->Release();
	m_backBufferSurface = nullptr;
	if (m_autoDepthSurface != nullptr)
		m_autoDepthSurface->Release();
	m_autoDepthSurface = nullptr;
}

void Device::Replace_Surface(Surface*& slot, Surface* surface)
{
	if (surface != nullptr)
		surface->AddRef();
	if (slot != nullptr)
		slot->Release();
	slot = surface;
}

// The default values of all states, as the D3D8 documentation lists them.
void Device::Set_Default_State()
{
	StateValues& s = m_state;
	memset(&s, 0, sizeof(s));

	DWORD* rs = s.renderStates;
	rs[D3DRS_ZENABLE] = m_presentParams.EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
	rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
	rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
	rs[D3DRS_ZWRITEENABLE] = TRUE;
	rs[D3DRS_LASTPIXEL] = TRUE;
	rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
	rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
	rs[D3DRS_CULLMODE] = D3DCULL_CCW;
	rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
	rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_FOGSTART] = Float_Bits(0.0f);
	rs[D3DRS_FOGEND] = Float_Bits(1.0f);
	rs[D3DRS_FOGDENSITY] = Float_Bits(1.0f);
	rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
	rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
	rs[D3DRS_STENCILMASK] = 0xFFFFFFFF;
	rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
	rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
	rs[D3DRS_CLIPPING] = TRUE;
	rs[D3DRS_LIGHTING] = TRUE;
	rs[D3DRS_COLORVERTEX] = TRUE;
	rs[D3DRS_LOCALVIEWER] = TRUE;
	rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
	rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
	rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
	rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
	rs[D3DRS_POINTSIZE] = Float_Bits(1.0f);
	rs[D3DRS_POINTSIZE_MIN] = Float_Bits(0.0f);
	rs[D3DRS_POINTSCALE_A] = Float_Bits(1.0f);
	rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
	rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
	rs[D3DRS_PATCHSEGMENTS] = Float_Bits(1.0f);
	rs[D3DRS_POINTSIZE_MAX] = Float_Bits(64.0f);
	rs[D3DRS_COLORWRITEENABLE] = 0xF;
	rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;

	for (DWORD stage = 0; stage < MAX_TEXTURE_STAGES; ++stage)
	{
		DWORD* tss = s.textureStageStates[stage];
		tss[D3DTSS_COLOROP] = stage == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
		tss[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
		tss[D3DTSS_COLORARG2] = D3DTA_CURRENT;
		tss[D3DTSS_ALPHAOP] = stage == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
		tss[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
		tss[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
		tss[D3DTSS_TEXCOORDINDEX] = stage;
		tss[D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
		tss[D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
		tss[D3DTSS_ADDRESSW] = D3DTADDRESS_WRAP;
		tss[D3DTSS_MAGFILTER] = D3DTEXF_POINT;
		tss[D3DTSS_MINFILTER] = D3DTEXF_POINT;
		tss[D3DTSS_MIPFILTER] = D3DTEXF_NONE;
		tss[D3DTSS_MAXANISOTROPY] = 1;
		tss[D3DTSS_COLORARG0] = D3DTA_CURRENT;
		tss[D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
		tss[D3DTSS_RESULTARG] = D3DTA_CURRENT;
	}

	for (D3DMATRIX& matrix : s.transforms)
		Set_Identity(matrix);

	for (D3DLIGHT8& light : s.lights)
	{
		light.Type = D3DLIGHT_DIRECTIONAL;
		light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 1.0f;
		light.Direction.z = 1.0f;
	}

	s.viewport.Width = m_presentParams.BackBufferWidth;
	s.viewport.Height = m_presentParams.BackBufferHeight;
	s.viewport.MaxZ = 1.0f;
	s.vertexShader = D3DFVF_XYZ;
}

void Device::Begin_Frame()
{
	if (m_recording)
		return;

	// Wait until the GPU is done with the last frame that used this back buffer and allocator.
	if (m_fence->GetCompletedValue() < m_frameFenceValues[m_backBufferIndex])
	{
		m_fence->SetEventOnCompletion(m_frameFenceValues[m_backBufferIndex], m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	ID3D12CommandAllocator* allocator = m_allocators[m_backBufferIndex].Get();
	allocator->Reset();
	m_commandList->Reset(allocator, nullptr);

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = m_backBuffers[m_backBufferIndex].Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	m_commandList->ResourceBarrier(1, &barrier);

	m_recording = true;
}

// Runs the commands recorded so far and waits for them, then continues recording the same frame.
void Device::Flush_Commands()
{
	if (!m_recording)
		return;
	m_commandList->Close();
	ID3D12CommandList* lists[] = { m_commandList.Get() };
	m_queue->ExecuteCommandLists(1, lists);
	Wait_For_GPU();

	ID3D12CommandAllocator* allocator = m_allocators[m_backBufferIndex].Get();
	allocator->Reset();
	m_commandList->Reset(allocator, nullptr);
}

// Copies the back buffer into a surface in system memory of the same size. Between frames that is
// the frame presented last.
HRESULT Device::Read_Back_Buffer(Surface* dest)
{
	const D3DSURFACE_DESC& desc = m_backBufferSurface->Desc();
	if (dest->Desc().Width != desc.Width || dest->Desc().Height != desc.Height || Get_Pitch(dest->Desc().Format, 1) != 4)
		return D3DERR_INVALIDCALL;

	const bool in_frame = m_recording;
	UINT index = m_backBufferIndex;
	D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_RENDER_TARGET;
	if (!in_frame)
	{
		index = (m_backBufferIndex + FRAME_COUNT - 1) % FRAME_COUNT;
		state = D3D12_RESOURCE_STATE_PRESENT;
	}
	ID3D12Resource* source = m_backBuffers[index].Get();
	Begin_Frame();

	D3D12_RESOURCE_DESC source_desc = source->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
	UINT64 total_size = 0;
	m_device->GetCopyableFootprints(&source_desc, 0, 1, 0, &footprint, nullptr, nullptr, &total_size);

	if (m_readbackBuffer == nullptr || m_readbackBuffer->GetDesc().Width < total_size)
	{
		m_readbackBuffer.Reset();
		D3D12_HEAP_PROPERTIES heap = {};
		heap.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC buffer_desc = {};
		buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		buffer_desc.Width = total_size;
		buffer_desc.Height = 1;
		buffer_desc.DepthOrArraySize = 1;
		buffer_desc.MipLevels = 1;
		buffer_desc.SampleDesc.Count = 1;
		buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_readbackBuffer))))
			return D3DERR_OUTOFVIDEOMEMORY;
	}

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = source;
	barrier.Transition.Subresource = 0;
	barrier.Transition.StateBefore = state;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
	m_commandList->ResourceBarrier(1, &barrier);

	D3D12_TEXTURE_COPY_LOCATION from = {};
	from.pResource = source;
	from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	from.SubresourceIndex = 0;
	D3D12_TEXTURE_COPY_LOCATION to = {};
	to.pResource = m_readbackBuffer.Get();
	to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	to.PlacedFootprint = footprint;
	m_commandList->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
	barrier.Transition.StateAfter = state;
	m_commandList->ResourceBarrier(1, &barrier);
	Flush_Commands();

	void* mapped = nullptr;
	const D3D12_RANGE range = { 0, (SIZE_T)total_size };
	if (FAILED(m_readbackBuffer->Map(0, &range, &mapped)))
		return D3DERR_INVALIDCALL;
	for (UINT y = 0; y < desc.Height; ++y)
		memcpy(dest->Data() + y * dest->Pitch(), (unsigned char*)mapped + footprint.Offset + y * footprint.Footprint.RowPitch, desc.Width * 4);
	const D3D12_RANGE written = { 0, 0 };
	m_readbackBuffer->Unmap(0, &written);
	dest->Set_Dirty(true);
	return D3D_OK;
}

// Writes what the debug layer reported since the last call to the log.
void Device::Log_Debug_Messages()
{
	if (m_infoQueue == nullptr)
		return;
	const UINT64 count = m_infoQueue->GetNumStoredMessages();
	for (UINT64 i = 0; i < count; ++i)
	{
		SIZE_T size = 0;
		m_infoQueue->GetMessage(i, nullptr, &size);
		std::vector<char> buffer(size);
		D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
		if (SUCCEEDED(m_infoQueue->GetMessage(i, message, &size)))
			Log("debug layer: %s", message->pDescription);
	}
	m_infoQueue->ClearStoredMessages();
}

void Device::Wait_For_GPU()
{
	if (m_queue == nullptr || m_fence == nullptr)
		return;
	const UINT64 value = m_nextFenceValue++;
	m_queue->Signal(m_fence.Get(), value);
	if (m_fence->GetCompletedValue() < value)
	{
		m_fence->SetEventOnCompletion(value, m_fenceEvent);
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
}

// ----------------------------------------------------------------------------
// IUnknown
// ----------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE Device::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IDirect3DDevice8)
	{
		AddRef();
		*object = static_cast<IDirect3DDevice8*>(this);
		return S_OK;
	}
	*object = nullptr;
	return E_NOINTERFACE;
}

// ----------------------------------------------------------------------------
// Device, swap chain and presentation
// ----------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE Device::TestCooperativeLevel()
{
	return m_deviceRemoved ? D3DERR_DEVICELOST : D3D_OK;
}

UINT STDMETHODCALLTYPE Device::GetAvailableTextureMem()
{
	// The engine keeps this in a 32-bit value.
	const SIZE_T memory = m_adapter.desc.DedicatedVideoMemory;
	return memory > 0x7FFFFFFF ? 0x7FFFFFFF : (UINT)memory;
}

HRESULT STDMETHODCALLTYPE Device::ResourceManagerDiscardBytes(DWORD)
{
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetDirect3D(IDirect3D8** d3d)
{
	if (d3d == nullptr)
		return D3DERR_INVALIDCALL;
	m_d3d->AddRef();
	*d3d = m_d3d;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetDeviceCaps(D3DCAPS8* caps)
{
	return m_d3d->GetDeviceCaps(m_creationParams.AdapterOrdinal, m_creationParams.DeviceType, caps);
}

HRESULT STDMETHODCALLTYPE Device::GetDisplayMode(D3DDISPLAYMODE* mode)
{
	return m_d3d->GetAdapterDisplayMode(m_creationParams.AdapterOrdinal, mode);
}

HRESULT STDMETHODCALLTYPE Device::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* params)
{
	if (params == nullptr)
		return D3DERR_INVALIDCALL;
	*params = m_creationParams;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetCursorProperties(UINT hotspot_x, UINT hotspot_y, IDirect3DSurface8* cursor_bitmap)
{
	Surface* surface = static_cast<Surface*>(cursor_bitmap);
	if (surface == nullptr || !surface->Has_Data() || surface->Desc().Format != D3DFMT_A8R8G8B8)
		return D3DERR_INVALIDCALL;

	// The cursor image becomes a Windows cursor.
	const D3DSURFACE_DESC& desc = surface->Desc();
	BITMAPINFO info = {};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = desc.Width;
	info.bmiHeader.biHeight = -(LONG)desc.Height;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	info.bmiHeader.biCompression = BI_RGB;

	void* bits = nullptr;
	HDC dc = GetDC(nullptr);
	HBITMAP color = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
	ReleaseDC(nullptr, dc);
	if (color == nullptr)
		return D3DERR_INVALIDCALL;
	for (UINT y = 0; y < desc.Height; ++y)
		memcpy((unsigned char*)bits + y * desc.Width * 4, surface->Data() + y * surface->Pitch(), desc.Width * 4);
	HBITMAP mask = CreateBitmap(desc.Width, desc.Height, 1, 1, nullptr);

	ICONINFO icon = {};
	icon.fIcon = FALSE;
	icon.xHotspot = hotspot_x;
	icon.yHotspot = hotspot_y;
	icon.hbmMask = mask;
	icon.hbmColor = color;
	HCURSOR cursor = CreateIconIndirect(&icon);
	DeleteObject(color);
	DeleteObject(mask);

	if (m_cursor != nullptr)
		DestroyIcon(m_cursor);
	m_cursor = cursor;
	if (m_cursorVisible)
		SetCursor(m_cursor);
	return D3D_OK;
}

void STDMETHODCALLTYPE Device::SetCursorPosition(UINT, UINT, DWORD)
{
	// The Windows cursor follows the mouse by itself.
}

BOOL STDMETHODCALLTYPE Device::ShowCursor(BOOL show)
{
	const BOOL was_visible = m_cursorVisible;
	m_cursorVisible = show != FALSE;
	if (m_cursorVisible && m_cursor != nullptr)
		SetCursor(m_cursor);
	return was_visible;
}

HRESULT STDMETHODCALLTYPE Device::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain8**)
{
	D3D8ON12_UNSUPPORTED("CreateAdditionalSwapChain");
	return D3DERR_NOTAVAILABLE;
}

HRESULT STDMETHODCALLTYPE Device::Reset(D3DPRESENT_PARAMETERS* params)
{
	if (params == nullptr)
		return D3DERR_INVALIDCALL;

	if (m_recording)
	{
		m_commandList->Close();
		m_recording = false;
	}
	Wait_For_GPU();

	Replace_Surface(m_renderTarget, nullptr);
	Replace_Surface(m_depthStencil, nullptr);
	Release_Swap_Chain_Buffers();

	D3DPRESENT_PARAMETERS new_params = *params;
	if (new_params.BackBufferWidth == 0 || new_params.BackBufferHeight == 0)
	{
		RECT client;
		GetClientRect(m_window, &client);
		new_params.BackBufferWidth = max(client.right - client.left, 1L);
		new_params.BackBufferHeight = max(client.bottom - client.top, 1L);
	}
	if (new_params.BackBufferFormat == D3DFMT_UNKNOWN)
		new_params.BackBufferFormat = D3DFMT_X8R8G8B8;
	new_params.BackBufferCount = 1;
	m_presentParams = new_params;
	*params = new_params;

	if (FAILED(m_swapChain->ResizeBuffers(FRAME_COUNT, new_params.BackBufferWidth, new_params.BackBufferHeight, DXGI_FORMAT_B8G8R8A8_UNORM, 0)))
	{
		Log("ResizeBuffers failed");
		return D3DERR_DEVICELOST;
	}
	memset(m_frameFenceValues, 0, sizeof(m_frameFenceValues));

	HRESULT hr = Create_Swap_Chain_Buffers();
	if (FAILED(hr))
		return hr;
	Set_Default_State();
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::Present(CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*)
{
	if (m_deviceRemoved)
		return D3DERR_DEVICELOST;

	Begin_Frame();

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = m_backBuffers[m_backBufferIndex].Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	m_commandList->ResourceBarrier(1, &barrier);
	m_commandList->Close();
	m_recording = false;

	ID3D12CommandList* lists[] = { m_commandList.Get() };
	m_queue->ExecuteCommandLists(1, lists);

	// D3D8 presents windowed swap chains right away; fullscreen ones follow the presentation interval.
	UINT sync_interval = 0;
	if (!m_presentParams.Windowed && m_presentParams.FullScreen_PresentationInterval != D3DPRESENT_INTERVAL_IMMEDIATE)
		sync_interval = 1;

	const HRESULT hr = m_swapChain->Present(sync_interval, 0);
	Log_Debug_Messages();

	const UINT64 value = m_nextFenceValue++;
	m_queue->Signal(m_fence.Get(), value);
	m_frameFenceValues[m_backBufferIndex] = value;
	m_backBufferIndex = m_swapChain->GetCurrentBackBufferIndex();

	if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
	{
		Log("device removed: 0x%08X", m_device->GetDeviceRemovedReason());
		m_deviceRemoved = true;
		return D3DERR_DEVICELOST;
	}
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE, IDirect3DSurface8** back_buffer)
{
	if (back_buffer == nullptr || index != 0 || m_backBufferSurface == nullptr)
		return D3DERR_INVALIDCALL;
	m_backBufferSurface->AddRef();
	*back_buffer = m_backBufferSurface;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetRasterStatus(D3DRASTER_STATUS* status)
{
	if (status == nullptr)
		return D3DERR_INVALIDCALL;
	status->InVBlank = FALSE;
	status->ScanLine = 0;
	return D3D_OK;
}

void STDMETHODCALLTYPE Device::SetGammaRamp(DWORD, CONST D3DGAMMARAMP* ramp)
{
	if (ramp != nullptr)
		m_gammaRamp = *ramp;
}

void STDMETHODCALLTYPE Device::GetGammaRamp(D3DGAMMARAMP* ramp)
{
	if (ramp != nullptr)
		*ramp = m_gammaRamp;
}

// ----------------------------------------------------------------------------
// Resources
// ----------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE Device::CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8** texture)
{
	if (texture == nullptr || width == 0 || height == 0 || Get_Format_Info(format) == nullptr)
		return D3DERR_INVALIDCALL;
	*texture = new Texture(this, width, height, levels, usage, format, pool);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture8**)
{
	D3D8ON12_UNSUPPORTED("CreateVolumeTexture");
	return D3DERR_NOTAVAILABLE;
}

HRESULT STDMETHODCALLTYPE Device::CreateCubeTexture(UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture8** texture)
{
	if (texture == nullptr || edge_length == 0 || Get_Format_Info(format) == nullptr)
		return D3DERR_INVALIDCALL;
	*texture = new CubeTexture(this, edge_length, levels, usage, format, pool);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8** buffer)
{
	if (buffer == nullptr || length == 0)
		return D3DERR_INVALIDCALL;
	*buffer = new VertexBuffer(this, length, usage, fvf, pool);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer8** buffer)
{
	if (buffer == nullptr || length == 0 || (format != D3DFMT_INDEX16 && format != D3DFMT_INDEX32))
		return D3DERR_INVALIDCALL;
	*buffer = new IndexBuffer(this, length, usage, format, pool);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample, BOOL, IDirect3DSurface8** surface)
{
	if (surface == nullptr || Get_Format_Info(format) == nullptr)
		return D3DERR_INVALIDCALL;
	*surface = new Surface(this, nullptr, Surface::KIND_RENDER_TARGET, width, height, format, D3DUSAGE_RENDERTARGET, D3DPOOL_DEFAULT, multisample);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample, IDirect3DSurface8** surface)
{
	if (surface == nullptr || Get_Format_Info(format) == nullptr)
		return D3DERR_INVALIDCALL;
	*surface = new Surface(this, nullptr, Surface::KIND_DEPTH_STENCIL, width, height, format, D3DUSAGE_DEPTHSTENCIL, D3DPOOL_DEFAULT, multisample);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateImageSurface(UINT width, UINT height, D3DFORMAT format, IDirect3DSurface8** surface)
{
	if (surface == nullptr || width == 0 || height == 0 || Get_Format_Info(format) == nullptr)
		return D3DERR_INVALIDCALL;
	*surface = new Surface(this, nullptr, Surface::KIND_MEMORY, width, height, format, 0, D3DPOOL_SYSTEMMEM, D3DMULTISAMPLE_NONE);
	return D3D_OK;
}

// Copies a rectangle between two surfaces in system memory of the same format.
static void Copy_Rect(Surface* source, const RECT& rect, Surface* dest, POINT point)
{
	const D3DFORMAT format = source->Desc().Format;
	const FormatInfo* info = Get_Format_Info(format);
	const unsigned block = info->compressed ? 4 : 1;

	const LONG left = rect.left / block;
	const LONG top = rect.top / block;
	const LONG dest_left = point.x / block;
	const LONG dest_top = point.y / block;
	const LONG columns = (rect.right - rect.left + block - 1) / block;
	const LONG rows = (rect.bottom - rect.top + block - 1) / block;

	for (LONG y = 0; y < rows; ++y)
	{
		memcpy(dest->Data() + (dest_top + y) * dest->Pitch() + dest_left * info->bytes,
			source->Data() + (top + y) * source->Pitch() + left * info->bytes,
			columns * info->bytes);
	}
	dest->Set_Dirty(true);
}

HRESULT STDMETHODCALLTYPE Device::CopyRects(IDirect3DSurface8* source_surface, CONST RECT* source_rects, UINT rect_count, IDirect3DSurface8* dest_surface, CONST POINT* dest_points)
{
	Surface* source = static_cast<Surface*>(source_surface);
	Surface* dest = static_cast<Surface*>(dest_surface);
	if (source == nullptr || dest == nullptr || source->Desc().Format != dest->Desc().Format)
		return D3DERR_INVALIDCALL;

	if (source->Get_Kind() == Surface::KIND_BACK_BUFFER && dest->Has_Data())
	{
		const bool whole = source_rects == nullptr || rect_count == 0;
		if (whole && dest->Desc().Width == source->Desc().Width && dest->Desc().Height == source->Desc().Height)
			return Read_Back_Buffer(dest);

		// Read the whole back buffer, then copy the rectangles out of it.
		Surface* copy = new Surface(this, nullptr, Surface::KIND_MEMORY, source->Desc().Width, source->Desc().Height,
			source->Desc().Format, 0, D3DPOOL_SYSTEMMEM, D3DMULTISAMPLE_NONE);
		HRESULT hr = Read_Back_Buffer(copy);
		if (SUCCEEDED(hr))
			hr = CopyRects(copy, source_rects, rect_count, dest_surface, dest_points);
		copy->Release();
		return hr;
	}

	if (!source->Has_Data() || !dest->Has_Data())
	{
		D3D8ON12_UNSUPPORTED("CopyRects with a render target");
		return D3DERR_INVALIDCALL;
	}

	if (source_rects == nullptr || rect_count == 0)
	{
		const RECT rect = { 0, 0, (LONG)source->Desc().Width, (LONG)source->Desc().Height };
		const POINT point = { 0, 0 };
		Copy_Rect(source, rect, dest, point);
		return D3D_OK;
	}

	for (UINT i = 0; i < rect_count; ++i)
	{
		POINT point = { source_rects[i].left, source_rects[i].top };
		if (dest_points != nullptr)
			point = dest_points[i];
		Copy_Rect(source, source_rects[i], dest, point);
	}
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::UpdateTexture(IDirect3DBaseTexture8* source, IDirect3DBaseTexture8* dest)
{
	if (source == nullptr || dest == nullptr || source->GetType() != dest->GetType())
		return D3DERR_INVALIDCALL;

	if (source->GetType() == D3DRTYPE_TEXTURE)
	{
		Texture* src = static_cast<Texture*>(static_cast<IDirect3DTexture8*>(source));
		Texture* dst = static_cast<Texture*>(static_cast<IDirect3DTexture8*>(dest));

		// The destination may have fewer levels; it then gets the source levels of matching size.
		UINT src_level = 0;
		while (src_level < src->GetLevelCount() && src->Level(src_level)->Desc().Width > dst->Level(0)->Desc().Width)
			++src_level;
		for (UINT level = 0; level < dst->GetLevelCount() && src_level < src->GetLevelCount(); ++level, ++src_level)
		{
			Surface* from = src->Level(src_level);
			Surface* to = dst->Level(level);
			if (!from->Has_Data() || !to->Has_Data() || from->Desc().Size != to->Desc().Size)
				return D3DERR_INVALIDCALL;
			memcpy(to->Data(), from->Data(), from->Desc().Size);
			to->Set_Dirty(true);
		}
		return D3D_OK;
	}

	D3D8ON12_UNSUPPORTED("UpdateTexture on cube or volume textures");
	return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE Device::GetFrontBuffer(IDirect3DSurface8*)
{
	D3D8ON12_UNSUPPORTED("GetFrontBuffer");
	return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE Device::SetRenderTarget(IDirect3DSurface8* render_target, IDirect3DSurface8* depth_stencil)
{
	// A null render target keeps the current one; a null depth buffer means none.
	if (render_target != nullptr)
		Replace_Surface(m_renderTarget, static_cast<Surface*>(render_target));
	Replace_Surface(m_depthStencil, static_cast<Surface*>(depth_stencil));

	const D3DSURFACE_DESC& desc = m_renderTarget->Desc();
	m_state.viewport.X = 0;
	m_state.viewport.Y = 0;
	m_state.viewport.Width = desc.Width;
	m_state.viewport.Height = desc.Height;
	m_state.viewport.MinZ = 0.0f;
	m_state.viewport.MaxZ = 1.0f;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetRenderTarget(IDirect3DSurface8** render_target)
{
	if (render_target == nullptr || m_renderTarget == nullptr)
		return D3DERR_INVALIDCALL;
	m_renderTarget->AddRef();
	*render_target = m_renderTarget;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetDepthStencilSurface(IDirect3DSurface8** depth_stencil)
{
	if (depth_stencil == nullptr)
		return D3DERR_INVALIDCALL;
	*depth_stencil = m_depthStencil;
	if (m_depthStencil == nullptr)
		return D3DERR_NOTFOUND;
	m_depthStencil->AddRef();
	return D3D_OK;
}

// ----------------------------------------------------------------------------
// Scene and clearing
// ----------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE Device::BeginScene()
{
	Begin_Frame();
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::EndScene()
{
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::Clear(DWORD count, CONST D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
	Begin_Frame();

	// D3D8 clears the viewport, or the given rectangles within it.
	const D3DVIEWPORT8& vp = m_state.viewport;
	std::vector<D3D12_RECT> clear_rects;
	if (count == 0 || rects == nullptr)
	{
		const D3D12_RECT rect = { (LONG)vp.X, (LONG)vp.Y, (LONG)(vp.X + vp.Width), (LONG)(vp.Y + vp.Height) };
		clear_rects.push_back(rect);
	}
	else
	{
		for (DWORD i = 0; i < count; ++i)
		{
			D3D12_RECT rect;
			rect.left = max(rects[i].x1, (LONG)vp.X);
			rect.top = max(rects[i].y1, (LONG)vp.Y);
			rect.right = min(rects[i].x2, (LONG)(vp.X + vp.Width));
			rect.bottom = min(rects[i].y2, (LONG)(vp.Y + vp.Height));
			if (rect.left < rect.right && rect.top < rect.bottom)
				clear_rects.push_back(rect);
		}
		if (clear_rects.empty())
			return D3D_OK;
	}

	if ((flags & D3DCLEAR_TARGET) != 0)
	{
		if (m_renderTarget == m_backBufferSurface)
		{
			const float rgba[4] =
			{
				((color >> 16) & 0xFF) / 255.0f,
				((color >> 8) & 0xFF) / 255.0f,
				(color & 0xFF) / 255.0f,
				((color >> 24) & 0xFF) / 255.0f,
			};
			D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
			rtv.ptr += m_backBufferIndex * m_rtvDescriptorSize;
			m_commandList->ClearRenderTargetView(rtv, rgba, (UINT)clear_rects.size(), clear_rects.data());
		}
		else
		{
			D3D8ON12_UNSUPPORTED("Clear on a render target texture");
		}
	}

	D3D12_CLEAR_FLAGS depth_flags = (D3D12_CLEAR_FLAGS)0;
	if ((flags & D3DCLEAR_ZBUFFER) != 0)
		depth_flags |= D3D12_CLEAR_FLAG_DEPTH;
	if ((flags & D3DCLEAR_STENCIL) != 0)
		depth_flags |= D3D12_CLEAR_FLAG_STENCIL;
	if (depth_flags != 0 && m_depthStencil != nullptr && m_depthStencil == m_autoDepthSurface)
	{
		m_commandList->ClearDepthStencilView(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), depth_flags,
			z, (UINT8)stencil, (UINT)clear_rects.size(), clear_rects.data());
	}
	return D3D_OK;
}

// ----------------------------------------------------------------------------
// Fixed function state
// ----------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE Device::SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX* matrix)
{
	const int index = Get_Transform_Index(state);
	if (matrix == nullptr || index < 0)
		return D3DERR_INVALIDCALL;
	m_state.transforms[index] = *matrix;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX* matrix)
{
	const int index = Get_Transform_Index(state);
	if (matrix == nullptr || index < 0)
		return D3DERR_INVALIDCALL;
	*matrix = m_state.transforms[index];
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::MultiplyTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX* matrix)
{
	const int index = Get_Transform_Index(state);
	if (matrix == nullptr || index < 0)
		return D3DERR_INVALIDCALL;
	const D3DMATRIX a = m_state.transforms[index];
	D3DMATRIX& r = m_state.transforms[index];
	for (int row = 0; row < 4; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			r.m[row][col] = a.m[row][0] * matrix->m[0][col] + a.m[row][1] * matrix->m[1][col]
				+ a.m[row][2] * matrix->m[2][col] + a.m[row][3] * matrix->m[3][col];
		}
	}
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetViewport(CONST D3DVIEWPORT8* viewport)
{
	if (viewport == nullptr)
		return D3DERR_INVALIDCALL;
	m_state.viewport = *viewport;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetViewport(D3DVIEWPORT8* viewport)
{
	if (viewport == nullptr)
		return D3DERR_INVALIDCALL;
	*viewport = m_state.viewport;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetMaterial(CONST D3DMATERIAL8* material)
{
	if (material == nullptr)
		return D3DERR_INVALIDCALL;
	m_state.material = *material;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetMaterial(D3DMATERIAL8* material)
{
	if (material == nullptr)
		return D3DERR_INVALIDCALL;
	*material = m_state.material;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetLight(DWORD index, CONST D3DLIGHT8* light)
{
	if (light == nullptr || index >= MAX_LIGHTS)
		return D3DERR_INVALIDCALL;
	m_state.lights[index] = *light;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetLight(DWORD index, D3DLIGHT8* light)
{
	if (light == nullptr || index >= MAX_LIGHTS)
		return D3DERR_INVALIDCALL;
	*light = m_state.lights[index];
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::LightEnable(DWORD index, BOOL enable)
{
	if (index >= MAX_LIGHTS)
		return D3DERR_INVALIDCALL;
	m_state.lightEnabled[index] = enable;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetLightEnable(DWORD index, BOOL* enable)
{
	if (enable == nullptr || index >= MAX_LIGHTS)
		return D3DERR_INVALIDCALL;
	*enable = m_state.lightEnabled[index];
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetClipPlane(DWORD index, CONST float* plane)
{
	if (plane == nullptr || index >= MAX_CLIP_PLANES)
		return D3DERR_INVALIDCALL;
	memcpy(m_state.clipPlanes[index], plane, sizeof(m_state.clipPlanes[index]));
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetClipPlane(DWORD index, float* plane)
{
	if (plane == nullptr || index >= MAX_CLIP_PLANES)
		return D3DERR_INVALIDCALL;
	memcpy(plane, m_state.clipPlanes[index], sizeof(m_state.clipPlanes[index]));
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetRenderState(D3DRENDERSTATETYPE state, DWORD value)
{
	if ((unsigned)state >= 256)
		return D3DERR_INVALIDCALL;
	m_state.renderStates[state] = value;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetRenderState(D3DRENDERSTATETYPE state, DWORD* value)
{
	if (value == nullptr || (unsigned)state >= 256)
		return D3DERR_INVALIDCALL;
	*value = m_state.renderStates[state];
	return D3D_OK;
}

// State blocks record all values in StateValues, not only the recorded or the requested ones.
HRESULT STDMETHODCALLTYPE Device::BeginStateBlock()
{
	if (m_recordingStateBlock != nullptr)
		return D3DERR_INVALIDCALL;
	m_recordingStateBlock = new StateValues(m_state);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::EndStateBlock(DWORD* token)
{
	if (token == nullptr || m_recordingStateBlock == nullptr)
		return D3DERR_INVALIDCALL;
	*m_recordingStateBlock = m_state;
	m_stateBlocks.push_back(m_recordingStateBlock);
	m_recordingStateBlock = nullptr;
	*token = (DWORD)m_stateBlocks.size();
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::ApplyStateBlock(DWORD token)
{
	if (token == 0 || token > m_stateBlocks.size() || m_stateBlocks[token - 1] == nullptr)
		return D3DERR_INVALIDCALL;
	m_state = *m_stateBlocks[token - 1];
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::CaptureStateBlock(DWORD token)
{
	if (token == 0 || token > m_stateBlocks.size() || m_stateBlocks[token - 1] == nullptr)
		return D3DERR_INVALIDCALL;
	*m_stateBlocks[token - 1] = m_state;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::DeleteStateBlock(DWORD token)
{
	if (token == 0 || token > m_stateBlocks.size() || m_stateBlocks[token - 1] == nullptr)
		return D3DERR_INVALIDCALL;
	delete m_stateBlocks[token - 1];
	m_stateBlocks[token - 1] = nullptr;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreateStateBlock(D3DSTATEBLOCKTYPE, DWORD* token)
{
	if (token == nullptr)
		return D3DERR_INVALIDCALL;
	m_stateBlocks.push_back(new StateValues(m_state));
	*token = (DWORD)m_stateBlocks.size();
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetClipStatus(CONST D3DCLIPSTATUS8*)
{
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetClipStatus(D3DCLIPSTATUS8* status)
{
	if (status == nullptr)
		return D3DERR_INVALIDCALL;
	memset(status, 0, sizeof(*status));
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetTexture(DWORD stage, IDirect3DBaseTexture8** texture)
{
	if (texture == nullptr || stage >= MAX_TEXTURE_STAGES)
		return D3DERR_INVALIDCALL;
	*texture = m_textures[stage];
	if (*texture != nullptr)
		(*texture)->AddRef();
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetTexture(DWORD stage, IDirect3DBaseTexture8* texture)
{
	if (stage >= MAX_TEXTURE_STAGES)
		return D3DERR_INVALIDCALL;
	if (texture != nullptr)
		texture->AddRef();
	if (m_textures[stage] != nullptr)
		m_textures[stage]->Release();
	m_textures[stage] = texture;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD* value)
{
	if (value == nullptr || stage >= MAX_TEXTURE_STAGES || (unsigned)type >= 32)
		return D3DERR_INVALIDCALL;
	*value = m_state.textureStageStates[stage][type];
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value)
{
	if (stage >= MAX_TEXTURE_STAGES || (unsigned)type >= 32)
		return D3DERR_INVALIDCALL;
	m_state.textureStageStates[stage][type] = value;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::ValidateDevice(DWORD* passes)
{
	if (passes != nullptr)
		*passes = 1;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetInfo(DWORD, void*, DWORD)
{
	return E_FAIL;
}

HRESULT STDMETHODCALLTYPE Device::SetPaletteEntries(UINT palette, CONST PALETTEENTRY* entries)
{
	if (entries == nullptr || palette >= 256)
		return D3DERR_INVALIDCALL;
	memcpy(m_palettes[palette], entries, sizeof(m_palettes[palette]));
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetPaletteEntries(UINT palette, PALETTEENTRY* entries)
{
	if (entries == nullptr || palette >= 256)
		return D3DERR_INVALIDCALL;
	memcpy(entries, m_palettes[palette], sizeof(m_palettes[palette]));
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetCurrentTexturePalette(UINT palette)
{
	if (palette >= 256)
		return D3DERR_INVALIDCALL;
	m_currentPalette = palette;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetCurrentTexturePalette(UINT* palette)
{
	if (palette == nullptr)
		return D3DERR_INVALIDCALL;
	*palette = m_currentPalette;
	return D3D_OK;
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE Device::DrawPrimitive(D3DPRIMITIVETYPE, UINT, UINT)
{
	D3D8ON12_UNSUPPORTED("DrawPrimitive");
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT)
{
	D3D8ON12_UNSUPPORTED("DrawIndexedPrimitive");
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::DrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, CONST void*, UINT)
{
	D3D8ON12_UNSUPPORTED("DrawPrimitiveUP");
	// Like D3D8, leave stream 0 and the indices unset.
	SetStreamSource(0, nullptr, 0);
	SetIndices(nullptr, 0);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, CONST void*, D3DFORMAT, CONST void*, UINT)
{
	D3D8ON12_UNSUPPORTED("DrawIndexedPrimitiveUP");
	SetStreamSource(0, nullptr, 0);
	SetIndices(nullptr, 0);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer8*, DWORD)
{
	D3D8ON12_UNSUPPORTED("ProcessVertices");
	return D3DERR_INVALIDCALL;
}

// ----------------------------------------------------------------------------
// Shaders
// ----------------------------------------------------------------------------

// Length in DWORDs of a vertex shader declaration, including its D3DVSD_END token.
static size_t Get_Declaration_Length(const DWORD* declaration)
{
	size_t length = 0;
	while (declaration[length] != D3DVSD_END())
	{
		const DWORD token = declaration[length];
		const DWORD type = (token & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;
		if (type == D3DVSD_TOKEN_CONSTMEM)
			length += 1 + 4 * ((token & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT);
		else if (type == D3DVSD_TOKEN_EXT)
			length += 1 + ((token & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT);
		else
			length += 1;
	}
	return length + 1;
}

// Length in DWORDs of shader byte code, including its end token.
static size_t Get_Function_Length(const DWORD* function)
{
	size_t length = 1;	// version token
	while (function[length] != 0x0000FFFF)
	{
		const DWORD token = function[length];
		if ((token & 0xFFFF) == 0xFFFE)	// comment
			length += 1 + (token >> 16);
		else
			length += 1;
	}
	return length + 1;
}

HRESULT STDMETHODCALLTYPE Device::CreateVertexShader(CONST DWORD* declaration, CONST DWORD* function, DWORD* handle, DWORD)
{
	if (declaration == nullptr || handle == nullptr)
		return D3DERR_INVALIDCALL;

	ShaderData shader;
	shader.used = true;
	shader.declaration.assign(declaration, declaration + Get_Declaration_Length(declaration));
	if (function != nullptr)
		shader.function.assign(function, function + Get_Function_Length(function));
	m_vertexShaders.push_back(shader);
	*handle = Vertex_Shader_Handle(m_vertexShaders.size() - 1);
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetVertexShader(DWORD handle)
{
	m_state.vertexShader = handle;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetVertexShader(DWORD* handle)
{
	if (handle == nullptr)
		return D3DERR_INVALIDCALL;
	*handle = m_state.vertexShader;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::DeleteVertexShader(DWORD handle)
{
	const size_t index = Vertex_Shader_Index(handle);
	if (!Is_Vertex_Shader_Handle(handle) || index >= m_vertexShaders.size() || !m_vertexShaders[index].used)
		return D3DERR_INVALIDCALL;
	m_vertexShaders[index] = ShaderData();
	m_vertexShaders[index].used = false;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetVertexShaderConstant(DWORD reg, CONST void* data, DWORD count)
{
	if (data == nullptr || reg + count > MAX_VERTEX_SHADER_CONSTANTS)
		return D3DERR_INVALIDCALL;
	memcpy(m_state.vertexShaderConstants[reg], data, count * 4 * sizeof(float));
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetVertexShaderConstant(DWORD reg, void* data, DWORD count)
{
	if (data == nullptr || reg + count > MAX_VERTEX_SHADER_CONSTANTS)
		return D3DERR_INVALIDCALL;
	memcpy(data, m_state.vertexShaderConstants[reg], count * 4 * sizeof(float));
	return D3D_OK;
}

static HRESULT Copy_Shader_Data(const std::vector<DWORD>& source, void* data, DWORD* size)
{
	if (size == nullptr)
		return D3DERR_INVALIDCALL;
	const DWORD bytes = (DWORD)(source.size() * sizeof(DWORD));
	if (data == nullptr)
	{
		*size = bytes;
		return D3D_OK;
	}
	if (*size < bytes)
		return D3DERR_MOREDATA;
	memcpy(data, source.data(), bytes);
	*size = bytes;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetVertexShaderDeclaration(DWORD handle, void* data, DWORD* size)
{
	const size_t index = Vertex_Shader_Index(handle);
	if (!Is_Vertex_Shader_Handle(handle) || index >= m_vertexShaders.size() || !m_vertexShaders[index].used)
		return D3DERR_INVALIDCALL;
	return Copy_Shader_Data(m_vertexShaders[index].declaration, data, size);
}

HRESULT STDMETHODCALLTYPE Device::GetVertexShaderFunction(DWORD handle, void* data, DWORD* size)
{
	const size_t index = Vertex_Shader_Index(handle);
	if (!Is_Vertex_Shader_Handle(handle) || index >= m_vertexShaders.size() || !m_vertexShaders[index].used)
		return D3DERR_INVALIDCALL;
	return Copy_Shader_Data(m_vertexShaders[index].function, data, size);
}

HRESULT STDMETHODCALLTYPE Device::SetStreamSource(UINT stream, IDirect3DVertexBuffer8* buffer, UINT stride)
{
	if (stream >= MAX_STREAMS)
		return D3DERR_INVALIDCALL;
	if (buffer != nullptr)
		buffer->AddRef();
	if (m_streams[stream].buffer != nullptr)
		m_streams[stream].buffer->Release();
	m_streams[stream].buffer = buffer;
	m_streams[stream].stride = stride;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetStreamSource(UINT stream, IDirect3DVertexBuffer8** buffer, UINT* stride)
{
	if (buffer == nullptr || stride == nullptr || stream >= MAX_STREAMS)
		return D3DERR_INVALIDCALL;
	*buffer = m_streams[stream].buffer;
	if (*buffer != nullptr)
		(*buffer)->AddRef();
	*stride = m_streams[stream].stride;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetIndices(IDirect3DIndexBuffer8* buffer, UINT base_vertex_index)
{
	if (buffer != nullptr)
		buffer->AddRef();
	if (m_indices != nullptr)
		m_indices->Release();
	m_indices = buffer;
	m_baseVertexIndex = base_vertex_index;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetIndices(IDirect3DIndexBuffer8** buffer, UINT* base_vertex_index)
{
	if (buffer == nullptr || base_vertex_index == nullptr)
		return D3DERR_INVALIDCALL;
	*buffer = m_indices;
	if (m_indices != nullptr)
		m_indices->AddRef();
	*base_vertex_index = m_baseVertexIndex;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::CreatePixelShader(CONST DWORD* function, DWORD* handle)
{
	if (function == nullptr || handle == nullptr)
		return D3DERR_INVALIDCALL;
	ShaderData shader;
	shader.used = true;
	shader.function.assign(function, function + Get_Function_Length(function));
	m_pixelShaders.push_back(shader);
	*handle = (DWORD)m_pixelShaders.size();
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetPixelShader(DWORD handle)
{
	m_state.pixelShader = handle;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetPixelShader(DWORD* handle)
{
	if (handle == nullptr)
		return D3DERR_INVALIDCALL;
	*handle = m_state.pixelShader;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::DeletePixelShader(DWORD handle)
{
	if (handle == 0 || handle > m_pixelShaders.size() || !m_pixelShaders[handle - 1].used)
		return D3DERR_INVALIDCALL;
	m_pixelShaders[handle - 1] = ShaderData();
	m_pixelShaders[handle - 1].used = false;
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::SetPixelShaderConstant(DWORD reg, CONST void* data, DWORD count)
{
	if (data == nullptr || reg + count > MAX_PIXEL_SHADER_CONSTANTS)
		return D3DERR_INVALIDCALL;
	memcpy(m_state.pixelShaderConstants[reg], data, count * 4 * sizeof(float));
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetPixelShaderConstant(DWORD reg, void* data, DWORD count)
{
	if (data == nullptr || reg + count > MAX_PIXEL_SHADER_CONSTANTS)
		return D3DERR_INVALIDCALL;
	memcpy(data, m_state.pixelShaderConstants[reg], count * 4 * sizeof(float));
	return D3D_OK;
}

HRESULT STDMETHODCALLTYPE Device::GetPixelShaderFunction(DWORD handle, void* data, DWORD* size)
{
	if (handle == 0 || handle > m_pixelShaders.size() || !m_pixelShaders[handle - 1].used)
		return D3DERR_INVALIDCALL;
	return Copy_Shader_Data(m_pixelShaders[handle - 1].function, data, size);
}

HRESULT STDMETHODCALLTYPE Device::DrawRectPatch(UINT, CONST float*, CONST D3DRECTPATCH_INFO*)
{
	D3D8ON12_UNSUPPORTED("DrawRectPatch");
	return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE Device::DrawTriPatch(UINT, CONST float*, CONST D3DTRIPATCH_INFO*)
{
	D3D8ON12_UNSUPPORTED("DrawTriPatch");
	return D3DERR_INVALIDCALL;
}

HRESULT STDMETHODCALLTYPE Device::DeletePatch(UINT)
{
	return D3D_OK;
}

} // namespace D3D8On12
