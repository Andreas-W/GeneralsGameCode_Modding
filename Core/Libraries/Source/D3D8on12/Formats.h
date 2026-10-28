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

// Formats.h
// D3D8 surface formats and how they are stored in Direct3D 12.

#pragma once

#include "Private.h"

namespace D3D8On12
{

enum FormatUsage
{
	FORMAT_TEXTURE = 1 << 0,		// can be sampled
	FORMAT_RENDER_TARGET = 1 << 1,
	FORMAT_DEPTH_STENCIL = 1 << 2,
	FORMAT_DISPLAY = 1 << 3,		// display mode and back buffer
};

struct FormatInfo
{
	D3DFORMAT format;
	DXGI_FORMAT dxgi;			// format of the Direct3D 12 resource
	unsigned bytes;				// bytes per pixel, or per 4x4 block when compressed
	bool compressed;
	bool converted;				// the D3D8 data is converted when uploaded to the Direct3D 12 resource
	unsigned usage;				// FormatUsage flags
	UINT swizzle;				// shader component mapping of the shader resource view
};

// Returns nullptr for formats this implementation does not know.
const FormatInfo* Get_Format_Info(D3DFORMAT format);

// Bytes between rows of a locked surface (rows of 4x4 blocks for compressed formats).
unsigned Get_Pitch(D3DFORMAT format, unsigned width);

// Number of rows of a locked surface (rows of 4x4 blocks for compressed formats).
unsigned Get_Row_Count(D3DFORMAT format, unsigned height);

// Bytes per pixel (per 4x4 block for compressed formats) of the Direct3D 12 resource.
unsigned Get_Resource_Bytes(const FormatInfo& info);

// Converts one row (one row of blocks) from the D3D8 layout to the Direct3D 12 resource format.
void Convert_Row(const FormatInfo& info, const unsigned char* source, unsigned char* dest, unsigned width);

} // namespace D3D8On12
