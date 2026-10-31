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

// ShaderTranslate.cpp
// Translates D3D8 shader byte code (vs.1.1, ps.1.1 to ps.1.4) into HLSL, one statement per
// instruction. Registers become local variables; the D3D8 vertex declaration becomes the input
// layout of the vertex shader.

#include "Pipeline.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace D3D8On12
{

// Token layout of shader model 1 byte code.
static const DWORD OPCODE_MASK = 0xFFFF;
static const DWORD COISSUE = 0x40000000;
static const DWORD COMMENT = 0xFFFE;
static const DWORD END = 0x0000FFFF;

enum Opcode
{
	OP_NOP = 0, OP_MOV, OP_ADD, OP_SUB, OP_MAD, OP_MUL, OP_RCP, OP_RSQ, OP_DP3, OP_DP4, OP_MIN, OP_MAX, OP_SLT, OP_SGE,
	OP_EXP, OP_LOG, OP_LIT, OP_DST, OP_LRP, OP_FRC, OP_M4x4, OP_M4x3, OP_M3x4, OP_M3x3, OP_M3x2,
	OP_TEXCOORD = 64, OP_TEXKILL, OP_TEX, OP_TEXBEM, OP_TEXBEML, OP_TEXREG2AR, OP_TEXREG2GB, OP_TEXM3x2PAD, OP_TEXM3x2TEX,
	OP_TEXM3x3PAD, OP_TEXM3x3TEX, OP_TEXM3x3DIFF, OP_TEXM3x3SPEC, OP_TEXM3x3VSPEC, OP_EXPP, OP_LOGP, OP_CND, OP_DEF,
	OP_TEXREG2RGB, OP_TEXDP3TEX, OP_TEXM3x2DEPTH, OP_TEXDP3, OP_TEXM3x3, OP_TEXDEPTH, OP_CMP, OP_BEM,
	OP_PHASE = 0xFFFD,
};

enum RegisterType
{
	REG_TEMP = 0, REG_INPUT = 1, REG_CONST = 2, REG_ADDR_OR_TEXTURE = 3, REG_RASTOUT = 4, REG_ATTROUT = 5, REG_TEXCRDOUT = 6,
};

static void Append(std::string& text, const char* format, ...)
{
	char line[1024];
	va_list args;
	va_start(args, format);
	_vsnprintf_s(line, sizeof(line), _TRUNCATE, format, args);
	va_end(args);
	text += line;
}

// Number of parameter tokens of an instruction, -1 for unknown instructions.
static int Get_Parameter_Count(DWORD opcode, bool ps14)
{
	switch (opcode)
	{
	case OP_NOP: case OP_PHASE:						return 0;
	case OP_TEXKILL: case OP_TEXDEPTH:				return 1;
	case OP_TEXCOORD: case OP_TEX:					return ps14 ? 2 : 1;
	case OP_MOV: case OP_RCP: case OP_RSQ: case OP_EXP: case OP_LOG: case OP_LIT: case OP_FRC:
	case OP_EXPP: case OP_LOGP: case OP_TEXBEM: case OP_TEXBEML: case OP_TEXREG2AR: case OP_TEXREG2GB:
	case OP_TEXREG2RGB: case OP_TEXM3x2PAD: case OP_TEXM3x2TEX: case OP_TEXM3x3PAD: case OP_TEXM3x3TEX:
	case OP_TEXM3x3DIFF: case OP_TEXM3x3VSPEC: case OP_TEXDP3TEX: case OP_TEXM3x2DEPTH: case OP_TEXDP3: case OP_TEXM3x3:
		return 2;
	case OP_ADD: case OP_SUB: case OP_MUL: case OP_DP3: case OP_DP4: case OP_MIN: case OP_MAX: case OP_SLT: case OP_SGE:
	case OP_DST: case OP_M4x4: case OP_M4x3: case OP_M3x4: case OP_M3x3: case OP_M3x2: case OP_TEXM3x3SPEC: case OP_BEM:
		return 3;
	case OP_MAD: case OP_LRP: case OP_CND: case OP_CMP:
		return 4;
	case OP_DEF:
		return 5;
	}
	return -1;
}

static UINT Get_Register_Type(DWORD token)
{
	return (token >> 28) & 7;
}

static UINT Get_Register_Number(DWORD token)
{
	return token & 0x7FF;
}

struct Translator
{
	bool pixel;
	bool ps14;
	std::string body;
	bool failed;
	const char* failure;
	float defs[96][4];
	bool defined[96];
	const PixelShaderTranslation* ps;

	Translator() : pixel(false), ps14(false), failed(false), failure(""), ps(nullptr)
	{
		memset(defined, 0, sizeof(defined));
	}

	void Fail(const char* reason)
	{
		if (!failed)
		{
			failed = true;
			failure = reason;
		}
	}

	std::string Register_Name(DWORD token, bool relative)
	{
		const UINT number = Get_Register_Number(token);
		char name[64];
		switch (Get_Register_Type(token))
		{
		case REG_TEMP:		_snprintf_s(name, sizeof(name), _TRUNCATE, "r%u", number); break;
		case REG_INPUT:		_snprintf_s(name, sizeof(name), _TRUNCATE, "v%u", number); break;
		case REG_CONST:
			if (relative)
				_snprintf_s(name, sizeof(name), _TRUNCATE, "c[clamp(a0.x + %u, 0, 95)]", number);
			else if (number < 96 && defined[number])
				_snprintf_s(name, sizeof(name), _TRUNCATE, "float4(%.9g, %.9g, %.9g, %.9g)", defs[number][0], defs[number][1], defs[number][2], defs[number][3]);
			else
				_snprintf_s(name, sizeof(name), _TRUNCATE, "c[%u]", number);
			break;
		case REG_ADDR_OR_TEXTURE:
			if (pixel)
				_snprintf_s(name, sizeof(name), _TRUNCATE, "t%u", number);
			else
				_snprintf_s(name, sizeof(name), _TRUNCATE, "a0");
			break;
		case REG_RASTOUT:
			_snprintf_s(name, sizeof(name), _TRUNCATE, number == 0 ? "oPos" : (number == 1 ? "oFog" : "oPts"));
			break;
		case REG_ATTROUT:	_snprintf_s(name, sizeof(name), _TRUNCATE, "oD%u", number); break;
		case REG_TEXCRDOUT:	_snprintf_s(name, sizeof(name), _TRUNCATE, "oT%u", number); break;
		default:
			Fail("unknown register type");
			_snprintf_s(name, sizeof(name), _TRUNCATE, "r0");
			break;
		}
		return name;
	}

	// A source operand as a float4 expression.
	std::string Source(DWORD token)
	{
		const bool relative = !pixel && (token & (1 << 13)) != 0;
		std::string value = Register_Name(token, relative);
		if (Get_Register_Type(token) == REG_ADDR_OR_TEXTURE && !pixel)
			value = "float4(a0)";

		const DWORD swizzle = (token >> 16) & 0xFF;
		if (swizzle != 0xE4)
		{
			static const char components[] = "xyzw";
			char text[8] = { '.', components[swizzle & 3], components[(swizzle >> 2) & 3], components[(swizzle >> 4) & 3], components[(swizzle >> 6) & 3], 0 };
			value = "(" + value + ")" + text;
		}

		switch ((token >> 24) & 0xF)
		{
		case 0:		break;
		case 1:		value = "(-" + value + ")"; break;
		case 2:		value = "(" + value + " - 0.5)"; break;
		case 3:		value = "(0.5 - " + value + ")"; break;
		case 4:		value = "(2 * " + value + " - 1)"; break;
		case 5:		value = "(1 - 2 * " + value + ")"; break;
		case 6:		value = "(1 - " + value + ")"; break;
		case 7:		value = "(2 * " + value + ")"; break;
		case 8:		value = "(-2 * " + value + ")"; break;
		case 9:		value = "((" + value + ") / (" + value + ").z)"; break;	// _dz
		case 10:	value = "((" + value + ") / (" + value + ").w)"; break;	// _dw
		default:	Fail("unknown source modifier"); break;
		}
		return value;
	}

	// Assigns a float4 expression to a destination, with its write mask, shift and saturation.
	void Write(DWORD token, const std::string& expression)
	{
		std::string value = expression;
		switch ((token >> 24) & 0xF)
		{
		case 0:		break;
		case 1:		value = "(" + value + ") * 2"; break;
		case 2:		value = "(" + value + ") * 4"; break;
		case 3:		value = "(" + value + ") * 8"; break;
		case 13:	value = "(" + value + ") / 8"; break;
		case 14:	value = "(" + value + ") / 4"; break;
		case 15:	value = "(" + value + ") / 2"; break;
		default:	Fail("unknown result shift"); break;
		}
		if (token & (1 << 20))
			value = "saturate(" + value + ")";

		DWORD mask = (token >> 16) & 0xF;
		if (mask == 0)
			mask = 0xF;
		std::string components;
		if (mask & 1) components += 'x';
		if (mask & 2) components += 'y';
		if (mask & 4) components += 'z';
		if (mask & 8) components += 'w';

		const bool address = !pixel && Get_Register_Type(token) == REG_ADDR_OR_TEXTURE;
		const std::string name = Register_Name(token, false);
		if (address)
			Append(body, "	a0.%s = (int4)floor(float4(%s)).%s;\n", components.c_str(), value.c_str(), components.c_str());
		else
			Append(body, "	%s.%s = (float4(%s)).%s;\n", name.c_str(), components.c_str(), value.c_str(), components.c_str());
	}

	// Coordinates of a texture stage for tex and texbem in ps.1.1 to 1.3.
	std::string Stage_Coords(UINT stage)
	{
		char text[128];
		const PixelShaderTranslation::Stage& s = ps->stages[stage];
		if (s.projected && s.coordCount >= 2)
		{
			static const char components[] = "xyzw";
			_snprintf_s(text, sizeof(text), _TRUNCATE, "(input.tex%u / input.tex%u.%c)", stage, stage, components[s.coordCount - 1]);
		}
		else
		{
			_snprintf_s(text, sizeof(text), _TRUNCATE, "input.tex%u", stage);
		}
		return text;
	}

	std::string Sample(UINT stage, const std::string& coords)
	{
		char text[256];
		if (stage >= FF_STAGES || ps->stages[stage].textureType == TEXTURE_NONE)
			return "float4(0, 0, 0, 1)";
		if (ps->stages[stage].textureType == TEXTURE_CUBE)
			_snprintf_s(text, sizeof(text), _TRUNCATE, "Texture%u.Sample(Sampler%u, (%s).xyz)", stage, stage, coords.c_str());
		else
			_snprintf_s(text, sizeof(text), _TRUNCATE, "Texture%u.Sample(Sampler%u, (%s).xy)", stage, stage, coords.c_str());
		return text;
	}

	void Instruction(DWORD opcode, const DWORD* params)
	{
		const DWORD dst = params[0];
		switch (opcode)
		{
		case OP_NOP:
		case OP_PHASE:
			return;
		case OP_MOV:	Write(dst, Source(params[1])); return;
		case OP_ADD:	Write(dst, Source(params[1]) + " + " + Source(params[2])); return;
		case OP_SUB:	Write(dst, Source(params[1]) + " - " + Source(params[2])); return;
		case OP_MUL:	Write(dst, Source(params[1]) + " * " + Source(params[2])); return;
		case OP_MAD:	Write(dst, Source(params[1]) + " * " + Source(params[2]) + " + " + Source(params[3])); return;
		case OP_MIN:	Write(dst, "min(" + Source(params[1]) + ", " + Source(params[2]) + ")"); return;
		case OP_MAX:	Write(dst, "max(" + Source(params[1]) + ", " + Source(params[2]) + ")"); return;
		case OP_SLT:	Write(dst, "float4(" + Source(params[1]) + " < " + Source(params[2]) + ")"); return;
		case OP_SGE:	Write(dst, "float4(" + Source(params[1]) + " >= " + Source(params[2]) + ")"); return;
		case OP_DP3:	Write(dst, "dot((" + Source(params[1]) + ").xyz, (" + Source(params[2]) + ").xyz).xxxx"); return;
		case OP_DP4:	Write(dst, "dot(" + Source(params[1]) + ", " + Source(params[2]) + ").xxxx"); return;
		case OP_RCP:	Write(dst, "Rcp((" + Source(params[1]) + ").w).xxxx"); return;
		case OP_RSQ:	Write(dst, "Rsq((" + Source(params[1]) + ").w).xxxx"); return;
		case OP_EXP:	Write(dst, "exp2((" + Source(params[1]) + ").w).xxxx"); return;
		case OP_LOG:	Write(dst, "Log2((" + Source(params[1]) + ").w).xxxx"); return;
		case OP_EXPP:	Write(dst, "Expp((" + Source(params[1]) + ").w)"); return;
		case OP_LOGP:	Write(dst, "Logp((" + Source(params[1]) + ").w)"); return;
		case OP_FRC:	Write(dst, "frac(" + Source(params[1]) + ")"); return;
		case OP_LIT:	Write(dst, "Lit(" + Source(params[1]) + ")"); return;
		case OP_DST:	Write(dst, "Dst(" + Source(params[1]) + ", " + Source(params[2]) + ")"); return;
		case OP_LRP:	Write(dst, "lerp(" + Source(params[3]) + ", " + Source(params[2]) + ", " + Source(params[1]) + ")"); return;
		case OP_M4x4: case OP_M4x3: case OP_M3x4: case OP_M3x3: case OP_M3x2:
		{
			// Rows of the matrix are consecutive constant registers.
			const bool three = opcode == OP_M3x4 || opcode == OP_M3x3 || opcode == OP_M3x2;
			const int rows = opcode == OP_M4x4 || opcode == OP_M3x4 ? 4 : (opcode == OP_M3x2 ? 2 : 3);
			std::string value = "float4(";
			for (int row = 0; row < 4; ++row)
			{
				if (row > 0)
					value += ", ";
				if (row >= rows)
				{
					value += "0";
					continue;
				}
				const DWORD matrix = params[2] + row;	// next register
				if (three)
					value += "dot((" + Source(params[1]) + ").xyz, (" + Source(matrix) + ").xyz)";
				else
					value += "dot(" + Source(params[1]) + ", " + Source(matrix) + ")";
			}
			value += ")";
			Write(dst, value);
			return;
		}
		case OP_CND:
			if (ps14)
				Write(dst, "(" + Source(params[1]) + " > 0.5 ? " + Source(params[2]) + " : " + Source(params[3]) + ")");
			else
				Write(dst, "((" + Source(params[1]) + ").w > 0.5 ? " + Source(params[2]) + " : " + Source(params[3]) + ")");
			return;
		case OP_CMP:
			Write(dst, "(" + Source(params[1]) + " >= 0 ? " + Source(params[2]) + " : " + Source(params[3]) + ")");
			return;
		case OP_DEF:
		{
			const UINT number = Get_Register_Number(dst);
			if (number < 96)
			{
				memcpy(defs[number], &params[1], sizeof(defs[number]));
				defined[number] = true;
			}
			return;
		}
		}

		if (!pixel)
		{
			Fail("pixel shader instruction in a vertex shader");
			return;
		}

		const UINT stage = Get_Register_Number(dst);
		char name[16];
		_snprintf_s(name, sizeof(name), _TRUNCATE, "t%u", stage);
		switch (opcode)
		{
		case OP_TEX:
			if (ps14)
			{
				// texld r#, coords: samples the stage of the destination register.
				Write(dst, Sample(stage, Source(params[1])));
			}
			else
			{
				Append(body, "	t%u = %s;\n", stage, Sample(stage, Stage_Coords(stage)).c_str());
			}
			return;
		case OP_TEXCOORD:
			if (ps14)
				Write(dst, Source(params[1]));
			else
				Append(body, "	t%u = float4(saturate(input.tex%u.xyz), 1);\n", stage, stage);
			return;
		case OP_TEXKILL:
			if (ps14 && Get_Register_Type(dst) == REG_TEMP)
				Append(body, "	if (any(r%u.xyz < 0)) discard;\n", stage);
			else
				Append(body, "	if (any(input.tex%u.xyz < 0)) discard;\n", stage);
			return;
		case OP_TEXBEM:
		case OP_TEXBEML:
		{
			// The offset from the source register, transformed by the bump matrix of this stage.
			const std::string source = Source(params[1]);
			char coords[256];
			_snprintf_s(coords, sizeof(coords), _TRUNCATE,
				"(%s.xy + float2(dot((%s).xy, BumpMatrix[%u].xz), dot((%s).xy, BumpMatrix[%u].yw)))",
				Stage_Coords(stage).c_str(), source.c_str(), stage, source.c_str(), stage);
			Append(body, "	t%u = %s;\n", stage, Sample(stage, coords).c_str());
			if (opcode == OP_TEXBEML)
				Append(body, "	t%u.rgb *= saturate((%s).z * BumpLuminance[%u].x + BumpLuminance[%u].y);\n", stage, source.c_str(), stage, stage);
			return;
		}
		case OP_TEXREG2AR:
			Append(body, "	t%u = %s;\n", stage, Sample(stage, "(" + Source(params[1]) + ").wx").c_str());
			return;
		case OP_TEXREG2GB:
			Append(body, "	t%u = %s;\n", stage, Sample(stage, "(" + Source(params[1]) + ").yz").c_str());
			return;
		case OP_TEXREG2RGB:
			Append(body, "	t%u = %s;\n", stage, Sample(stage, "(" + Source(params[1]) + ").xyz").c_str());
			return;
		}
		Fail("unsupported pixel shader instruction");
	}
};

static const char* s_helpers =
	"float Rcp(float x) { return x == 0 ? 3.402823466e+38 : 1 / x; }\n"
	"float Rsq(float x) { x = abs(x); return x == 0 ? 3.402823466e+38 : rsqrt(x); }\n"
	"float Log2(float x) { x = abs(x); return x == 0 ? -3.402823466e+38 : log2(x); }\n"
	"float4 Expp(float x) { float f = floor(x); return float4(exp2(f), x - f, exp2(x), 1); }\n"
	"float4 Logp(float x) { x = abs(x); if (x == 0) return float4(-3.402823466e+38, 1, -3.402823466e+38, 1); float e = floor(log2(x)); return float4(e, x / exp2(e), log2(x), 1); }\n"
	"float4 Lit(float4 s) { float4 r = float4(1, 0, 0, 1); if (s.x > 0) { r.y = s.x; if (s.y > 0) r.z = pow(s.y, clamp(s.w, -127.9961, 127.9961)); } return r; }\n"
	"float4 Dst(float4 a, float4 b) { return float4(1, a.y * b.y, a.z, b.w); }\n";

static bool Translate_Body(Translator& t, const DWORD* function, DWORD& version)
{
	version = function[0];
	const DWORD* token = function + 1;
	for (int guard = 0; guard < 65536; ++guard)
	{
		const DWORD instruction = *token;
		if (instruction == END)
			return !t.failed;
		const DWORD opcode = instruction & OPCODE_MASK;
		if (opcode == COMMENT)
		{
			token += 1 + ((instruction >> 16) & 0x7FFF);
			continue;
		}
		const int count = Get_Parameter_Count(opcode, t.ps14);
		if (count < 0)
		{
			t.Fail("unknown instruction");
			return false;
		}
		t.Instruction(opcode, token + 1);
		token += 1 + count;
	}
	t.Fail("no end token");
	return false;
}

// ----------------------------------------------------------------------------
// Vertex shaders
// ----------------------------------------------------------------------------

static const char* s_outputStruct =
	"struct VSOutput\n"
	"{\n"
	"	float4 position : SV_Position;\n"
	"	float4 diffuse : COLOR0;\n"
	"	float4 specular : COLOR1;\n"
	"	float fog : FOG;\n"
	"	float4 tex0 : TEXCOORD0;\n"
	"	float4 tex1 : TEXCOORD1;\n"
	"	float4 tex2 : TEXCOORD2;\n"
	"	float4 tex3 : TEXCOORD3;\n"
	"	float4 tex4 : TEXCOORD4;\n"
	"	float4 tex5 : TEXCOORD5;\n"
	"	float4 tex6 : TEXCOORD6;\n"
	"	float4 tex7 : TEXCOORD7;\n"
	"};\n";

bool Translate_Vertex_Shader(const DWORD* declaration, const DWORD* function, std::string& hlsl, std::vector<D3D12_INPUT_ELEMENT_DESC>& layout, std::string& error)
{
	layout.clear();
	hlsl.clear();

	// Inputs from the declaration: registers of stream 0 with their formats.
	std::string inputs = "struct VSInput\n{\n";
	std::string conversions;
	bool input_used[16] = {};
	UINT stream = 0;
	UINT offset = 0;
	for (const DWORD* token = declaration; *token != D3DVSD_END(); ++token)
	{
		const DWORD type = (*token & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;
		if (type == D3DVSD_TOKEN_STREAM)
		{
			stream = *token & D3DVSD_STREAMNUMBERMASK;
			offset = 0;
		}
		else if (type == D3DVSD_TOKEN_STREAMDATA)
		{
			if (*token & 0x10000000)	// D3DVSD_SKIP
			{
				offset += 4 * ((*token & D3DVSD_SKIPCOUNTMASK) >> D3DVSD_SKIPCOUNTSHIFT);
				continue;
			}
			const UINT reg = *token & D3DVSD_VERTEXREGMASK;
			const UINT data_type = (*token & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT;
			if (stream != 0)
			{
				error = "vertex declaration with more than one stream";
				return false;
			}

			D3D12_INPUT_ELEMENT_DESC element = {};
			element.SemanticName = "V";
			element.SemanticIndex = reg;
			element.AlignedByteOffset = offset;
			element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
			const char* input_type = "float4";
			const char* conversion = "float4(input.v%u)";
			switch (data_type)
			{
			case D3DVSDT_FLOAT1:	element.Format = DXGI_FORMAT_R32_FLOAT; input_type = "float"; conversion = "float4(input.v%u, 0, 0, 1)"; offset += 4; break;
			case D3DVSDT_FLOAT2:	element.Format = DXGI_FORMAT_R32G32_FLOAT; input_type = "float2"; conversion = "float4(input.v%u, 0, 1)"; offset += 8; break;
			case D3DVSDT_FLOAT3:	element.Format = DXGI_FORMAT_R32G32B32_FLOAT; input_type = "float3"; conversion = "float4(input.v%u, 1)"; offset += 12; break;
			case D3DVSDT_FLOAT4:	element.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; offset += 16; break;
			case D3DVSDT_D3DCOLOR:	element.Format = DXGI_FORMAT_B8G8R8A8_UNORM; offset += 4; break;
			case D3DVSDT_UBYTE4:	element.Format = DXGI_FORMAT_R8G8B8A8_UINT; input_type = "uint4"; offset += 4; break;
			case D3DVSDT_SHORT2:	element.Format = DXGI_FORMAT_R16G16_SINT; input_type = "int2"; conversion = "float4(input.v%u, 0, 1)"; offset += 4; break;
			case D3DVSDT_SHORT4:	element.Format = DXGI_FORMAT_R16G16B16A16_SINT; input_type = "int4"; offset += 8; break;
			default:
				error = "unknown vertex declaration type";
				return false;
			}
			layout.push_back(element);
			input_used[reg] = true;
			Append(inputs, "	%s v%u : V%u;\n", input_type, reg, reg);
			char line[128];
			_snprintf_s(line, sizeof(line), _TRUNCATE, conversion, reg);
			Append(conversions, "	v%u = %s;\n", reg, line);
		}
		else if (type == D3DVSD_TOKEN_CONSTMEM)
		{
			token += 4 * ((*token & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT);
			error = "constants in a vertex declaration";
			return false;
		}
		else if (type == D3DVSD_TOKEN_EXT)
		{
			token += (*token & D3DVSD_EXTCOUNTMASK) >> D3DVSD_EXTCOUNTSHIFT;
		}
	}
	inputs += "};\n";

	if (function == nullptr)
	{
		error = "fixed function vertex shader declaration";
		return false;
	}

	Translator t;
	t.pixel = false;
	DWORD version = 0;
	if (!Translate_Body(t, function, version))
	{
		error = t.failure;
		return false;
	}

	hlsl += "cbuffer VertexConstants : register(b0)\n{\n	float4 c[96];\n	float4 Viewport;\n};\n";
	hlsl += s_helpers;
	hlsl += inputs;
	hlsl += s_outputStruct;
	hlsl += "VSOutput main(VSInput input)\n{\n";
	hlsl += "	float4 v0 = 0, v1 = 0, v2 = 0, v3 = 0, v4 = 0, v5 = 0, v6 = 0, v7 = 0, v8 = 0, v9 = 0, v10 = 0, v11 = 0, v12 = 0, v13 = 0, v14 = 0, v15 = 0;\n";
	hlsl += conversions;
	hlsl += "	float4 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r8 = 0, r9 = 0, r10 = 0, r11 = 0;\n";
	hlsl += "	int4 a0 = 0;\n";
	hlsl += "	float4 oPos = float4(0, 0, 0, 1), oFog = 1, oPts = 1, oD0 = 1, oD1 = 0;\n";
	hlsl += "	float4 oT0 = 0, oT1 = 0, oT2 = 0, oT3 = 0, oT4 = 0, oT5 = 0, oT6 = 0, oT7 = 0;\n";
	hlsl += t.body;
	hlsl +=
		"	VSOutput output;\n"
		"	output.position = oPos;\n"
		// D3D8 pixel centers are on integer coordinates, Direct3D 12 ones half a pixel further.
		"	output.position.xy += float2(1.0 / Viewport.z, -1.0 / Viewport.w) * oPos.w;\n"
		"	output.diffuse = saturate(oD0);\n"
		"	output.specular = saturate(oD1);\n"
		"	output.fog = saturate(oFog.x);\n"
		"	output.tex0 = oT0; output.tex1 = oT1; output.tex2 = oT2; output.tex3 = oT3;\n"
		"	output.tex4 = oT4; output.tex5 = oT5; output.tex6 = oT6; output.tex7 = oT7;\n"
		"	return output;\n"
		"}\n";
	(void)input_used;
	return true;
}

// ----------------------------------------------------------------------------
// Pixel shaders
// ----------------------------------------------------------------------------

bool Translate_Pixel_Shader(const DWORD* function, const PixelShaderTranslation& info, std::string& hlsl, std::string& error)
{
	hlsl.clear();
	const DWORD version = function[0];
	if ((version & 0xFFFF0000) != 0xFFFF0000 || ((version >> 8) & 0xFF) != 1)
	{
		error = "not a ps.1.x shader";
		return false;
	}

	Translator t;
	t.pixel = true;
	t.ps14 = (version & 0xFF) >= 4;
	t.ps = &info;
	DWORD parsed_version = 0;
	if (!Translate_Body(t, function, parsed_version))
	{
		error = t.failure;
		return false;
	}

	hlsl +=
		"cbuffer PixelConstants : register(b1)\n"
		"{\n"
		"	float4 TextureFactor;\n"
		"	float4 FogColor;\n"
		"	float4 BumpMatrix[8];\n"
		"	float4 BumpLuminance[8];\n"
		"	float4 AlphaRef;\n"
		"	float4 c[8];\n"
		"};\n";
	for (UINT stage = 0; stage < FF_STAGES; ++stage)
	{
		if (info.stages[stage].textureType == TEXTURE_2D)
			Append(hlsl, "Texture2D Texture%u : register(t%u);\nSamplerState Sampler%u : register(s%u);\n", stage, stage, stage, stage);
		else if (info.stages[stage].textureType == TEXTURE_CUBE)
			Append(hlsl, "TextureCube Texture%u : register(t%u);\nSamplerState Sampler%u : register(s%u);\n", stage, stage, stage, stage);
	}
	hlsl += s_helpers;
	hlsl += s_outputStruct;
	hlsl += "float4 main(VSOutput input) : SV_Target\n{\n";
	hlsl += "	float4 v0 = saturate(input.diffuse), v1 = saturate(input.specular);\n";
	hlsl += "	float4 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0;\n";
	// In ps.1.4 the t registers are the texture coordinates; before, they receive texture reads.
	if (t.ps14)
		hlsl += "	float4 t0 = input.tex0, t1 = input.tex1, t2 = input.tex2, t3 = input.tex3, t4 = input.tex4, t5 = input.tex5, t6 = input.tex6, t7 = input.tex7;\n";
	else
		hlsl += "	float4 t0 = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0, t5 = 0, t6 = 0, t7 = 0;\n";
	hlsl += t.body;
	hlsl += "	float4 color = r0;\n";

	// Fog and the alpha test come after the pixel shader in D3D8.
	if (info.fog)
		hlsl += "	color.rgb = lerp(FogColor.rgb, color.rgb, saturate(input.fog));\n";
	switch (info.alphaFunc)
	{
	case D3DCMP_NEVER:			hlsl += "	discard;\n"; break;
	case D3DCMP_LESS:			hlsl += "	if (!(color.a < AlphaRef.x)) discard;\n"; break;
	case D3DCMP_EQUAL:			hlsl += "	if (!(color.a == AlphaRef.x)) discard;\n"; break;
	case D3DCMP_LESSEQUAL:		hlsl += "	if (!(color.a <= AlphaRef.x)) discard;\n"; break;
	case D3DCMP_GREATER:		hlsl += "	if (!(color.a > AlphaRef.x)) discard;\n"; break;
	case D3DCMP_NOTEQUAL:		hlsl += "	if (!(color.a != AlphaRef.x)) discard;\n"; break;
	case D3DCMP_GREATEREQUAL:	hlsl += "	if (!(color.a >= AlphaRef.x)) discard;\n"; break;
	default:					break;
	}
	hlsl += "	return color;\n}\n";
	return true;
}

} // namespace D3D8On12
