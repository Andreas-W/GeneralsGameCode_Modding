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

// ShaderCompiler.h
// Compiling the HLSL pixel shaders of the extensions (post-processing, materials): a header the device
// provides, other includes from the engine, and the variables of a parameter cbuffer by name.

#pragma once

#include "Private.h"

#include <d3dcommon.h>

#include <string>
#include <unordered_map>

namespace D3D8On12
{

struct ShaderVariable
{
	UINT offset;
	UINT size;
};

// Compiles a ps_5_0 pixel shader with the entry point main. "#include <header_name>" gets header; other
// files are read through include, which may be null. On failure the compiler messages go to errors.
HRESULT Compile_Pixel_Shader(const char* source, UINT length, const char* name, const char* header_name, const char* header,
	ID3DInclude* include, ComPtr<ID3DBlob>& code, ID3DBlob** errors);

// The size and the variables of the cbuffer buffer_name of a compiled shader; 0 and none without it.
void Reflect_Constant_Buffer(ID3DBlob* code, const char* buffer_name, UINT& size, std::unordered_map<std::string, ShaderVariable>& variables);

} // namespace D3D8On12
