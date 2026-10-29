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

// Draw.cpp
// Drawing with the fixed function pipeline emulated by generated shaders.
//
// Vertex and index buffers that are not dynamic have a Direct3D 12 copy (Gpu.cpp). Draws from dynamic
// buffers copy the vertices and indices they use into upload memory, so every lock of those behaves
// like D3DLOCK_NOOVERWRITE.

#include "Device.h"
#include "Formats.h"
#include "Resources.h"

#include <d3dcompiler.h>
#include <math.h>
#include <string.h>

namespace D3D8On12
{

static const UINT ROOT_VERTEX_CONSTANTS = 0;
static const UINT ROOT_PIXEL_CONSTANTS = 1;
static const UINT ROOT_TEXTURES = 2;
static const UINT ROOT_SAMPLERS = ROOT_TEXTURES + FF_STAGES;

static UINT Get_Vertex_Count(D3DPRIMITIVETYPE type, UINT primitive_count)
{
	switch (type)
	{
	case D3DPT_POINTLIST:		return primitive_count;
	case D3DPT_LINELIST:		return primitive_count * 2;
	case D3DPT_LINESTRIP:		return primitive_count + 1;
	case D3DPT_TRIANGLELIST:	return primitive_count * 3;
	case D3DPT_TRIANGLESTRIP:	return primitive_count + 2;
	case D3DPT_TRIANGLEFAN:		return primitive_count + 2;
	}
	return 0;
}

static D3D12_PRIMITIVE_TOPOLOGY Get_Topology(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST:		return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	case D3DPT_LINELIST:		return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
	case D3DPT_LINESTRIP:		return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
	case D3DPT_TRIANGLESTRIP:	return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
	default:					return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;	// fans are drawn as lists
	}
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE Get_Topology_Type(D3DPRIMITIVETYPE type)
{
	switch (type)
	{
	case D3DPT_POINTLIST:		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	case D3DPT_LINELIST:
	case D3DPT_LINESTRIP:		return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	default:					return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	}
}

static void To_Float4(D3DCOLOR color, float* out)
{
	out[0] = ((color >> 16) & 0xFF) / 255.0f;
	out[1] = ((color >> 8) & 0xFF) / 255.0f;
	out[2] = (color & 0xFF) / 255.0f;
	out[3] = ((color >> 24) & 0xFF) / 255.0f;
}

static void To_Float4(const D3DCOLORVALUE& color, float* out)
{
	out[0] = color.r;
	out[1] = color.g;
	out[2] = color.b;
	out[3] = color.a;
}

static float As_Float(DWORD bits)
{
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void Multiply(const D3DMATRIX& a, const D3DMATRIX& b, D3DMATRIX& result)
{
	for (int row = 0; row < 4; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			result.m[row][col] = a.m[row][0] * b.m[0][col] + a.m[row][1] * b.m[1][col]
				+ a.m[row][2] * b.m[2][col] + a.m[row][3] * b.m[3][col];
		}
	}
}

static D3DVECTOR Transform_Point(const D3DVECTOR& v, const D3DMATRIX& m)
{
	D3DVECTOR r;
	r.x = v.x * m._11 + v.y * m._21 + v.z * m._31 + m._41;
	r.y = v.x * m._12 + v.y * m._22 + v.z * m._32 + m._42;
	r.z = v.x * m._13 + v.y * m._23 + v.z * m._33 + m._43;
	return r;
}

static D3DVECTOR Transform_Direction(const D3DVECTOR& v, const D3DMATRIX& m)
{
	D3DVECTOR r;
	r.x = v.x * m._11 + v.y * m._21 + v.z * m._31;
	r.y = v.x * m._12 + v.y * m._22 + v.z * m._32;
	r.z = v.x * m._13 + v.y * m._23 + v.z * m._33;
	const float length = sqrtf(r.x * r.x + r.y * r.y + r.z * r.z);
	if (length > 0)
	{
		r.x /= length;
		r.y /= length;
		r.z /= length;
	}
	return r;
}

// ----------------------------------------------------------------------------
// Geometry
// ----------------------------------------------------------------------------

bool Device::Upload_Vertices(const unsigned char* data, UINT stride, UINT count, D3D12_VERTEX_BUFFER_VIEW& view)
{
	const UINT size = stride * count;
	UploadAllocation upload = Allocate_Upload(size, 16);
	if (upload.cpu == nullptr)
		return false;
	memcpy(upload.cpu, data, size);
	view.BufferLocation = upload.gpu;
	view.SizeInBytes = size;
	view.StrideInBytes = stride;
	return true;
}

// Copies indices, turning triangle fans into lists.
bool Device::Upload_Indices(D3DPRIMITIVETYPE type, const void* data, bool index32, UINT count, D3D12_INDEX_BUFFER_VIEW& view, UINT& index_count)
{
	const UINT index_size = index32 ? 4 : 2;
	index_count = type == D3DPT_TRIANGLEFAN ? (count - 2) * 3 : count;
	UploadAllocation upload = Allocate_Upload((UINT64)index_count * index_size, 16);
	if (upload.cpu == nullptr)
		return false;

	if (type != D3DPT_TRIANGLEFAN)
	{
		memcpy(upload.cpu, data, (size_t)count * index_size);
	}
	else if (index32)
	{
		const DWORD* source = (const DWORD*)data;
		DWORD* dest = (DWORD*)upload.cpu;
		for (UINT i = 1; i + 1 < count; ++i)
		{
			*dest++ = source[0];
			*dest++ = source[i];
			*dest++ = source[i + 1];
		}
	}
	else
	{
		const WORD* source = (const WORD*)data;
		WORD* dest = (WORD*)upload.cpu;
		for (UINT i = 1; i + 1 < count; ++i)
		{
			*dest++ = source[0];
			*dest++ = source[i];
			*dest++ = source[i + 1];
		}
	}

	view.BufferLocation = upload.gpu;
	view.SizeInBytes = index_count * index_size;
	view.Format = index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
	return true;
}

// Indices 0, 1, 2, ... of a non-indexed triangle fan, for Upload_Indices to turn into a list.
static void Make_Fan_Indices(UINT vertex_count, std::vector<WORD>& indices)
{
	indices.clear();
	for (UINT i = 0; i < vertex_count; ++i)
		indices.push_back((WORD)i);
}

// The view of a buffer's Direct3D 12 copy.
static D3D12_VERTEX_BUFFER_VIEW Make_Vertex_View(D3D12_GPU_VIRTUAL_ADDRESS address, UINT size, UINT stride)
{
	D3D12_VERTEX_BUFFER_VIEW view;
	view.BufferLocation = address;
	view.SizeInBytes = size;
	view.StrideInBytes = stride;
	return view;
}

HRESULT STDMETHODCALLTYPE Device::DrawPrimitive(D3DPRIMITIVETYPE type, UINT start_vertex, UINT primitive_count)
{
	VertexBuffer* buffer = static_cast<VertexBuffer*>(m_streams[0].buffer);
	const UINT stride = m_streams[0].stride;
	const UINT count = Get_Vertex_Count(type, primitive_count);
	if (buffer == nullptr || stride == 0 || count == 0 || (start_vertex + count) * stride > buffer->Size())
		return D3DERR_INVALIDCALL;
	if (!Begin_Draw())
		return D3D_OK;

	// first is the vertex to start with, in the view.
	D3D12_VERTEX_BUFFER_VIEW vertices;
	UINT first = start_vertex;
	if (buffer->Has_Gpu_Copy())
	{
		const D3D12_GPU_VIRTUAL_ADDRESS address = Prepare_Buffer(buffer->Gpu(), buffer->Data(), buffer->Size(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		if (address == 0)
			return D3D_OK;
		vertices = Make_Vertex_View(address, buffer->Size(), stride);
	}
	else
	{
		if (!Upload_Vertices(buffer->Data() + start_vertex * stride, stride, count, vertices))
			return D3D_OK;
		first = 0;
	}

	if (type == D3DPT_TRIANGLEFAN)
	{
		std::vector<WORD> fan;
		Make_Fan_Indices(count, fan);
		D3D12_INDEX_BUFFER_VIEW indices;
		UINT index_count = 0;
		if (!Upload_Indices(type, fan.data(), false, count, indices, index_count))
			return D3D_OK;
		return Draw_Geometry(type, vertices, &indices, index_count, 0, (INT)first);
	}
	return Draw_Geometry(type, vertices, nullptr, count, first, 0);
}

HRESULT STDMETHODCALLTYPE Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, UINT min_index, UINT num_vertices, UINT start_index, UINT primitive_count)
{
	VertexBuffer* buffer = static_cast<VertexBuffer*>(m_streams[0].buffer);
	IndexBuffer* index_buffer = static_cast<IndexBuffer*>(m_indices);
	const UINT stride = m_streams[0].stride;
	const UINT count = Get_Vertex_Count(type, primitive_count);
	if (buffer == nullptr || index_buffer == nullptr || stride == 0 || count == 0 || num_vertices == 0)
		return D3DERR_INVALIDCALL;

	D3DINDEXBUFFER_DESC index_desc;
	index_buffer->GetDesc(&index_desc);
	const bool index32 = index_desc.Format == D3DFMT_INDEX32;
	const UINT index_size = index32 ? 4 : 2;
	const UINT first_vertex = m_baseVertexIndex + min_index;
	if ((start_index + count) * index_size > index_buffer->Size() || (first_vertex + num_vertices) * stride > buffer->Size())
		return D3DERR_INVALIDCALL;
	if (!Begin_Draw())
		return D3D_OK;

	// Indices count from the base vertex index; view_start is the vertex the view starts with.
	D3D12_VERTEX_BUFFER_VIEW vertices;
	UINT view_start = 0;
	if (buffer->Has_Gpu_Copy())
	{
		const D3D12_GPU_VIRTUAL_ADDRESS address = Prepare_Buffer(buffer->Gpu(), buffer->Data(), buffer->Size(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		if (address == 0)
			return D3D_OK;
		vertices = Make_Vertex_View(address, buffer->Size(), stride);
	}
	else
	{
		if (!Upload_Vertices(buffer->Data() + first_vertex * stride, stride, num_vertices, vertices))
			return D3D_OK;
		view_start = first_vertex;
	}
	const INT base_vertex = (INT)m_baseVertexIndex - (INT)view_start;

	D3D12_INDEX_BUFFER_VIEW indices;
	if (index_buffer->Has_Gpu_Copy() && type != D3DPT_TRIANGLEFAN)
	{
		indices.BufferLocation = Prepare_Buffer(index_buffer->Gpu(), index_buffer->Data(), index_buffer->Size(), D3D12_RESOURCE_STATE_INDEX_BUFFER);
		indices.SizeInBytes = index_buffer->Size();
		indices.Format = index32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
		if (indices.BufferLocation == 0)
			return D3D_OK;
		return Draw_Geometry(type, vertices, &indices, count, start_index, base_vertex);
	}

	UINT index_count = 0;
	if (!Upload_Indices(type, index_buffer->Data() + start_index * index_size, index32, count, indices, index_count))
		return D3D_OK;
	return Draw_Geometry(type, vertices, &indices, index_count, 0, base_vertex);
}

HRESULT STDMETHODCALLTYPE Device::DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitive_count, CONST void* data, UINT stride)
{
	// Like D3D8, leave stream 0 and the indices unset.
	SetStreamSource(0, nullptr, 0);
	SetIndices(nullptr, 0);

	const UINT count = Get_Vertex_Count(type, primitive_count);
	if (data == nullptr || stride == 0 || count == 0)
		return D3DERR_INVALIDCALL;
	if (!Begin_Draw())
		return D3D_OK;

	D3D12_VERTEX_BUFFER_VIEW vertices;
	if (!Upload_Vertices((const unsigned char*)data, stride, count, vertices))
		return D3D_OK;

	if (type == D3DPT_TRIANGLEFAN)
	{
		std::vector<WORD> fan;
		Make_Fan_Indices(count, fan);
		D3D12_INDEX_BUFFER_VIEW indices;
		UINT index_count = 0;
		if (!Upload_Indices(type, fan.data(), false, count, indices, index_count))
			return D3D_OK;
		return Draw_Geometry(type, vertices, &indices, index_count, 0, 0);
	}
	return Draw_Geometry(type, vertices, nullptr, count, 0, 0);
}

HRESULT STDMETHODCALLTYPE Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT min_index, UINT num_vertices, UINT primitive_count, CONST void* index_data, D3DFORMAT index_format, CONST void* vertex_data, UINT stride)
{
	SetStreamSource(0, nullptr, 0);
	SetIndices(nullptr, 0);

	const UINT count = Get_Vertex_Count(type, primitive_count);
	if (index_data == nullptr || vertex_data == nullptr || stride == 0 || count == 0 || num_vertices == 0)
		return D3DERR_INVALIDCALL;
	if (!Begin_Draw())
		return D3D_OK;

	D3D12_VERTEX_BUFFER_VIEW vertices;
	D3D12_INDEX_BUFFER_VIEW indices;
	UINT index_count = 0;
	if (!Upload_Vertices((const unsigned char*)vertex_data + min_index * stride, stride, num_vertices, vertices))
		return D3D_OK;
	if (!Upload_Indices(type, index_data, index_format == D3DFMT_INDEX32, count, indices, index_count))
		return D3D_OK;
	return Draw_Geometry(type, vertices, &indices, index_count, 0, -(INT)min_index);
}

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------

// Returns false for draws that can not be carried out yet; they are dropped.
bool Device::Begin_Draw()
{
	if (m_deviceRemoved)
		return false;
	Begin_Frame();
	return true;
}

static BYTE Get_Material_Source(DWORD source, bool color_vertex, bool has_color0, bool has_color1)
{
	if (!color_vertex)
		return D3DMCS_MATERIAL;
	if (source == D3DMCS_COLOR1 && !has_color0)
		return D3DMCS_MATERIAL;
	if (source == D3DMCS_COLOR2 && !has_color1)
		return D3DMCS_MATERIAL;
	return (BYTE)source;
}

static D3D12_BLEND Get_Blend(DWORD blend, bool alpha)
{
	D3D12_BLEND result = D3D12_BLEND_ONE;
	if (blend >= D3DBLEND_ZERO && blend <= D3DBLEND_SRCALPHASAT)
		result = (D3D12_BLEND)blend;	// same values
	if (alpha)
	{
		// The alpha channel can not blend with colors.
		switch (result)
		{
		case D3D12_BLEND_SRC_COLOR:			return D3D12_BLEND_SRC_ALPHA;
		case D3D12_BLEND_INV_SRC_COLOR:		return D3D12_BLEND_INV_SRC_ALPHA;
		case D3D12_BLEND_DEST_COLOR:		return D3D12_BLEND_DEST_ALPHA;
		case D3D12_BLEND_INV_DEST_COLOR:	return D3D12_BLEND_INV_DEST_ALPHA;
		default:							break;
		}
	}
	return result;
}

ID3DBlob* Device::Get_Shader(UINT64 hash, const std::string& source, const char* target)
{
	auto found = m_shaders.find(hash);
	if (found != m_shaders.end())
		return found->second.Get();

	ComPtr<ID3DBlob> code;
	ComPtr<ID3DBlob> errors;
	const HRESULT hr = D3DCompile(source.data(), source.size(), "d3d8on12", nullptr, nullptr, "main", target,
		D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
	if (FAILED(hr))
	{
		Log("could not compile a %s shader: %s", target, errors != nullptr ? (const char*)errors->GetBufferPointer() : "");
		Log("%s", source.c_str());
		code.Reset();
	}
	m_shaders[hash] = code;
	return code.Get();
}

ID3D12PipelineState* Device::Get_Pipeline_State(D3D12_PRIMITIVE_TOPOLOGY_TYPE topology, const ShaderChoice& shaders, const TargetState& targets)
{
	const DWORD* rs = m_state.renderStates;
	const bool has_depth = targets.dsvFormat != DXGI_FORMAT_UNKNOWN;

	PipelineKey key;
	memset(&key, 0, sizeof(key));
	key.vertexShader = shaders.vertexHash;
	key.pixelShader = shaders.pixelHash;
	key.fvf = shaders.fvf;
	key.topology = (BYTE)topology;
	key.blendEnable = rs[D3DRS_ALPHABLENDENABLE] != FALSE;
	if (key.blendEnable)
	{
		key.srcBlend = (BYTE)rs[D3DRS_SRCBLEND];
		key.destBlend = (BYTE)rs[D3DRS_DESTBLEND];
		key.blendOp = (BYTE)rs[D3DRS_BLENDOP];
	}
	key.colorWrite = (BYTE)(rs[D3DRS_COLORWRITEENABLE] & 0xF);
	key.targetFormat = targets.rtvFormat;
	key.sampleCount = (BYTE)targets.sampleCount;
	key.hasDepthBuffer = has_depth;
	if (has_depth)
	{
		key.depthFormat = targets.dsvFormat;
		key.depthEnable = rs[D3DRS_ZENABLE] != D3DZB_FALSE;
		key.depthWrite = key.depthEnable && rs[D3DRS_ZWRITEENABLE] != FALSE;
		key.depthFunc = key.depthEnable ? (BYTE)rs[D3DRS_ZFUNC] : 0;
		key.stencilEnable = rs[D3DRS_STENCILENABLE] != FALSE;
		if (key.stencilEnable)
		{
			key.stencilFunc = (BYTE)rs[D3DRS_STENCILFUNC];
			key.stencilFail = (BYTE)rs[D3DRS_STENCILFAIL];
			key.stencilDepthFail = (BYTE)rs[D3DRS_STENCILZFAIL];
			key.stencilPass = (BYTE)rs[D3DRS_STENCILPASS];
			key.stencilReadMask = (BYTE)rs[D3DRS_STENCILMASK];
			key.stencilWriteMask = (BYTE)rs[D3DRS_STENCILWRITEMASK];
		}
		key.depthBias = (BYTE)rs[D3DRS_ZBIAS];
	}
	// Point sprites are never culled.
	key.cullMode = shaders.pointSprite ? (BYTE)D3DCULL_NONE : (BYTE)rs[D3DRS_CULLMODE];
	key.fillMode = (BYTE)rs[D3DRS_FILLMODE];

	const UINT64 hash = Hash_Bytes(&key, sizeof(key));
	auto found = m_pipelines.find(hash);
	if (found != m_pipelines.end())
		return found->second.Get();

	// Shader sources are only generated when the compiled shader is not cached yet.
	ID3DBlob* vs = nullptr;
	if (m_shaders.find(key.vertexShader) != m_shaders.end())
		vs = m_shaders[key.vertexShader].Get();
	else
		vs = Get_Shader(key.vertexShader, shaders.vertexKey != nullptr ? Generate_Vertex_Shader(*shaders.vertexKey) : shaders.vertexShader->hlsl, "vs_5_0");
	ID3DBlob* ps = nullptr;
	if (m_shaders.find(key.pixelShader) != m_shaders.end())
		ps = m_shaders[key.pixelShader].Get();
	else
		ps = Get_Shader(key.pixelShader, shaders.pixelKey != nullptr ? Generate_Pixel_Shader(*shaders.pixelKey) : shaders.pixelShader->hlsl, "ps_5_0");
	ID3DBlob* gs = nullptr;
	if (shaders.pointSprite && shaders.vertexKey != nullptr)
	{
		gs = Get_Shader(key.vertexShader ^ 0x6a09e667f3bcc908ull, Generate_Geometry_Shader(*shaders.vertexKey), "gs_5_0");
		if (gs == nullptr)
			vs = nullptr;
	}
	if (vs == nullptr || ps == nullptr)
	{
		m_pipelines[hash] = nullptr;
		return nullptr;
	}

	std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
	if (shaders.vertexShader != nullptr)
		elements = shaders.vertexShader->layout;
	else
		Get_Input_Layout(key.fvf, elements);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = m_rootSignature.Get();
	desc.VS.pShaderBytecode = vs->GetBufferPointer();
	desc.VS.BytecodeLength = vs->GetBufferSize();
	desc.PS.pShaderBytecode = ps->GetBufferPointer();
	desc.PS.BytecodeLength = ps->GetBufferSize();
	if (gs != nullptr)
	{
		desc.GS.pShaderBytecode = gs->GetBufferPointer();
		desc.GS.BytecodeLength = gs->GetBufferSize();
	}

	D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
	blend.BlendEnable = key.blendEnable;
	blend.SrcBlend = Get_Blend(key.srcBlend, false);
	blend.DestBlend = Get_Blend(key.destBlend, false);
	blend.SrcBlendAlpha = Get_Blend(key.srcBlend, true);
	blend.DestBlendAlpha = Get_Blend(key.destBlend, true);
	if (key.srcBlend == D3DBLEND_BOTHSRCALPHA)
	{
		blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_SRC_ALPHA;
		blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	}
	else if (key.srcBlend == D3DBLEND_BOTHINVSRCALPHA)
	{
		blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_SRC_ALPHA;
	}
	blend.BlendOp = blend.BlendOpAlpha = (key.blendOp >= D3DBLENDOP_ADD && key.blendOp <= D3DBLENDOP_MAX) ? (D3D12_BLEND_OP)key.blendOp : D3D12_BLEND_OP_ADD;
	blend.LogicOp = D3D12_LOGIC_OP_NOOP;
	blend.RenderTargetWriteMask = key.colorWrite;	// same bits
	desc.SampleMask = UINT_MAX;

	D3D12_RASTERIZER_DESC& raster = desc.RasterizerState;
	raster.FillMode = key.fillMode == D3DFILL_SOLID ? D3D12_FILL_MODE_SOLID : D3D12_FILL_MODE_WIREFRAME;
	// D3D8 front faces are clockwise, like the Direct3D 12 default.
	raster.CullMode = key.cullMode == D3DCULL_CCW ? D3D12_CULL_MODE_BACK : (key.cullMode == D3DCULL_CW ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_NONE);
	raster.FrontCounterClockwise = FALSE;
	raster.DepthBias = -(INT)key.depthBias * 4;
	raster.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC& depth = desc.DepthStencilState;
	depth.DepthEnable = key.depthEnable;
	depth.DepthWriteMask = key.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
	depth.DepthFunc = key.depthEnable ? (D3D12_COMPARISON_FUNC)key.depthFunc : D3D12_COMPARISON_FUNC_ALWAYS;	// same values
	depth.StencilEnable = key.stencilEnable;
	depth.StencilReadMask = key.stencilReadMask;
	depth.StencilWriteMask = key.stencilWriteMask;
	D3D12_DEPTH_STENCILOP_DESC face = {};
	face.StencilFailOp = key.stencilEnable ? (D3D12_STENCIL_OP)key.stencilFail : D3D12_STENCIL_OP_KEEP;	// same values
	face.StencilDepthFailOp = key.stencilEnable ? (D3D12_STENCIL_OP)key.stencilDepthFail : D3D12_STENCIL_OP_KEEP;
	face.StencilPassOp = key.stencilEnable ? (D3D12_STENCIL_OP)key.stencilPass : D3D12_STENCIL_OP_KEEP;
	face.StencilFunc = key.stencilEnable ? (D3D12_COMPARISON_FUNC)key.stencilFunc : D3D12_COMPARISON_FUNC_ALWAYS;
	depth.FrontFace = face;
	depth.BackFace = face;

	desc.InputLayout.pInputElementDescs = elements.data();
	desc.InputLayout.NumElements = (UINT)elements.size();
	desc.PrimitiveTopologyType = topology;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = key.targetFormat;
	desc.DSVFormat = has_depth ? key.depthFormat : DXGI_FORMAT_UNKNOWN;
	desc.SampleDesc.Count = key.sampleCount;

	ComPtr<ID3D12PipelineState> pipeline;
	if (FAILED(m_device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline))))
		Log("could not create a pipeline state for FVF 0x%X", key.fvf);
	m_pipelines[hash] = pipeline;
	return pipeline.Get();
}

void Device::Fill_Vertex_Constants(FFVertexConstants& c)
{
	const DWORD* rs = m_state.renderStates;
	const D3DMATRIX& view = m_state.transforms[0];
	const D3DMATRIX& projection = m_state.transforms[1];
	const D3DMATRIX& world = m_state.transforms[10];

	D3DMATRIX world_view;
	Multiply(world, view, world_view);
	memcpy(c.world, &world, sizeof(c.world));
	memcpy(c.worldView, &world_view, sizeof(c.worldView));
	memcpy(c.projection, &projection, sizeof(c.projection));
	for (UINT stage = 0; stage < FF_STAGES; ++stage)
		memcpy(c.texTransform[stage], &m_state.transforms[2 + stage], sizeof(c.texTransform[stage]));

	const D3DVIEWPORT8& vp = m_state.viewport;
	c.viewport[0] = (float)vp.X;
	c.viewport[1] = (float)vp.Y;
	c.viewport[2] = (float)max(vp.Width, 1u);
	c.viewport[3] = (float)max(vp.Height, 1u);

	To_Float4(rs[D3DRS_AMBIENT], c.globalAmbient);
	To_Float4(m_state.material.Diffuse, c.materialDiffuse);
	To_Float4(m_state.material.Ambient, c.materialAmbient);
	To_Float4(m_state.material.Specular, c.materialSpecular);
	To_Float4(m_state.material.Emissive, c.materialEmissive);
	c.materialPower[0] = m_state.material.Power;
	c.materialPower[1] = c.materialPower[2] = c.materialPower[3] = 0;
	c.fog[0] = As_Float(rs[D3DRS_FOGSTART]);
	c.fog[1] = As_Float(rs[D3DRS_FOGEND]);
	c.fog[2] = As_Float(rs[D3DRS_FOGDENSITY]);
	c.fog[3] = 0;

	int count = 0;
	for (UINT i = 0; i < MAX_LIGHTS && count < (int)FF_LIGHTS; ++i)
	{
		if (!m_state.lightEnabled[i])
			continue;
		const D3DLIGHT8& light = m_state.lights[i];
		FFLight& out = c.lights[count++];
		To_Float4(light.Diffuse, out.diffuse);
		To_Float4(light.Specular, out.specular);
		To_Float4(light.Ambient, out.ambient);

		const D3DVECTOR position = Transform_Point(light.Position, view);
		out.position[0] = position.x;
		out.position[1] = position.y;
		out.position[2] = position.z;
		out.position[3] = (float)light.Type;

		// Towards the light.
		D3DVECTOR direction = Transform_Direction(light.Direction, view);
		out.direction[0] = -direction.x;
		out.direction[1] = -direction.y;
		out.direction[2] = -direction.z;
		out.direction[3] = light.Range;

		out.attenuation[0] = light.Attenuation0;
		out.attenuation[1] = light.Attenuation1;
		out.attenuation[2] = light.Attenuation2;
		out.attenuation[3] = light.Falloff;
		out.spot[0] = cosf(light.Theta * 0.5f);
		out.spot[1] = cosf(light.Phi * 0.5f);
		out.spot[2] = out.spot[3] = 0;
	}
	c.lightCount[0] = count;
	c.lightCount[1] = c.lightCount[2] = c.lightCount[3] = 0;

	c.pointSize[0] = As_Float(rs[D3DRS_POINTSIZE]);
	c.pointSize[1] = As_Float(rs[D3DRS_POINTSIZE_MIN]);
	c.pointSize[2] = min(As_Float(rs[D3DRS_POINTSIZE_MAX]), 256.0f);
	c.pointSize[3] = 0;
	c.pointScale[0] = As_Float(rs[D3DRS_POINTSCALE_A]);
	c.pointScale[1] = As_Float(rs[D3DRS_POINTSCALE_B]);
	c.pointScale[2] = As_Float(rs[D3DRS_POINTSCALE_C]);
	c.pointScale[3] = 0;
}

void Device::Fill_Pixel_Constants(FFPixelConstants& c)
{
	const DWORD* rs = m_state.renderStates;
	To_Float4(rs[D3DRS_TEXTUREFACTOR], c.textureFactor);
	To_Float4(rs[D3DRS_FOGCOLOR], c.fogColor);
	for (UINT stage = 0; stage < FF_STAGES; ++stage)
	{
		const DWORD* tss = m_state.textureStageStates[stage];
		c.bumpMatrix[stage][0] = As_Float(tss[D3DTSS_BUMPENVMAT00]);
		c.bumpMatrix[stage][1] = As_Float(tss[D3DTSS_BUMPENVMAT01]);
		c.bumpMatrix[stage][2] = As_Float(tss[D3DTSS_BUMPENVMAT10]);
		c.bumpMatrix[stage][3] = As_Float(tss[D3DTSS_BUMPENVMAT11]);
		c.bumpLuminance[stage][0] = As_Float(tss[D3DTSS_BUMPENVLSCALE]);
		c.bumpLuminance[stage][1] = As_Float(tss[D3DTSS_BUMPENVLOFFSET]);
		c.bumpLuminance[stage][2] = c.bumpLuminance[stage][3] = 0;
	}
	c.alphaRef[0] = (rs[D3DRS_ALPHAREF] & 0xFF) / 255.0f;
	c.alphaRef[1] = c.alphaRef[2] = c.alphaRef[3] = 0;
	memcpy(c.shaderConstants, m_state.pixelShaderConstants, sizeof(c.shaderConstants));
}

void Device::Fill_Shader_Vertex_Constants(ShaderVertexConstants& c)
{
	memcpy(c.c, m_state.vertexShaderConstants, sizeof(c.c));
	const D3DVIEWPORT8& vp = m_state.viewport;
	c.viewport[0] = (float)vp.X;
	c.viewport[1] = (float)vp.Y;
	c.viewport[2] = (float)max(vp.Width, 1u);
	c.viewport[3] = (float)max(vp.Height, 1u);
}

// The translation of a vertex shader created with CreateVertexShader, translated on first use.
const TranslatedShader* Device::Get_Translated_Vertex_Shader(DWORD handle, UINT64& hash)
{
	const size_t index = handle >> 1;
	if (index >= m_vertexShaders.size() || !m_vertexShaders[index].used)
		return nullptr;
	const ShaderData& shader = m_vertexShaders[index];
	hash = shader.hash;

	auto found = m_translatedShaders.find(hash);
	if (found == m_translatedShaders.end())
	{
		TranslatedShader translated;
		std::string error;
		translated.ok = !shader.function.empty() && Translate_Vertex_Shader(shader.declaration.data(), shader.function.data(), translated.hlsl, translated.layout, error);
		if (!translated.ok)
			Log("could not translate a vertex shader: %s", shader.function.empty() ? "no byte code" : error.c_str());
		found = m_translatedShaders.insert(std::make_pair(hash, translated)).first;
	}
	return found->second.ok ? &found->second : nullptr;
}

const TranslatedShader* Device::Get_Translated_Pixel_Shader(const PixelShaderTranslation& info, const DWORD* function, UINT64& hash)
{
	hash = Hash_Bytes(&info, sizeof(info)) ^ 0x13198a2e03707344ull;
	auto found = m_translatedShaders.find(hash);
	if (found == m_translatedShaders.end())
	{
		TranslatedShader translated;
		std::string error;
		translated.ok = Translate_Pixel_Shader(function, info, translated.hlsl, error);
		if (!translated.ok)
			Log("could not translate a pixel shader: %s", error.c_str());
		found = m_translatedShaders.insert(std::make_pair(hash, translated)).first;
	}
	return found->second.ok ? &found->second : nullptr;
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------

HRESULT Device::Draw_Geometry(D3DPRIMITIVETYPE type, const D3D12_VERTEX_BUFFER_VIEW& vertices, const D3D12_INDEX_BUFFER_VIEW* indices, UINT count, UINT first, INT base_vertex)
{
	const DWORD* rs = m_state.renderStates;
	const bool shader_vs = (m_state.vertexShader & 1) != 0;
	const bool shader_ps = m_state.pixelShader != 0;
	const DWORD fvf = shader_vs ? 0 : m_state.vertexShader;
	const bool rhw = (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW;
	const bool has_color0 = (fvf & D3DFVF_DIFFUSE) != 0;
	const bool has_color1 = (fvf & D3DFVF_SPECULAR) != 0;

	// Pixel shader key: the enabled texture stages.
	PixelShaderKey ps_key;
	memset(&ps_key, 0, sizeof(ps_key));
	UINT srvs[FF_STAGES] = {};
	PixelShaderTranslation ps_info;
	memset(&ps_info, 0, sizeof(ps_info));
	for (UINT stage = 0; shader_ps && stage < FF_STAGES; ++stage)
	{
		// Pixel shaders can read every stage that has a texture.
		const DWORD* tss = m_state.textureStageStates[stage];
		TextureType type_of_texture = TEXTURE_NONE;
		srvs[stage] = Prepare_Texture(m_textures[stage], type_of_texture);
		ps_info.stages[stage].textureType = (BYTE)type_of_texture;
		ps_info.stages[stage].projected = (tss[D3DTSS_TEXTURETRANSFORMFLAGS] & D3DTTFF_PROJECTED) != 0;
		ps_info.stages[stage].coordCount = (BYTE)(tss[D3DTSS_TEXTURETRANSFORMFLAGS] & 0xFF);
		ps_key.stages[stage].textureType = (BYTE)type_of_texture;
		ps_key.stageCount = FF_STAGES;
	}
	for (UINT stage = 0; !shader_ps && stage < FF_STAGES; ++stage)
	{
		const DWORD* tss = m_state.textureStageStates[stage];
		if (tss[D3DTSS_COLOROP] == D3DTOP_DISABLE)
			break;
		PixelShaderKey::Stage& s = ps_key.stages[stage];
		s.colorOp = (BYTE)tss[D3DTSS_COLOROP];
		s.colorArg0 = (BYTE)tss[D3DTSS_COLORARG0];
		s.colorArg1 = (BYTE)tss[D3DTSS_COLORARG1];
		s.colorArg2 = (BYTE)tss[D3DTSS_COLORARG2];
		s.alphaOp = (BYTE)tss[D3DTSS_ALPHAOP];
		s.alphaArg0 = (BYTE)tss[D3DTSS_ALPHAARG0];
		s.alphaArg1 = (BYTE)tss[D3DTSS_ALPHAARG1];
		s.alphaArg2 = (BYTE)tss[D3DTSS_ALPHAARG2];
		s.resultArg = (BYTE)tss[D3DTSS_RESULTARG];
		s.projected = (tss[D3DTSS_TEXTURETRANSFORMFLAGS] & D3DTTFF_PROJECTED) != 0;
		s.coordCount = (BYTE)(tss[D3DTSS_TEXTURETRANSFORMFLAGS] & 0xFF);

		TextureType type_of_texture = TEXTURE_NONE;
		srvs[stage] = Prepare_Texture(m_textures[stage], type_of_texture);
		s.textureType = (BYTE)type_of_texture;
		ps_key.stageCount = (BYTE)(stage + 1);
	}
	ps_key.alphaFunc = rs[D3DRS_ALPHATESTENABLE] ? (BYTE)rs[D3DRS_ALPHAFUNC] : 0;
	if (ps_key.alphaFunc == D3DCMP_ALWAYS)
		ps_key.alphaFunc = 0;

	BYTE fog_mode = D3DFOG_NONE;
	if (rs[D3DRS_FOGENABLE])
		fog_mode = (BYTE)(rs[D3DRS_FOGTABLEMODE] != D3DFOG_NONE ? rs[D3DRS_FOGTABLEMODE] : rs[D3DRS_FOGVERTEXMODE]);
	ps_key.fog = fog_mode != D3DFOG_NONE || (rs[D3DRS_FOGENABLE] && rhw);
	ps_key.specular = rs[D3DRS_SPECULARENABLE] != FALSE;

	// Vertex shader key.
	VertexShaderKey vs_key;
	memset(&vs_key, 0, sizeof(vs_key));
	vs_key.fvf = fvf;
	vs_key.lighting = rs[D3DRS_LIGHTING] && (fvf & D3DFVF_NORMAL) != 0 && !rhw;
	if (vs_key.lighting)
	{
		const bool color_vertex = rs[D3DRS_COLORVERTEX] != FALSE;
		vs_key.diffuseSource = Get_Material_Source(rs[D3DRS_DIFFUSEMATERIALSOURCE], color_vertex, has_color0, has_color1);
		vs_key.ambientSource = Get_Material_Source(rs[D3DRS_AMBIENTMATERIALSOURCE], color_vertex, has_color0, has_color1);
		vs_key.specularSource = Get_Material_Source(rs[D3DRS_SPECULARMATERIALSOURCE], color_vertex, has_color0, has_color1);
		vs_key.emissiveSource = Get_Material_Source(rs[D3DRS_EMISSIVEMATERIALSOURCE], color_vertex, has_color0, has_color1);
	}
	vs_key.fogMode = ps_key.fog ? (fog_mode != D3DFOG_NONE ? fog_mode : (BYTE)D3DFOG_LINEAR) : (BYTE)D3DFOG_NONE;
	vs_key.rangeFog = rs[D3DRS_RANGEFOGENABLE] != FALSE;
	vs_key.stageCount = ps_key.stageCount;	// all of them for pixel shaders
	vs_key.pointSprite = type == D3DPT_POINTLIST && rs[D3DRS_POINTSPRITEENABLE] != FALSE;
	vs_key.pointScale = vs_key.pointSprite && rs[D3DRS_POINTSCALEENABLE] != FALSE && !rhw;
	for (UINT stage = 0; stage < vs_key.stageCount; ++stage)
	{
		const DWORD* tss = m_state.textureStageStates[stage];
		VertexShaderKey::Stage& s = vs_key.stages[stage];
		s.coordIndex = (BYTE)(tss[D3DTSS_TEXCOORDINDEX] & 0xFFFF);
		s.texGen = (BYTE)((tss[D3DTSS_TEXCOORDINDEX] >> 16) & 0xFF);
		s.transformCount = (BYTE)(tss[D3DTSS_TEXTURETRANSFORMFLAGS] & 0xFF);
	}

	ShaderChoice shaders = {};
	shaders.fvf = fvf;
	shaders.pointSprite = vs_key.pointSprite != 0 && !shader_vs;
	if (shader_vs)
	{
		shaders.vertexShader = Get_Translated_Vertex_Shader(m_state.vertexShader, shaders.vertexHash);
		if (shaders.vertexShader == nullptr)
			return D3D_OK;
	}
	else
	{
		shaders.vertexKey = &vs_key;
		shaders.vertexHash = Hash_Bytes(&vs_key, sizeof(vs_key));
	}
	if (shader_ps)
	{
		const size_t index = m_state.pixelShader - 1;
		if (index >= m_pixelShaders.size() || !m_pixelShaders[index].used)
			return D3D_OK;
		const std::vector<DWORD>& function = m_pixelShaders[index].function;
		ps_info.fog = rs[D3DRS_FOGENABLE] != FALSE;
		ps_info.alphaFunc = ps_key.alphaFunc;
		ps_info.function = m_pixelShaders[index].hash;
		shaders.pixelShader = Get_Translated_Pixel_Shader(ps_info, function.data(), shaders.pixelHash);
		if (shaders.pixelShader == nullptr)
			return D3D_OK;
	}
	else
	{
		shaders.pixelKey = &ps_key;
		shaders.pixelHash = Hash_Bytes(&ps_key, sizeof(ps_key)) ^ 0x5bd1e995;
	}

	TargetState targets;
	if (!Prepare_Targets(targets))
		return D3D_OK;
	ID3D12PipelineState* pipeline = Get_Pipeline_State(Get_Topology_Type(type), shaders, targets);
	if (pipeline == nullptr)
		return D3D_OK;

	const UINT vs_size = shader_vs ? sizeof(ShaderVertexConstants) : sizeof(FFVertexConstants);
	UploadAllocation vs_constants = Allocate_Upload(vs_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	UploadAllocation ps_constants = Allocate_Upload(sizeof(FFPixelConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
	if (vs_constants.cpu == nullptr || ps_constants.cpu == nullptr)
		return D3D_OK;
	if (shader_vs)
		Fill_Shader_Vertex_Constants(*(ShaderVertexConstants*)vs_constants.cpu);
	else
		Fill_Vertex_Constants(*(FFVertexConstants*)vs_constants.cpu);
	Fill_Pixel_Constants(*(FFPixelConstants*)ps_constants.cpu);

	ID3D12GraphicsCommandList* list = m_commandList.Get();
	if (!m_heapsBound)
	{
		ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get(), m_samplerHeap.Get() };
		list->SetDescriptorHeaps(2, heaps);
		m_heapsBound = true;
	}
	list->SetGraphicsRootSignature(m_rootSignature.Get());

	list->OMSetRenderTargets(1, &targets.rtv, FALSE, targets.dsvFormat != DXGI_FORMAT_UNKNOWN ? &targets.dsv : nullptr);
	list->OMSetStencilRef(rs[D3DRS_STENCILREF] & 0xFF);

	const D3DVIEWPORT8& vp = m_state.viewport;
	D3D12_VIEWPORT viewport = { (float)vp.X, (float)vp.Y, (float)vp.Width, (float)vp.Height, vp.MinZ, vp.MaxZ };
	D3D12_RECT scissor = { (LONG)vp.X, (LONG)vp.Y, (LONG)(vp.X + vp.Width), (LONG)(vp.Y + vp.Height) };
	list->RSSetViewports(1, &viewport);
	list->RSSetScissorRects(1, &scissor);

	list->SetPipelineState(pipeline);
	list->SetGraphicsRootConstantBufferView(ROOT_VERTEX_CONSTANTS, vs_constants.gpu);
	list->SetGraphicsRootConstantBufferView(ROOT_PIXEL_CONSTANTS, ps_constants.gpu);
	for (UINT stage = 0; stage < FF_STAGES; ++stage)
	{
		// Every table of the root signature must be set, also those the shader does not read.
		const UINT srv = stage < ps_key.stageCount ? srvs[stage] : 0;
		list->SetGraphicsRootDescriptorTable(ROOT_TEXTURES + stage, Srv_Gpu_Handle(srv));
		const UINT sampler = stage < ps_key.stageCount && ps_key.stages[stage].textureType != TEXTURE_NONE ? Get_Sampler(stage) : 0;
		D3D12_GPU_DESCRIPTOR_HANDLE sampler_handle = m_samplerHeap->GetGPUDescriptorHandleForHeapStart();
		sampler_handle.ptr += (UINT64)sampler * m_samplerDescriptorSize;
		list->SetGraphicsRootDescriptorTable(ROOT_SAMPLERS + stage, sampler_handle);
	}

	list->IASetPrimitiveTopology(Get_Topology(type));
	list->IASetVertexBuffers(0, 1, &vertices);
	if (indices != nullptr)
	{
		list->IASetIndexBuffer(indices);
		list->DrawIndexedInstanced(count, 1, first, base_vertex, 0);
	}
	else
	{
		list->DrawInstanced(count, 1, first, 0);
	}
	return D3D_OK;
}

} // namespace D3D8On12
