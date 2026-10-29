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

// Device.h
// IDirect3DDevice8 on Direct3D 12.

#pragma once

#include "Private.h"
#include "Pipeline.h"

#include <unordered_map>

namespace D3D8On12
{

class Surface;

const unsigned MAX_TEXTURE_STAGES = 8;
const unsigned MAX_STREAMS = 16;
const unsigned MAX_LIGHTS = 8;
const unsigned MAX_CLIP_PLANES = 6;
const unsigned MAX_VERTEX_SHADER_CONSTANTS = 96;
const unsigned MAX_PIXEL_SHADER_CONSTANTS = 8;
const unsigned TRANSFORM_COUNT = 14;	// view, projection, texture 0-7, world 0-3
const unsigned FRAME_COUNT = 3;

// The device state that state blocks record. Bound resources are kept apart, as they are reference counted.
struct StateValues
{
	DWORD renderStates[256];
	DWORD textureStageStates[MAX_TEXTURE_STAGES][32];
	D3DMATRIX transforms[TRANSFORM_COUNT];
	D3DMATERIAL8 material;
	D3DLIGHT8 lights[MAX_LIGHTS];
	BOOL lightEnabled[MAX_LIGHTS];
	float clipPlanes[MAX_CLIP_PLANES][4];
	D3DVIEWPORT8 viewport;
	DWORD vertexShader;
	DWORD pixelShader;
	float vertexShaderConstants[MAX_VERTEX_SHADER_CONSTANTS][4];
	float pixelShaderConstants[MAX_PIXEL_SHADER_CONSTANTS][4];
};

struct StreamBinding
{
	IDirect3DVertexBuffer8* buffer;
	UINT stride;
};

class PostProcessor;

class Device : public RefCounted<IDirect3DDevice8>
{
	friend class PostProcessor;

public:
	Device(Direct3D* d3d, const AdapterInfo& adapter, UINT adapter_index, D3DDEVTYPE device_type, HWND focus_window, DWORD behavior_flags);
	virtual ~Device() override;

	// Creates the Direct3D 12 device and the swap chain. Returns a D3D8 error code on failure.
	HRESULT Init(D3DPRESENT_PARAMETERS* params);

	// The last release drops what the device holds; the device itself goes once its last resource does.
	ULONG STDMETHODCALLTYPE Release() override;
	void Child_Created();
	void Child_Destroyed();

	ID3D12Device* D3D12_Device() const { return m_device.Get(); }

	// Releases the Direct3D 12 copy of a texture once the GPU is done with the frames that used it.
	void Release_Gpu_Texture(GpuTexture& gpu);
	void Release_Gpu_Resource(ComPtr<ID3D12Resource>& resource);
	UINT Allocate_Target_View(bool depth);
	void Free_Target_View(bool depth, UINT index);

	// IUnknown
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;

	// IDirect3DDevice8
	HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override;
	UINT STDMETHODCALLTYPE GetAvailableTextureMem() override;
	HRESULT STDMETHODCALLTYPE ResourceManagerDiscardBytes(DWORD bytes) override;
	HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D8** d3d) override;
	HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS8* caps) override;
	HRESULT STDMETHODCALLTYPE GetDisplayMode(D3DDISPLAYMODE* mode) override;
	HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* params) override;
	HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT hotspot_x, UINT hotspot_y, IDirect3DSurface8* cursor_bitmap) override;
	void STDMETHODCALLTYPE SetCursorPosition(UINT x, UINT y, DWORD flags) override;
	BOOL STDMETHODCALLTYPE ShowCursor(BOOL show) override;
	HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS* params, IDirect3DSwapChain8** swap_chain) override;
	HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* params) override;
	HRESULT STDMETHODCALLTYPE Present(CONST RECT* source_rect, CONST RECT* dest_rect, HWND dest_window, CONST RGNDATA* dirty_region) override;
	HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface8** back_buffer) override;
	HRESULT STDMETHODCALLTYPE GetRasterStatus(D3DRASTER_STATUS* status) override;
	void STDMETHODCALLTYPE SetGammaRamp(DWORD flags, CONST D3DGAMMARAMP* ramp) override;
	void STDMETHODCALLTYPE GetGammaRamp(D3DGAMMARAMP* ramp) override;
	HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8** texture) override;
	HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture8** texture) override;
	HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture8** texture) override;
	HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer8** buffer) override;
	HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer8** buffer) override;
	HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample, BOOL lockable, IDirect3DSurface8** surface) override;
	HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multisample, IDirect3DSurface8** surface) override;
	HRESULT STDMETHODCALLTYPE CreateImageSurface(UINT width, UINT height, D3DFORMAT format, IDirect3DSurface8** surface) override;
	HRESULT STDMETHODCALLTYPE CopyRects(IDirect3DSurface8* source, CONST RECT* source_rects, UINT rect_count, IDirect3DSurface8* dest, CONST POINT* dest_points) override;
	HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture8* source, IDirect3DBaseTexture8* dest) override;
	HRESULT STDMETHODCALLTYPE GetFrontBuffer(IDirect3DSurface8* dest) override;
	HRESULT STDMETHODCALLTYPE SetRenderTarget(IDirect3DSurface8* render_target, IDirect3DSurface8* depth_stencil) override;
	HRESULT STDMETHODCALLTYPE GetRenderTarget(IDirect3DSurface8** render_target) override;
	HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface8** depth_stencil) override;
	HRESULT STDMETHODCALLTYPE BeginScene() override;
	HRESULT STDMETHODCALLTYPE EndScene() override;
	HRESULT STDMETHODCALLTYPE Clear(DWORD count, CONST D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;
	HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX* matrix) override;
	HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX* matrix) override;
	HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state, CONST D3DMATRIX* matrix) override;
	HRESULT STDMETHODCALLTYPE SetViewport(CONST D3DVIEWPORT8* viewport) override;
	HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT8* viewport) override;
	HRESULT STDMETHODCALLTYPE SetMaterial(CONST D3DMATERIAL8* material) override;
	HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL8* material) override;
	HRESULT STDMETHODCALLTYPE SetLight(DWORD index, CONST D3DLIGHT8* light) override;
	HRESULT STDMETHODCALLTYPE GetLight(DWORD index, D3DLIGHT8* light) override;
	HRESULT STDMETHODCALLTYPE LightEnable(DWORD index, BOOL enable) override;
	HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD index, BOOL* enable) override;
	HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD index, CONST float* plane) override;
	HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD index, float* plane) override;
	HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override;
	HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state, DWORD* value) override;
	HRESULT STDMETHODCALLTYPE BeginStateBlock() override;
	HRESULT STDMETHODCALLTYPE EndStateBlock(DWORD* token) override;
	HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD token) override;
	HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD token) override;
	HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD token) override;
	HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type, DWORD* token) override;
	HRESULT STDMETHODCALLTYPE SetClipStatus(CONST D3DCLIPSTATUS8* status) override;
	HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS8* status) override;
	HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DBaseTexture8** texture) override;
	HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture8* texture) override;
	HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD* value) override;
	HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override;
	HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* passes) override;
	HRESULT STDMETHODCALLTYPE GetInfo(DWORD id, void* info, DWORD size) override;
	HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT palette, CONST PALETTEENTRY* entries) override;
	HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT palette, PALETTEENTRY* entries) override;
	HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT palette) override;
	HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* palette) override;
	HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type, UINT start_vertex, UINT primitive_count) override;
	HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT min_index, UINT num_vertices, UINT start_index, UINT primitive_count) override;
	HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitive_count, CONST void* vertices, UINT stride) override;
	HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT min_index, UINT num_vertices, UINT primitive_count, CONST void* indices, D3DFORMAT index_format, CONST void* vertices, UINT stride) override;
	HRESULT STDMETHODCALLTYPE ProcessVertices(UINT src_start_index, UINT dest_index, UINT vertex_count, IDirect3DVertexBuffer8* dest_buffer, DWORD flags) override;
	HRESULT STDMETHODCALLTYPE CreateVertexShader(CONST DWORD* declaration, CONST DWORD* function, DWORD* handle, DWORD usage) override;
	HRESULT STDMETHODCALLTYPE SetVertexShader(DWORD handle) override;
	HRESULT STDMETHODCALLTYPE GetVertexShader(DWORD* handle) override;
	HRESULT STDMETHODCALLTYPE DeleteVertexShader(DWORD handle) override;
	HRESULT STDMETHODCALLTYPE SetVertexShaderConstant(DWORD reg, CONST void* data, DWORD count) override;
	HRESULT STDMETHODCALLTYPE GetVertexShaderConstant(DWORD reg, void* data, DWORD count) override;
	HRESULT STDMETHODCALLTYPE GetVertexShaderDeclaration(DWORD handle, void* data, DWORD* size) override;
	HRESULT STDMETHODCALLTYPE GetVertexShaderFunction(DWORD handle, void* data, DWORD* size) override;
	HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream, IDirect3DVertexBuffer8* buffer, UINT stride) override;
	HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream, IDirect3DVertexBuffer8** buffer, UINT* stride) override;
	HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer8* buffer, UINT base_vertex_index) override;
	HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer8** buffer, UINT* base_vertex_index) override;
	HRESULT STDMETHODCALLTYPE CreatePixelShader(CONST DWORD* function, DWORD* handle) override;
	HRESULT STDMETHODCALLTYPE SetPixelShader(DWORD handle) override;
	HRESULT STDMETHODCALLTYPE GetPixelShader(DWORD* handle) override;
	HRESULT STDMETHODCALLTYPE DeletePixelShader(DWORD handle) override;
	HRESULT STDMETHODCALLTYPE SetPixelShaderConstant(DWORD reg, CONST void* data, DWORD count) override;
	HRESULT STDMETHODCALLTYPE GetPixelShaderConstant(DWORD reg, void* data, DWORD count) override;
	HRESULT STDMETHODCALLTYPE GetPixelShaderFunction(DWORD handle, void* data, DWORD* size) override;
	HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT handle, CONST float* segments, CONST D3DRECTPATCH_INFO* info) override;
	HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT handle, CONST float* segments, CONST D3DTRIPATCH_INFO* info) override;
	HRESULT STDMETHODCALLTYPE DeletePatch(UINT handle) override;

private:
	struct ShaderData
	{
		bool used;
		std::vector<DWORD> declaration;
		std::vector<DWORD> function;
		UINT64 hash;	// of the declaration and the function
	};

	struct UploadAllocation
	{
		unsigned char* cpu;
		D3D12_GPU_VIRTUAL_ADDRESS gpu;
		ID3D12Resource* resource;
		UINT64 offset;
	};

	// Gpu.cpp
	HRESULT Init_Gpu_Objects();
	UploadAllocation Allocate_Upload(UINT64 size, UINT64 alignment);
	UINT Allocate_Srv();
	D3D12_CPU_DESCRIPTOR_HANDLE Srv_Cpu_Handle(UINT index) const;
	D3D12_GPU_DESCRIPTOR_HANDLE Srv_Gpu_Handle(UINT index) const;
	UINT Prepare_Texture(IDirect3DBaseTexture8* texture, TextureType& type);
	D3D12_GPU_VIRTUAL_ADDRESS Prepare_Buffer(GpuBuffer& gpu, const unsigned char* data, UINT size, D3D12_RESOURCE_STATES read_state);
	UINT Get_Sampler(DWORD stage);
	UINT Find_Sampler(const D3D12_SAMPLER_DESC& desc);
	void Release_Finished_Resources();

	// Targets.cpp
	struct TargetState
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtv;
		DXGI_FORMAT rtvFormat;
		D3D12_CPU_DESCRIPTOR_HANDLE dsv;
		DXGI_FORMAT dsvFormat;		// DXGI_FORMAT_UNKNOWN without depth buffer
		UINT sampleCount;
	};
	HRESULT Init_Target_Heaps();
	D3D12_CPU_DESCRIPTOR_HANDLE Target_View_Handle(bool depth, UINT index) const;
	void Transition(GpuTexture& gpu, D3D12_RESOURCE_STATES state);
	bool Create_Gpu_Resource(GpuTexture& gpu, D3DFORMAT format, UINT width, UINT height, UINT levels, UINT faces, DWORD usage, bool sampled);
	bool Ensure_Target(Surface* surface);
	bool Prepare_Targets(TargetState& targets);
	HRESULT Read_Surface(Surface* source, Surface* dest);
	bool Ensure_Readback_Buffer(UINT64 size);

	// Draw.cpp
	bool Begin_Draw();
	// Draws count vertices or indices; first is the first vertex or index, base_vertex is added to indices.
	HRESULT Draw_Geometry(D3DPRIMITIVETYPE type, const D3D12_VERTEX_BUFFER_VIEW& vertices, const D3D12_INDEX_BUFFER_VIEW* indices, UINT count, UINT first, INT base_vertex);
	bool Upload_Vertices(const unsigned char* data, UINT stride, UINT count, D3D12_VERTEX_BUFFER_VIEW& view);
	bool Upload_Indices(D3DPRIMITIVETYPE type, const void* data, bool index32, UINT count, D3D12_INDEX_BUFFER_VIEW& view, UINT& index_count);
	// The shaders of a draw: generated fixed function ones, or translated D3D8 ones.
	struct ShaderChoice
	{
		UINT64 vertexHash;
		UINT64 pixelHash;
		DWORD fvf;								// 0 for translated vertex shaders
		const VertexShaderKey* vertexKey;		// fixed function, or null
		const PixelShaderKey* pixelKey;
		const TranslatedShader* vertexShader;	// translated, or null
		const TranslatedShader* pixelShader;
		bool pointSprite;
	};
	ID3D12PipelineState* Get_Pipeline_State(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology, const ShaderChoice& shaders, const TargetState& targets);
	ID3DBlob* Get_Shader(UINT64 hash, const std::string& source, const char* target);
	const TranslatedShader* Get_Translated_Vertex_Shader(DWORD handle, UINT64& hash);
	const TranslatedShader* Get_Translated_Pixel_Shader(const PixelShaderTranslation& info, const DWORD* function, UINT64& hash);
	void Fill_Vertex_Constants(FFVertexConstants& constants);
	void Fill_Pixel_Constants(FFPixelConstants& constants);
	void Fill_Shader_Vertex_Constants(ShaderVertexConstants& constants);

	HRESULT Create_Swap_Chain_Buffers();
	void Release_Swap_Chain_Buffers();
	HRESULT Apply_Window_Mode();
	bool Is_Fullscreen_Lost();
	void Hook_Window();
	void Unhook_Window();
	static LRESULT CALLBACK Window_Proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
	void Set_Default_State();
	void Begin_Frame();
	void Release_Held_Objects();
	void Resolve_Back_Buffer();
	void Flush_Commands();
	void Wait_For_GPU();
	void Log_Debug_Messages();
	HRESULT Read_Back_Buffer(Surface* dest);
	void Replace_Surface(Surface*& slot, Surface* surface);

	Direct3D* m_d3d;
	AdapterInfo m_adapter;
	D3DDEVICE_CREATION_PARAMETERS m_creationParams;
	D3DPRESENT_PARAMETERS m_presentParams;
	HWND m_window;
	WNDPROC m_windowProc;	// the window's own procedure while the device hooks it, else null

	ComPtr<ID3D12Device> m_device;
	ComPtr<ID3D12InfoQueue> m_infoQueue;	// only with the debug layer
	ComPtr<ID3D12CommandQueue> m_queue;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
	UINT m_rtvDescriptorSize;
	ComPtr<ID3D12Resource> m_backBuffers[FRAME_COUNT];
	ComPtr<ID3D12Resource> m_depthBuffer;
	// With multisampling the back buffer is drawn here and resolved into the swap chain buffer, as flip
	// model swap chains can not be multisampled.
	ComPtr<ID3D12Resource> m_multisampleTarget;
	UINT m_sampleCount;
	LONG m_liveChildren;
	ComPtr<ID3D12CommandAllocator> m_allocators[FRAME_COUNT];
	ComPtr<ID3D12GraphicsCommandList> m_commandList;
	ComPtr<ID3D12Resource> m_readbackBuffer;

	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_srvHeap;		// shader visible
	ComPtr<ID3D12DescriptorHeap> m_samplerHeap;	// shader visible
	UINT m_srvDescriptorSize;
	UINT m_samplerDescriptorSize;
	std::vector<UINT> m_freeSrvs;
	UINT m_nextSrv;
	std::unordered_map<UINT64, UINT> m_samplers;
	bool m_heapsBound;

	ComPtr<ID3D12Resource> m_uploadBuffers[FRAME_COUNT];
	unsigned char* m_uploadCpu[FRAME_COUNT];
	UINT64 m_uploadOffset;
	// Released resources and descriptors, with the fence value after which the GPU no longer uses them.
	struct Garbage
	{
		UINT64 fence;
		ComPtr<ID3D12Resource> resource;
		UINT srv;
		UINT64 poolKey;		// non-zero for a texture that can be used again, see m_texturePool
		D3D12_RESOURCE_STATES state;
	};
	std::vector<Garbage> m_garbage;

	// Textures the engine released, kept for a while to be used again for a texture of the same kind:
	// text is drawn with a new texture whenever it changes, and creating resources is slow.
	struct PooledTexture
	{
		UINT64 key;
		UINT64 fence;	// when it was released
		ComPtr<ID3D12Resource> resource;
		UINT srv;
		D3D12_RESOURCE_STATES state;
	};
	std::vector<PooledTexture> m_texturePool;
	std::vector<Surface*> m_textureSubresources;	// Prepare_Texture's list, kept to not allocate per draw

	ComPtr<ID3D12DescriptorHeap> m_targetRtvHeap;	// views of render target textures and surfaces
	ComPtr<ID3D12DescriptorHeap> m_targetDsvHeap;

	// Depth buffers the device draws with instead of a bound one that is larger than the render target
	// or has another sample count, which D3D8 allows and Direct3D 12 does not. One per size.
	struct SubstituteDepth
	{
		UINT width;
		UINT height;
		UINT samples;
		DXGI_FORMAT format;
		ComPtr<ID3D12Resource> resource;
		UINT view;
	};
	std::vector<SubstituteDepth> m_substituteDepths;
	bool Use_Substitute_Depth(TargetState& targets, UINT width, UINT height, DXGI_FORMAT format);
	UINT m_dsvDescriptorSize;
	UINT m_nextTargetRtv;
	UINT m_nextTargetDsv;
	std::vector<UINT> m_freeTargetRtvs;
	std::vector<UINT> m_freeTargetDsvs;

	std::unordered_map<UINT64, ComPtr<ID3DBlob>> m_shaders;
	std::unordered_map<UINT64, TranslatedShader> m_translatedShaders;
	std::unordered_map<UINT64, ComPtr<ID3D12PipelineState>> m_pipelines;
	ComPtr<ID3D12Fence> m_fence;
	HANDLE m_fenceEvent;
	UINT64 m_frameFenceValues[FRAME_COUNT];
	UINT64 m_nextFenceValue;
	UINT m_backBufferIndex;
	bool m_recording;
	bool m_deviceRemoved;

	Surface* m_backBufferSurface;
	Surface* m_autoDepthSurface;
	Surface* m_renderTarget;
	Surface* m_depthStencil;

	StateValues m_state;
	IDirect3DBaseTexture8* m_textures[MAX_TEXTURE_STAGES];
	StreamBinding m_streams[MAX_STREAMS];
	IDirect3DIndexBuffer8* m_indices;
	UINT m_baseVertexIndex;

	std::vector<ShaderData> m_vertexShaders;
	std::vector<ShaderData> m_pixelShaders;
	std::vector<StateValues*> m_stateBlocks;
	StateValues* m_recordingStateBlock;

	HCURSOR m_cursor;
	bool m_cursorVisible;
	D3DGAMMARAMP m_gammaRamp;
	PALETTEENTRY m_palettes[256][256];
	UINT m_currentPalette;

	PostProcessor* m_postProcessor;	// created when the engine asks for it
	std::vector<ComPtr<ID3D12PipelineState>> m_retiredPipelines;	// of released shaders, kept until the GPU is done
};

} // namespace D3D8On12
