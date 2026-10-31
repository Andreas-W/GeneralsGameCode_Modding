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

// Formats.cpp
// D3D8 surface formats and how they are stored in Direct3D 12.

#include "Formats.h"

#include <string.h>

namespace D3D8On12
{

static const unsigned TEX = FORMAT_TEXTURE;
static const unsigned RT = FORMAT_RENDER_TARGET;
static const unsigned DS = FORMAT_DEPTH_STENCIL;
static const unsigned DISPLAY = FORMAT_DISPLAY;

// Formats without a matching DXGI format are converted on upload. Palettized and 24-bit formats are
// not offered: the engine falls back to other formats when CheckDeviceFormat refuses them.
static const UINT DEFAULT = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
static const UINT LUMINANCE = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
static const UINT LUMINANCE_ALPHA = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 1);
static const UINT BUMP = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 1, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);

// 16-bit formats are expanded to 32 bits, since Direct3D 12 hardware does not have to support all of
// them. Palettized and 24-bit formats are not offered: the engine falls back to other formats when
// CheckDeviceFormat refuses them.
static const FormatInfo s_formats[] =
{
	//	D3D8 format				DXGI format								bytes	compr.	conv.	usage				swizzle
	{ D3DFMT_A8R8G8B8,		DXGI_FORMAT_B8G8R8A8_UNORM,			4,		false,	false,	TEX | RT,			DEFAULT },
	{ D3DFMT_X8R8G8B8,		DXGI_FORMAT_B8G8R8X8_UNORM,			4,		false,	false,	TEX | RT | DISPLAY,	DEFAULT },
	{ D3DFMT_R5G6B5,			DXGI_FORMAT_B8G8R8A8_UNORM,			2,		false,	true,		TEX,				DEFAULT },
	{ D3DFMT_X1R5G5B5,		DXGI_FORMAT_B8G8R8A8_UNORM,			2,		false,	true,		TEX,				DEFAULT },
	{ D3DFMT_A1R5G5B5,		DXGI_FORMAT_B8G8R8A8_UNORM,			2,		false,	true,		TEX,				DEFAULT },
	{ D3DFMT_A4R4G4B4,		DXGI_FORMAT_B8G8R8A8_UNORM,			2,		false,	true,		TEX,				DEFAULT },
	{ D3DFMT_X4R4G4B4,		DXGI_FORMAT_B8G8R8A8_UNORM,			2,		false,	true,		TEX,				DEFAULT },
	{ D3DFMT_A8,				DXGI_FORMAT_A8_UNORM,					1,		false,	false,	TEX,				DEFAULT },
	{ D3DFMT_L8,				DXGI_FORMAT_R8_UNORM,					1,		false,	false,	TEX,				LUMINANCE },
	{ D3DFMT_A8L8,			DXGI_FORMAT_R8G8_UNORM,					2,		false,	false,	TEX,				LUMINANCE_ALPHA },
	{ D3DFMT_V8U8,			DXGI_FORMAT_R8G8_SNORM,					2,		false,	false,	TEX,				BUMP },
	{ D3DFMT_DXT1,			DXGI_FORMAT_BC1_UNORM,					8,		true,		false,	TEX,				DEFAULT },
	{ D3DFMT_DXT2,			DXGI_FORMAT_BC2_UNORM,					16,		true,		false,	TEX,				DEFAULT },
	{ D3DFMT_DXT3,			DXGI_FORMAT_BC2_UNORM,					16,		true,		false,	TEX,				DEFAULT },
	{ D3DFMT_DXT4,			DXGI_FORMAT_BC3_UNORM,					16,		true,		false,	TEX,				DEFAULT },
	{ D3DFMT_DXT5,			DXGI_FORMAT_BC3_UNORM,					16,		true,		false,	TEX,				DEFAULT },
	{ D3DFMT_D16,				DXGI_FORMAT_D16_UNORM,					2,		false,	false,	DS,					DEFAULT },
	{ D3DFMT_D16_LOCKABLE,	DXGI_FORMAT_D16_UNORM,					2,		false,	false,	DS,					DEFAULT },
	{ D3DFMT_D24S8,			DXGI_FORMAT_D24_UNORM_S8_UINT,		4,		false,	false,	DS,					DEFAULT },
	{ D3DFMT_D24X8,			DXGI_FORMAT_D24_UNORM_S8_UINT,		4,		false,	false,	DS,					DEFAULT },

	// Known so that locks and copies work, but refused by CheckDeviceFormat.
	{ D3DFMT_R8G8B8,			DXGI_FORMAT_B8G8R8A8_UNORM,			3,		false,	true,		0,					DEFAULT },
	{ D3DFMT_P8,				DXGI_FORMAT_B8G8R8A8_UNORM,			1,		false,	true,		0,					DEFAULT },
	{ D3DFMT_A8P8,			DXGI_FORMAT_B8G8R8A8_UNORM,			2,		false,	true,		0,					DEFAULT },
	{ D3DFMT_A4L4,			DXGI_FORMAT_R8G8_UNORM,					1,		false,	true,		0,					LUMINANCE_ALPHA },
	{ D3DFMT_L6V5U5,			DXGI_FORMAT_R8G8B8A8_SNORM,			2,		false,	true,		0,					DEFAULT },
	{ D3DFMT_X8L8V8U8,		DXGI_FORMAT_R8G8B8A8_SNORM,			4,		false,	true,		0,					DEFAULT },
	{ D3DFMT_D24X4S4,		DXGI_FORMAT_D24_UNORM_S8_UINT,		4,		false,	false,	0,					DEFAULT },
	{ D3DFMT_D15S1,			DXGI_FORMAT_D24_UNORM_S8_UINT,		2,		false,	false,	0,					DEFAULT },
	{ D3DFMT_D32,				DXGI_FORMAT_D32_FLOAT,					4,		false,	false,	0,					DEFAULT },
};

const FormatInfo* Get_Format_Info(D3DFORMAT format)
{
	for (const FormatInfo& info : s_formats)
	{
		if (info.format == format)
			return &info;
	}
	return nullptr;
}

unsigned Get_Pitch(D3DFORMAT format, unsigned width)
{
	const FormatInfo* info = Get_Format_Info(format);
	if (info == nullptr)
		return 0;
	if (info->compressed)
		return ((width + 3) / 4) * info->bytes;
	return width * info->bytes;
}

unsigned Get_Row_Count(D3DFORMAT format, unsigned height)
{
	const FormatInfo* info = Get_Format_Info(format);
	if (info != nullptr && info->compressed)
		return (height + 3) / 4;
	return height;
}

unsigned Get_Resource_Bytes(const FormatInfo& info)
{
	if (!info.converted)
		return info.bytes;
	switch (info.dxgi)
	{
	case DXGI_FORMAT_R8G8_UNORM:	return 2;
	default:						return 4;
	}
}

// Expands a channel of the given number of bits to 8 bits.
static unsigned char Expand(unsigned value, unsigned bits)
{
	const unsigned max = (1u << bits) - 1;
	return (unsigned char)((value * 255 + max / 2) / max);
}

void Convert_Row(const FormatInfo& info, const unsigned char* source, unsigned char* dest, unsigned width)
{
	if (!info.converted)
	{
		const unsigned bytes = info.compressed ? ((width + 3) / 4) * info.bytes : width * info.bytes;
		memcpy(dest, source, bytes);
		return;
	}

	for (unsigned x = 0; x < width; ++x)
	{
		unsigned char* out = dest + x * Get_Resource_Bytes(info);
		const unsigned short pixel16 = (unsigned short)(source[x * 2] | (source[x * 2 + 1] << 8));

		// Outputs are B, G, R, A in memory.
		switch (info.format)
		{
		case D3DFMT_R5G6B5:
			out[0] = Expand(pixel16 & 0x1F, 5);
			out[1] = Expand((pixel16 >> 5) & 0x3F, 6);
			out[2] = Expand((pixel16 >> 11) & 0x1F, 5);
			out[3] = 255;
			break;
		case D3DFMT_X1R5G5B5:
		case D3DFMT_A1R5G5B5:
			out[0] = Expand(pixel16 & 0x1F, 5);
			out[1] = Expand((pixel16 >> 5) & 0x1F, 5);
			out[2] = Expand((pixel16 >> 10) & 0x1F, 5);
			out[3] = (info.format == D3DFMT_X1R5G5B5 || (pixel16 & 0x8000) != 0) ? 255 : 0;
			break;
		case D3DFMT_A4R4G4B4:
		case D3DFMT_X4R4G4B4:
			out[0] = Expand(pixel16 & 0xF, 4);
			out[1] = Expand((pixel16 >> 4) & 0xF, 4);
			out[2] = Expand((pixel16 >> 8) & 0xF, 4);
			out[3] = info.format == D3DFMT_X4R4G4B4 ? 255 : Expand(pixel16 >> 12, 4);
			break;
		case D3DFMT_R8G8B8:
			out[0] = source[x * 3];
			out[1] = source[x * 3 + 1];
			out[2] = source[x * 3 + 2];
			out[3] = 255;
			break;
		case D3DFMT_A4L4:
			out[0] = Expand(source[x] & 0xF, 4);
			out[1] = Expand(source[x] >> 4, 4);
			break;
		default:
			// Palettized and bump formats with luminance are never offered; leave them black.
			memset(out, 0, Get_Resource_Bytes(info));
			break;
		}
	}
}

DXGI_FORMAT Get_Depth_Typeless_Format(DXGI_FORMAT depth)
{
	switch (depth)
	{
	case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
	case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24G8_TYPELESS;
	case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
	default: return DXGI_FORMAT_UNKNOWN;
	}
}

DXGI_FORMAT Get_Depth_Read_Format(DXGI_FORMAT depth)
{
	switch (depth)
	{
	case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
	case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
	case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
	default: return DXGI_FORMAT_UNKNOWN;
	}
}

} // namespace D3D8On12
