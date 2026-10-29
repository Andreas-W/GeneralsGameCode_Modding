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

// ShaderCompiler.cpp

#include "ShaderCompiler.h"

#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <string.h>

namespace D3D8On12
{

// {5A58797D-A72C-478D-8BA2-EFC6B0EFE88E}, defined here instead of linking dxguid, which also defines the D3D8 IDs.
static const IID IID_ShaderReflection = { 0x5a58797d, 0xa72c, 0x478d, { 0x8b, 0xa2, 0xef, 0xc6, 0xb0, 0xef, 0xe8, 0x8e } };

// Resolves the device's header itself and leaves other files to the engine.
class IncludeHandler : public ID3DInclude
{
public:
	IncludeHandler(const char* header_name, const char* header, ID3DInclude* engine)
		: m_headerName(header_name), m_header(header), m_engine(engine) {}

	HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE type, LPCSTR name, LPCVOID parent, LPCVOID* data, UINT* bytes) override
	{
		if (m_headerName != nullptr && _stricmp(name, m_headerName) == 0)
		{
			*data = m_header;
			*bytes = (UINT)strlen(m_header);
			return S_OK;
		}
		return m_engine != nullptr ? m_engine->Open(type, name, parent, data, bytes) : E_FAIL;
	}

	HRESULT STDMETHODCALLTYPE Close(LPCVOID data) override
	{
		if (data == m_header)
			return S_OK;
		return m_engine != nullptr ? m_engine->Close(data) : S_OK;
	}

private:
	const char* m_headerName;
	const char* m_header;
	ID3DInclude* m_engine;
};

HRESULT Compile_Pixel_Shader(const char* source, UINT length, const char* name, const char* header_name, const char* header,
	ID3DInclude* include, ComPtr<ID3DBlob>& code, ID3DBlob** errors)
{
	if (errors != nullptr)
		*errors = nullptr;
	IncludeHandler handler(header_name, header, include);
	ComPtr<ID3DBlob> messages;
	const HRESULT hr = D3DCompile(source, length, name, nullptr, &handler, "main", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
		&code, &messages);
	if (FAILED(hr) && errors != nullptr)
		*errors = messages.Detach();
	return hr;
}

void Reflect_Constant_Buffer(ID3DBlob* code, const char* buffer_name, UINT& size, std::unordered_map<std::string, ShaderVariable>& variables)
{
	size = 0;
	variables.clear();
	ComPtr<ID3D12ShaderReflection> reflection;
	if (FAILED(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(), IID_ShaderReflection, (void**)reflection.GetAddressOf())))
		return;
	ID3D12ShaderReflectionConstantBuffer* buffer = reflection->GetConstantBufferByName(buffer_name);
	D3D12_SHADER_BUFFER_DESC buffer_desc;
	if (buffer == nullptr || FAILED(buffer->GetDesc(&buffer_desc)))
		return;
	size = buffer_desc.Size;
	for (UINT i = 0; i < buffer_desc.Variables; ++i)
	{
		D3D12_SHADER_VARIABLE_DESC variable_desc;
		if (SUCCEEDED(buffer->GetVariableByIndex(i)->GetDesc(&variable_desc)))
		{
			ShaderVariable variable = { variable_desc.StartOffset, variable_desc.Size };
			variables[variable_desc.Name] = variable;
		}
	}
}

} // namespace D3D8On12
