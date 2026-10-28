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

namespace D3D8On12
{

static const unsigned TEX = FORMAT_TEXTURE;
static const unsigned RT = FORMAT_RENDER_TARGET;
static const unsigned DS = FORMAT_DEPTH_STENCIL;
static const unsigned DISPLAY = FORMAT_DISPLAY;

// Formats without a matching DXGI format are converted on upload. Palettized and 24-bit formats are
// not offered: the engine falls back to other formats when CheckDeviceFormat refuses them.
static const FormatInfo s_formats[] =
{
	//	D3D8 format				DXGI format								bytes	compr.	conv.	usage
	{ D3DFMT_A8R8G8B8,		DXGI_FORMAT_B8G8R8A8_UNORM,			4,		false,	false,	TEX | RT },
	{ D3DFMT_X8R8G8B8,		DXGI_FORMAT_B8G8R8X8_UNORM,			4,		false,	false,	TEX | RT | DISPLAY },
	{ D3DFMT_R5G6B5,			DXGI_FORMAT_B5G6R5_UNORM,				2,		false,	false,	TEX },
	{ D3DFMT_X1R5G5B5,		DXGI_FORMAT_B5G5R5A1_UNORM,			2,		false,	true,		TEX },
	{ D3DFMT_A1R5G5B5,		DXGI_FORMAT_B5G5R5A1_UNORM,			2,		false,	false,	TEX },
	{ D3DFMT_A4R4G4B4,		DXGI_FORMAT_B4G4R4A4_UNORM,			2,		false,	false,	TEX },
	{ D3DFMT_X4R4G4B4,		DXGI_FORMAT_B4G4R4A4_UNORM,			2,		false,	true,		TEX },
	{ D3DFMT_A8,				DXGI_FORMAT_A8_UNORM,					1,		false,	false,	TEX },
	{ D3DFMT_L8,				DXGI_FORMAT_R8_UNORM,					1,		false,	false,	TEX },
	{ D3DFMT_A8L8,			DXGI_FORMAT_R8G8_UNORM,					2,		false,	false,	TEX },
	{ D3DFMT_V8U8,			DXGI_FORMAT_R8G8_SNORM,					2,		false,	false,	TEX },
	{ D3DFMT_DXT1,			DXGI_FORMAT_BC1_UNORM,					8,		true,		false,	TEX },
	{ D3DFMT_DXT2,			DXGI_FORMAT_BC2_UNORM,					16,		true,		false,	TEX },
	{ D3DFMT_DXT3,			DXGI_FORMAT_BC2_UNORM,					16,		true,		false,	TEX },
	{ D3DFMT_DXT4,			DXGI_FORMAT_BC3_UNORM,					16,		true,		false,	TEX },
	{ D3DFMT_DXT5,			DXGI_FORMAT_BC3_UNORM,					16,		true,		false,	TEX },
	{ D3DFMT_D16,				DXGI_FORMAT_D16_UNORM,					2,		false,	false,	DS },
	{ D3DFMT_D16_LOCKABLE,	DXGI_FORMAT_D16_UNORM,					2,		false,	false,	DS },
	{ D3DFMT_D24S8,			DXGI_FORMAT_D24_UNORM_S8_UINT,		4,		false,	false,	DS },
	{ D3DFMT_D24X8,			DXGI_FORMAT_D24_UNORM_S8_UINT,		4,		false,	false,	DS },

	// Known so that locks and copies work, but refused by CheckDeviceFormat.
	{ D3DFMT_R8G8B8,			DXGI_FORMAT_B8G8R8A8_UNORM,			3,		false,	true,		0 },
	{ D3DFMT_P8,				DXGI_FORMAT_B8G8R8A8_UNORM,			1,		false,	true,		0 },
	{ D3DFMT_A8P8,			DXGI_FORMAT_B8G8R8A8_UNORM,			2,		false,	true,		0 },
	{ D3DFMT_A4L4,			DXGI_FORMAT_R8G8_UNORM,					1,		false,	true,		0 },
	{ D3DFMT_L6V5U5,			DXGI_FORMAT_R8G8B8A8_SNORM,			2,		false,	true,		0 },
	{ D3DFMT_X8L8V8U8,		DXGI_FORMAT_R8G8B8A8_SNORM,			4,		false,	true,		0 },
	{ D3DFMT_D24X4S4,		DXGI_FORMAT_D24_UNORM_S8_UINT,		4,		false,	false,	0 },
	{ D3DFMT_D15S1,			DXGI_FORMAT_D24_UNORM_S8_UINT,		2,		false,	false,	0 },
	{ D3DFMT_D32,				DXGI_FORMAT_D32_FLOAT,					4,		false,	false,	0 },
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

} // namespace D3D8On12
