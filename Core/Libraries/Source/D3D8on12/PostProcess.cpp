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

// PostProcess.cpp
// Full screen pixel shader passes over the 3D scene, drawn with the device's root signature.

#include "PostProcess.h"
#include "Device.h"

#include <d3dcompiler.h>
#include <d3d12shader.h>

namespace D3D8On12
{

static const UINT ROOT_VERTEX_CONSTANTS = 0;
static const UINT ROOT_PIXEL_CONSTANTS = 1;
static const UINT ROOT_TEXTURES = 2;
static const UINT ROOT_SAMPLERS = ROOT_TEXTURES + FF_STAGES;
static const UINT NULL_SRV_2D = 0;
static const UINT INPUT_COUNT = 4;

// {5A58797D-A72C-478D-8BA2-EFC6B0EFE88E}, defined here instead of linking dxguid, which also defines the D3D8 IDs.
static const IID IID_ShaderReflection = { 0x5a58797d, 0xa72c, 0x478d, { 0x8b, 0xa2, 0xef, 0xc6, 0xb0, 0xef, 0xe8, 0x8e } };

// What every pass shader includes. It describes how Run binds the inputs and constants.
static const char s_header[] =
	"cbuffer PostProcessCommon : register(b0)\n"
	"{\n"
	"	float4 ScreenSize;		// width, height, 1 / width, 1 / height\n"
	"	float4 OutputSize;		// the same for the target this pass draws into\n"
	"	float4 InputSize[4];	// the same for Input0..Input3\n"
	"	float Time;				// seconds\n"
	"	float3 PostProcessPad;\n"
	"	row_major float4x4 View;			// world to view, as given to Set_Camera\n"
	"	row_major float4x4 InverseView;\n"
	"	row_major float4x4 Projection;		// view to clip\n"
	"	row_major float4x4 InverseProjection;\n"
	"};\n"
	"Texture2D Input0 : register(t0);\n"
	"Texture2D Input1 : register(t1);\n"
	"Texture2D Input2 : register(t2);\n"
	"Texture2D Input3 : register(t3);\n"
	"SamplerState LinearClamp : register(s0);\n"
	"SamplerState PointClamp : register(s1);\n"
	"struct PostProcessInput\n"
	"{\n"
	"	float4 position : SV_Position;\n"
	"	float2 uv : TEXCOORD0;\n"
	"};\n"
	"float Luminance(float3 color) { return dot(color, float3(0.299, 0.587, 0.114)); }\n"
	"// Position in view space of the pixel at uv with this depth (from a Depth input).\n"
	"float3 ViewPosition(float2 uv, float depth)\n"
	"{\n"
	"	float4 position = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), InverseProjection);\n"
	"	return position.xyz / position.w;\n"
	"}\n"
	"float3 WorldPosition(float3 viewPosition) { return mul(float4(viewPosition, 1), InverseView).xyz; }\n"
	"// Distance from the camera plane in world units.\n"
	"float LinearDepth(float depth) { return abs(ViewPosition(float2(0.5, 0.5), depth).z); }\n";

// Copies the depth buffer into a float texture without multisampling (sample 0 of each pixel).
static const char s_depthCopyShader[] =
	"Texture2D<float> Depth : register(t0);\n"
	"float main(float4 position : SV_Position) : SV_Target { return Depth.Load(int3(position.xy, 0)); }\n";
static const char s_depthCopyShaderMS[] =
	"Texture2DMS<float> Depth : register(t0);\n"
	"float main(float4 position : SV_Position) : SV_Target { return Depth.Load(int2(position.xy), 0); }\n";

// One triangle that covers the screen, with texture coordinates 0..1 over it.
static const char s_vertexShader[] =
	"struct PostProcessInput { float4 position : SV_Position; float2 uv : TEXCOORD0; };\n"
	"PostProcessInput main(uint id : SV_VertexID)\n"
	"{\n"
	"	PostProcessInput output;\n"
	"	output.uv = float2((id << 1) & 2, id & 2);\n"
	"	output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);\n"
	"	return output;\n"
	"}\n";

struct CommonConstants
{
	float screenSize[4];
	float outputSize[4];
	float inputSize[INPUT_COUNT][4];
	float time;
	float pad[3];
	D3DMATRIX view;
	D3DMATRIX inverseView;
	D3DMATRIX projection;
	D3DMATRIX inverseProjection;
};

// Inverse of a 4x4 matrix by Gauss-Jordan elimination; the identity when it is singular.
static D3DMATRIX Invert(const D3DMATRIX& matrix)
{
	float a[4][8];
	for (int row = 0; row < 4; ++row)
		for (int column = 0; column < 4; ++column)
		{
			a[row][column] = matrix.m[row][column];
			a[row][column + 4] = row == column ? 1.0f : 0.0f;
		}
	for (int column = 0; column < 4; ++column)
	{
		int pivot = column;
		for (int row = column + 1; row < 4; ++row)
			if (fabsf(a[row][column]) > fabsf(a[pivot][column]))
				pivot = row;
		D3DMATRIX identity = {};
		identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
		if (fabsf(a[pivot][column]) < 1e-12f)
			return identity;
		if (pivot != column)
			for (int i = 0; i < 8; ++i)
			{
				const float swap = a[column][i];
				a[column][i] = a[pivot][i];
				a[pivot][i] = swap;
			}
		const float scale = 1.0f / a[column][column];
		for (int i = 0; i < 8; ++i)
			a[column][i] *= scale;
		for (int row = 0; row < 4; ++row)
		{
			if (row == column)
				continue;
			const float factor = a[row][column];
			for (int i = 0; i < 8; ++i)
				a[row][i] -= factor * a[column][i];
		}
	}
	D3DMATRIX inverse;
	for (int row = 0; row < 4; ++row)
		for (int column = 0; column < 4; ++column)
			inverse.m[row][column] = a[row][column + 4];
	return inverse;
}

static void Set_Size(float* size, UINT width, UINT height)
{
	size[0] = (float)width;
	size[1] = (float)height;
	size[2] = width != 0 ? 1.0f / width : 0.0f;
	size[3] = height != 0 ? 1.0f / height : 0.0f;
}

// Resolves "PostProcess.hlsli" itself and leaves other files to the engine.
class IncludeHandler : public ID3DInclude
{
public:
	explicit IncludeHandler(ID3DInclude* engine) : m_engine(engine) {}

	HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override
	{
		if (_stricmp(name, "PostProcess.hlsli") == 0)
		{
			*data = s_header;
			*bytes = sizeof(s_header) - 1;
			return S_OK;
		}
		return m_engine != nullptr ? m_engine->Open(type, name, parent, data, bytes) : E_FAIL;
	}

	HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override
	{
		if (data == s_header)
			return S_OK;
		return m_engine != nullptr ? m_engine->Close(data) : S_OK;
	}

private:
	ID3DInclude* m_engine;
};

PostProcessor::PostProcessor(Device* device)
	: m_device(device),
		m_linearSampler(0),
		m_pointSampler(0)
{
	m_scene.rtv = 0;
	m_scene.width = 0;
	m_scene.height = 0;
	m_scene.format = DXGI_FORMAT_UNKNOWN;
	m_depth = m_scene;
	m_depthShaders[0] = m_depthShaders[1] = 0;
	memset(&m_view, 0, sizeof(m_view));
	m_view._11 = m_view._22 = m_view._33 = m_view._44 = 1.0f;
	m_projection = m_view;
}

void STDMETHODCALLTYPE PostProcessor::Set_Camera(const D3DMATRIX* view, const D3DMATRIX* projection)
{
	if (view != nullptr)
		m_view = *view;
	if (projection != nullptr)
		m_projection = *projection;
}

// Copies the depth buffer into m_depth, which passes then read as their Depth input.
bool PostProcessor::Copy_Depth(UINT width, UINT height)
{
	Device& device = *m_device;
	if (device.m_depthBuffer == nullptr || device.m_depthSrv == 0)
		return false;
	const bool multisampled = device.m_sampleCount > 1;
	UINT& shader = m_depthShaders[multisampled ? 1 : 0];
	if (shader == 0)
	{
		const char* source = multisampled ? s_depthCopyShaderMS : s_depthCopyShader;
		ComPtr<ID3DBlob> messages;
		if (FAILED(Create_Shader(source, (UINT)strlen(source), "PostProcessDepth", nullptr, &shader, &messages)))
		{
			Log("could not compile the depth copy shader: %s", messages != nullptr ? (const char*)messages->GetBufferPointer() : "");
			return false;
		}
	}
	if (!Ensure_Target(m_depth, width, height, DXGI_FORMAT_R32_FLOAT))
		return false;
	ID3D12PipelineState* pipeline = Get_Pipeline(m_shaders[shader - 1], DXGI_FORMAT_R32_FLOAT, 1);
	if (pipeline == nullptr)
		return false;

	ID3D12GraphicsCommandList* list = device.m_commandList.Get();
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = device.m_depthBuffer.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	list->ResourceBarrier(1, &barrier);
	device.Transition(m_depth.gpu, D3D12_RESOURCE_STATE_RENDER_TARGET);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = device.Target_View_Handle(false, m_depth.rtv);
	list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f };
	D3D12_RECT scissor = { 0, 0, (LONG)width, (LONG)height };
	list->RSSetViewports(1, &viewport);
	list->RSSetScissorRects(1, &scissor);
	list->SetPipelineState(pipeline);
	list->SetGraphicsRootDescriptorTable(ROOT_TEXTURES, device.Srv_Gpu_Handle(device.m_depthSrv));
	list->DrawInstanced(3, 1, 0, 0);

	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	list->ResourceBarrier(1, &barrier);
	device.Transition(m_depth.gpu, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	return true;
}

// The device is idle when it deletes the post-processor, so the resources can go right away.
PostProcessor::~PostProcessor()
{
	for (Pass& pass : m_passes)
		for (IDirect3DBaseTexture8* texture : pass.desc.textures)
			if (texture != nullptr)
				texture->Release();
}

HRESULT STDMETHODCALLTYPE PostProcessor::QueryInterface(REFIID riid, void** object)
{
	if (object == nullptr)
		return E_POINTER;
	if (riid == IID_ID3D8On12PostProcess)
	{
		AddRef();
		*object = static_cast<ID3D8On12PostProcess*>(this);
		return S_OK;
	}
	return m_device->QueryInterface(riid, object);
}

ULONG STDMETHODCALLTYPE PostProcessor::AddRef()
{
	return m_device->AddRef();
}

ULONG STDMETHODCALLTYPE PostProcessor::Release()
{
	return m_device->Release();
}

HRESULT STDMETHODCALLTYPE PostProcessor::Create_Shader(const char* source, UINT length, const char* name, ID3DInclude* include,
	UINT* shader, ID3DBlob** errors)
{
	if (source == nullptr || shader == nullptr)
		return D3DERR_INVALIDCALL;
	*shader = 0;
	if (errors != nullptr)
		*errors = nullptr;

	IncludeHandler handler(include);
	Shader compiled;
	compiled.used = true;
	compiled.paramsSize = 0;
	ComPtr<ID3DBlob> messages;
	const HRESULT hr = D3DCompile(source, length, name, nullptr, &handler, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
		&compiled.code, &messages);
	if (FAILED(hr))
	{
		if (errors != nullptr)
			*errors = messages.Detach();
		return hr;
	}

	// Parameters are set by name, at the offsets the compiler gave the Params variables.
	ComPtr<ID3D12ShaderReflection> reflection;
	if (SUCCEEDED(D3DReflect(compiled.code->GetBufferPointer(), compiled.code->GetBufferSize(), IID_ShaderReflection, (void**)reflection.GetAddressOf())))
	{
		ID3D12ShaderReflectionConstantBuffer* params = reflection->GetConstantBufferByName("Params");
		D3D12_SHADER_BUFFER_DESC buffer_desc;
		if (params != nullptr && SUCCEEDED(params->GetDesc(&buffer_desc)))
		{
			compiled.paramsSize = buffer_desc.Size;
			for (UINT i = 0; i < buffer_desc.Variables; ++i)
			{
				D3D12_SHADER_VARIABLE_DESC variable_desc;
				if (SUCCEEDED(params->GetVariableByIndex(i)->GetDesc(&variable_desc)))
				{
					Variable variable = { variable_desc.StartOffset, variable_desc.Size };
					compiled.variables[variable_desc.Name] = variable;
				}
			}
		}
	}

	for (size_t i = 0; i < m_shaders.size(); ++i)
	{
		if (!m_shaders[i].used)
		{
			m_shaders[i] = compiled;
			*shader = (UINT)i + 1;
			return D3D_OK;
		}
	}
	m_shaders.push_back(compiled);
	*shader = (UINT)m_shaders.size();
	return D3D_OK;
}

void STDMETHODCALLTYPE PostProcessor::Release_Shader(UINT shader)
{
	if (shader == 0 || shader > m_shaders.size())
		return;
	Shader& released = m_shaders[shader - 1];
	// The GPU may still draw with its pipelines; the device keeps them until it goes.
	for (auto& pipeline : released.pipelines)
		m_device->m_retiredPipelines.push_back(pipeline.second);
	released = Shader();
	released.used = false;
	released.paramsSize = 0;
}

HRESULT STDMETHODCALLTYPE PostProcessor::Set_Chain(const D3D8ON12_PP_PASS* passes, UINT count)
{
	for (UINT i = 0; i < count; ++i)
	{
		const UINT shader = passes[i].shader;
		if (shader == 0 || shader > m_shaders.size() || !m_shaders[shader - 1].used)
			return D3DERR_INVALIDCALL;
		for (UINT input = 0; input < INPUT_COUNT; ++input)
		{
			const UINT source = passes[i].inputs[input];
			if (source >= D3D8ON12_PP_INPUT_PASS && source - D3D8ON12_PP_INPUT_PASS >= i)
				return D3DERR_INVALIDCALL;	// only earlier passes have an output
		}
	}

	Clear_Chain();
	m_passes.resize(count);
	for (UINT i = 0; i < count; ++i)
	{
		Pass& pass = m_passes[i];
		pass.desc = passes[i];
		for (IDirect3DBaseTexture8* texture : pass.desc.textures)
			if (texture != nullptr)
				texture->AddRef();
		pass.params.assign(m_shaders[pass.desc.shader - 1].paramsSize, 0);
		pass.output.rtv = 0;
		pass.output.width = 0;
		pass.output.height = 0;
		pass.output.format = DXGI_FORMAT_UNKNOWN;
	}
	return D3D_OK;
}

void PostProcessor::Clear_Chain()
{
	for (Pass& pass : m_passes)
	{
		for (IDirect3DBaseTexture8*& texture : pass.desc.textures)
		{
			if (texture != nullptr)
				texture->Release();
			texture = nullptr;
		}
		Release_Target(pass.output);
	}
	m_passes.clear();
}

HRESULT STDMETHODCALLTYPE PostProcessor::Set_Parameter(UINT pass, const char* name, const float* values, UINT count)
{
	if (pass >= m_passes.size() || name == nullptr || values == nullptr)
		return D3DERR_INVALIDCALL;
	const Shader& shader = m_shaders[m_passes[pass].desc.shader - 1];
	auto found = shader.variables.find(name);
	if (found == shader.variables.end())
		return E_INVALIDARG;
	const UINT size = min(count * (UINT)sizeof(float), found->second.size);
	memcpy(m_passes[pass].params.data() + found->second.offset, values, size);
	return D3D_OK;
}

ID3D12PipelineState* PostProcessor::Get_Pipeline(Shader& shader, DXGI_FORMAT format, UINT samples)
{
	const UINT64 key = ((UINT64)format << 8) | samples;
	auto found = shader.pipelines.find(key);
	if (found != shader.pipelines.end())
		return found->second.Get();

	if (m_vertexShader == nullptr)
	{
		ComPtr<ID3DBlob> messages;
		if (FAILED(D3DCompile(s_vertexShader, sizeof(s_vertexShader) - 1, "PostProcessVS", nullptr, nullptr, "main", "vs_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &m_vertexShader, &messages)))
		{
			Log("could not compile the post-processing vertex shader: %s", messages != nullptr ? (const char*)messages->GetBufferPointer() : "");
			return nullptr;
		}
	}

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_device->m_rootSignature.Get();
	desc.VS.pShaderBytecode = m_vertexShader->GetBufferPointer();
	desc.VS.BytecodeLength = m_vertexShader->GetBufferSize();
	desc.PS.pShaderBytecode = shader.code->GetBufferPointer();
	desc.PS.BytecodeLength = shader.code->GetBufferSize();
	desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	desc.SampleMask = UINT_MAX;
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	desc.RasterizerState.DepthClipEnable = TRUE;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = format;
	desc.SampleDesc.Count = samples;

	ComPtr<ID3D12PipelineState> pipeline;
	if (FAILED(m_device->m_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))))
	{
		Log("could not create a post-processing pipeline");
		return nullptr;
	}
	shader.pipelines[key] = pipeline;
	return pipeline.Get();
}

bool PostProcessor::Ensure_Target(Target& target, UINT width, UINT height, DXGI_FORMAT format)
{
	if (target.gpu.resource != nullptr && target.width == width && target.height == height && target.format == format)
		return true;
	Release_Target(target);

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
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	if (FAILED(m_device->m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr, IID_PPV_ARGS(&target.gpu.resource))))
	{
		Log("could not create a %ux%u post-processing target", width, height);
		return false;
	}
	target.gpu.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	target.gpu.srv = m_device->Allocate_Srv();
	m_device->m_device->CreateShaderResourceView(target.gpu.resource.Get(), nullptr, m_device->Srv_Cpu_Handle(target.gpu.srv));
	target.rtv = m_device->Allocate_Target_View(false);
	if (target.rtv != 0)
		m_device->m_device->CreateRenderTargetView(target.gpu.resource.Get(), nullptr, m_device->Target_View_Handle(false, target.rtv));
	target.width = width;
	target.height = height;
	target.format = format;
	return target.rtv != 0;
}

void PostProcessor::Release_Target(Target& target)
{
	if (target.gpu.resource != nullptr || target.gpu.srv != 0)
		m_device->Release_Gpu_Texture(target.gpu);
	m_device->Free_Target_View(false, target.rtv);
	target.rtv = 0;
	target.width = 0;
	target.height = 0;
	target.format = DXGI_FORMAT_UNKNOWN;
}

// Copies what is drawn into the back buffer so far into a texture the passes can read.
bool PostProcessor::Copy_Scene(UINT width, UINT height)
{
	if (!Ensure_Target(m_scene, width, height, DXGI_FORMAT_B8G8R8A8_UNORM))
		return false;

	Device& device = *m_device;
	ID3D12GraphicsCommandList* list = device.m_commandList.Get();
	const bool multisampled = device.m_multisampleTarget != nullptr;
	ID3D12Resource* source = multisampled ? device.m_multisampleTarget.Get() : device.m_backBuffers[device.m_backBufferIndex].Get();

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = source;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = multisampled ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE : D3D12_RESOURCE_STATE_COPY_SOURCE;
	list->ResourceBarrier(1, &barrier);
	device.Transition(m_scene.gpu, multisampled ? D3D12_RESOURCE_STATE_RESOLVE_DEST : D3D12_RESOURCE_STATE_COPY_DEST);

	if (multisampled)
		list->ResolveSubresource(m_scene.gpu.resource.Get(), 0, source, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
	else
		list->CopyResource(m_scene.gpu.resource.Get(), source);

	barrier.Transition.StateBefore = barrier.Transition.StateAfter;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	list->ResourceBarrier(1, &barrier);
	device.Transition(m_scene.gpu, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	return true;
}

HRESULT STDMETHODCALLTYPE PostProcessor::Run(float time)
{
	if (m_passes.empty())
		return D3D_OK;
	Device& device = *m_device;
	if (device.m_renderTarget != device.m_backBufferSurface || device.m_backBuffers[0] == nullptr)
		return D3DERR_INVALIDCALL;

	device.Begin_Frame();
	const UINT width = device.m_presentParams.BackBufferWidth;
	const UINT height = device.m_presentParams.BackBufferHeight;
	if (!Copy_Scene(width, height))
		return E_FAIL;

	if (m_linearSampler == 0)
	{
		D3D12_SAMPLER_DESC sampler = {};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
		m_linearSampler = device.Find_Sampler(sampler);
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		m_pointSampler = device.Find_Sampler(sampler);
	}

	ID3D12GraphicsCommandList* list = device.m_commandList.Get();
	if (!device.m_heapsBound)
	{
		ID3D12DescriptorHeap* heaps[] = { device.m_srvHeap.Get(), device.m_samplerHeap.Get() };
		list->SetDescriptorHeaps(2, heaps);
		device.m_heapsBound = true;
	}
	list->SetGraphicsRootSignature(device.m_rootSignature.Get());
	list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	bool uses_depth = false;
	for (const Pass& pass : m_passes)
		for (UINT input = 0; input < INPUT_COUNT; ++input)
			uses_depth = uses_depth || pass.desc.inputs[input] == D3D8ON12_PP_INPUT_DEPTH;
	if (uses_depth)
	{
		// The copy pass leaves the other root parameters to the passes, which set all of them.
		Device::UploadAllocation dummy = device.Allocate_Upload(sizeof(CommonConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		if (dummy.cpu == nullptr)
			return E_FAIL;
		list->SetGraphicsRootConstantBufferView(ROOT_VERTEX_CONSTANTS, dummy.gpu);
		list->SetGraphicsRootConstantBufferView(ROOT_PIXEL_CONSTANTS, dummy.gpu);
		for (UINT slot = 0; slot < FF_STAGES; ++slot)
		{
			list->SetGraphicsRootDescriptorTable(ROOT_TEXTURES + slot, device.Srv_Gpu_Handle(NULL_SRV_2D));
			list->SetGraphicsRootDescriptorTable(ROOT_SAMPLERS + slot, device.m_samplerHeap->GetGPUDescriptorHandleForHeapStart());
		}
		if (!Copy_Depth(width, height))
			uses_depth = false;
	}

	for (size_t index = 0; index < m_passes.size(); ++index)
	{
		Pass& pass = m_passes[index];
		Shader& shader = m_shaders[pass.desc.shader - 1];
		const bool last = index + 1 == m_passes.size();

		// The output: the back buffer for the last pass, else a texture of the pass.
		D3D12_CPU_DESCRIPTOR_HANDLE rtv;
		DXGI_FORMAT format;
		UINT samples = 1;
		UINT output_width = width;
		UINT output_height = height;
		if (last)
		{
			rtv = device.m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
			rtv.ptr += (device.m_multisampleTarget != nullptr ? FRAME_COUNT : device.m_backBufferIndex) * device.m_rtvDescriptorSize;
			format = DXGI_FORMAT_B8G8R8A8_UNORM;
			samples = device.m_sampleCount;
		}
		else
		{
			const UINT scale = pass.desc.scale == 0 ? 1 : min(pass.desc.scale, 8u);
			output_width = max(width / scale, 1u);
			output_height = max(height / scale, 1u);
			format = pass.desc.hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
			if (!Ensure_Target(pass.output, output_width, output_height, format))
				return E_FAIL;
			device.Transition(pass.output.gpu, D3D12_RESOURCE_STATE_RENDER_TARGET);
			rtv = device.Target_View_Handle(false, pass.output.rtv);
		}

		ID3D12PipelineState* pipeline = Get_Pipeline(shader, format, samples);
		if (pipeline == nullptr)
			return E_FAIL;

		Device::UploadAllocation common = device.Allocate_Upload(sizeof(CommonConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		Device::UploadAllocation params = device.Allocate_Upload(max((UINT)pass.params.size(), 16u), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
		if (common.cpu == nullptr || params.cpu == nullptr)
			return E_FAIL;
		CommonConstants& constants = *(CommonConstants*)common.cpu;
		memset(&constants, 0, sizeof(constants));
		Set_Size(constants.screenSize, width, height);
		Set_Size(constants.outputSize, output_width, output_height);
		constants.time = time;
		constants.view = m_view;
		constants.inverseView = Invert(m_view);
		constants.projection = m_projection;
		constants.inverseProjection = Invert(m_projection);
		memset(params.cpu, 0, max((UINT)pass.params.size(), 16u));
		if (!pass.params.empty())
			memcpy(params.cpu, pass.params.data(), pass.params.size());

		UINT srvs[INPUT_COUNT] = {};
		for (UINT input = 0; input < INPUT_COUNT; ++input)
		{
			const UINT source = pass.desc.inputs[input];
			if (source == D3D8ON12_PP_INPUT_SCENE)
			{
				srvs[input] = m_scene.gpu.srv;
				Set_Size(constants.inputSize[input], width, height);
			}
			else if (source == D3D8ON12_PP_INPUT_DEPTH)
			{
				if (uses_depth)
				{
					srvs[input] = m_depth.gpu.srv;
					Set_Size(constants.inputSize[input], width, height);
				}
			}
			else if (source == D3D8ON12_PP_INPUT_TEXTURE && pass.desc.textures[input] != nullptr)
			{
				TextureType type;
				srvs[input] = device.Prepare_Texture(pass.desc.textures[input], type);
				if (pass.desc.textures[input]->GetType() == D3DRTYPE_TEXTURE)
				{
					D3DSURFACE_DESC level;
					static_cast<IDirect3DTexture8*>(pass.desc.textures[input])->GetLevelDesc(0, &level);
					Set_Size(constants.inputSize[input], level.Width, level.Height);
				}
			}
			else if (source >= D3D8ON12_PP_INPUT_PASS)
			{
				const Target& earlier = m_passes[source - D3D8ON12_PP_INPUT_PASS].output;
				srvs[input] = earlier.gpu.srv;
				Set_Size(constants.inputSize[input], earlier.width, earlier.height);
			}
		}

		list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)output_width, (float)output_height, 0.0f, 1.0f };
		D3D12_RECT scissor = { 0, 0, (LONG)output_width, (LONG)output_height };
		list->RSSetViewports(1, &viewport);
		list->RSSetScissorRects(1, &scissor);
		list->SetPipelineState(pipeline);
		list->SetGraphicsRootConstantBufferView(ROOT_VERTEX_CONSTANTS, common.gpu);
		list->SetGraphicsRootConstantBufferView(ROOT_PIXEL_CONSTANTS, params.gpu);
		for (UINT slot = 0; slot < FF_STAGES; ++slot)
		{
			// Every table of the root signature must be set, also those the shader does not read.
			list->SetGraphicsRootDescriptorTable(ROOT_TEXTURES + slot, device.Srv_Gpu_Handle(slot < INPUT_COUNT ? srvs[slot] : NULL_SRV_2D));
			const UINT sampler = slot == 0 ? m_linearSampler : slot == 1 ? m_pointSampler : 0;
			D3D12_GPU_DESCRIPTOR_HANDLE sampler_handle = device.m_samplerHeap->GetGPUDescriptorHandleForHeapStart();
			sampler_handle.ptr += (UINT64)sampler * device.m_samplerDescriptorSize;
			list->SetGraphicsRootDescriptorTable(ROOT_SAMPLERS + slot, sampler_handle);
		}
		list->DrawInstanced(3, 1, 0, 0);

		if (!last)
			device.Transition(pass.output.gpu, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	return D3D_OK;
}

} // namespace D3D8On12
