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

// McpScreenshot.cpp
// Writes the current 3D view back buffer to a PNG or JPEG file for the MCP command bridge.

#include "StdAfx.h"
#include "mcp/McpScreenshot.h"

#include "WW3D2/dx8wrapper.h"
#include "WW3D2/surfaceclass.h"
#include <stb_image_write.h>

static int s_maxWidth = 0;
static int s_jpegQuality = 0;

void mcpSetScreenshotOptions(int maxWidth, int jpegQuality)
{
	s_maxWidth = maxWidth;
	s_jpegQuality = jpegQuality;
}

/// Box-filter downscale of an RGB image by an integer factor.
static unsigned char *downscale(const unsigned char *src, unsigned int width, unsigned int height, unsigned int factor, unsigned int *outWidth, unsigned int *outHeight)
{
	const unsigned int w = width / factor;
	const unsigned int h = height / factor;
	unsigned char *dst = new unsigned char[3 * w * h];
	for (unsigned int y = 0; y < h; ++y) {
		for (unsigned int x = 0; x < w; ++x) {
			unsigned int sum[3] = { 0, 0, 0 };
			for (unsigned int sy = 0; sy < factor; ++sy) {
				const unsigned char *row = src + 3 * ((y * factor + sy) * width + x * factor);
				for (unsigned int sx = 0; sx < factor; ++sx) {
					sum[0] += row[3 * sx + 0];
					sum[1] += row[3 * sx + 1];
					sum[2] += row[3 * sx + 2];
				}
			}
			unsigned char *out = dst + 3 * (y * w + x);
			for (int c = 0; c < 3; ++c) {
				out[c] = (unsigned char)(sum[c] / (factor * factor));
			}
		}
	}
	*outWidth = w;
	*outHeight = h;
	return dst;
}

bool mcpWriteBackBufferImage(const char *path, int *outWidth, int *outHeight)
{
	SurfaceClass *surface = DX8Wrapper::_Get_DX8_Back_Buffer();
	if (surface == nullptr) {
		return false;
	}
	SurfaceClass::SurfaceDescription surfaceDesc;
	surface->Get_Description(surfaceDesc);

	const bool is32Bit = surfaceDesc.Format == WW3D_FORMAT_A8R8G8B8 || surfaceDesc.Format == WW3D_FORMAT_X8R8G8B8;
	const bool is16Bit = surfaceDesc.Format == WW3D_FORMAT_R5G6B5;
	if (!is32Bit && !is16Bit) {
		surface->Release_Ref();
		return false;
	}

	// Copy to a lockable surface; the back buffer itself usually cannot be locked.
	SurfaceClass *surfaceCopy = NEW_REF(SurfaceClass, (DX8Wrapper::_Create_DX8_Surface(surfaceDesc.Width, surfaceDesc.Height, surfaceDesc.Format)));
	DX8Wrapper::_Copy_DX8_Rects(surface->Peek_D3D_Surface(), nullptr, 0, surfaceCopy->Peek_D3D_Surface(), nullptr);
	surface->Release_Ref();
	surface = nullptr;

	int pitch = 0;
	const unsigned char *bits = (const unsigned char *)surfaceCopy->Lock(&pitch);
	if (bits == nullptr) {
		surfaceCopy->Release_Ref();
		return false;
	}

	unsigned int width = surfaceDesc.Width;
	unsigned int height = surfaceDesc.Height;
	unsigned char *image = new unsigned char[3 * width * height];
	for (unsigned int y = 0; y < height; ++y) {
		for (unsigned int x = 0; x < width; ++x) {
			unsigned char *dst = image + 3 * (x + y * width);
			if (is32Bit) {
				const unsigned int argb = ((const unsigned int *)(bits + y * pitch))[x];
				dst[0] = (unsigned char)(argb >> 16);
				dst[1] = (unsigned char)(argb >> 8);
				dst[2] = (unsigned char)(argb >> 0);
			} else {
				const unsigned short rgb = ((const unsigned short *)(bits + y * pitch))[x];
				dst[0] = (unsigned char)((rgb & 0xF800) >> 8);
				dst[1] = (unsigned char)((rgb & 0x07E0) >> 3);
				dst[2] = (unsigned char)((rgb & 0x001F) << 3);
			}
		}
	}
	surfaceCopy->Unlock();
	surfaceCopy->Release_Ref();

	// Full-size frames are several MB as PNG, too large to hand to a model, so shrink them first.
	if (s_maxWidth > 0 && width > (unsigned int)s_maxWidth) {
		const unsigned int factor = (width + s_maxWidth - 1) / s_maxWidth;
		unsigned char *smaller = downscale(image, width, height, factor, &width, &height);
		delete[] image;
		image = smaller;
	}

	int success;
	if (s_jpegQuality > 0) {
		success = stbi_write_jpg(path, width, height, 3, image, s_jpegQuality);
	} else {
		success = stbi_write_png(path, width, height, 3, image, width * 3);
	}
	delete[] image;

	if (outWidth) *outWidth = width;
	if (outHeight) *outHeight = height;
	return success != 0;
}
