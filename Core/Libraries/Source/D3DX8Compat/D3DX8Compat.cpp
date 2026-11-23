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

// D3DX8Compat.cpp
// The D3DX8 functions the engine uses, for x64 builds where d3dx8.lib does not exist. The x64 game
// renders through D3D8on12. Only what the engine calls is here, with the behavior it relies on:
// - matrix and vector math,
// - texture creation, surface copies with format conversion and scaling, mip map generation,
// - pixel shader "assembly" from a table of shaders assembled once with the real D3DX8,
// - no fonts (callers already fall back to their own text when D3DXCreateFont fails).

#include <windows.h>
#include <d3d8.h>
#include <d3dx8.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <new>

//-------------------------------------------------------------------------------------------------
// Math
//-------------------------------------------------------------------------------------------------

D3DXMATRIX* WINAPI D3DXMatrixMultiply(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM1, CONST D3DXMATRIX *pM2)
{
	D3DXMATRIX result;
	for (int r = 0; r < 4; ++r)
	{
		for (int c = 0; c < 4; ++c)
		{
			result.m[r][c] =
				pM1->m[r][0] * pM2->m[0][c] +
				pM1->m[r][1] * pM2->m[1][c] +
				pM1->m[r][2] * pM2->m[2][c] +
				pM1->m[r][3] * pM2->m[3][c];
		}
	}
	*pOut = result;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixTranspose(D3DXMATRIX *pOut, CONST D3DXMATRIX *pM)
{
	D3DXMATRIX result;
	for (int r = 0; r < 4; ++r)
		for (int c = 0; c < 4; ++c)
			result.m[r][c] = pM->m[c][r];
	*pOut = result;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixInverse(D3DXMATRIX *pOut, FLOAT *pDeterminant, CONST D3DXMATRIX *pM)
{
	const float *m = &pM->m[0][0];
	float inv[16];

	inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
	inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
	inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
	inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
	inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
	inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
	inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
	inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
	inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
	inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
	inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
	inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
	inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
	inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
	inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
	inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

	float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
	if (pDeterminant)
		*pDeterminant = det;
	if (det == 0.0f)
		return nullptr;

	float invDet = 1.0f / det;
	float *out = &pOut->m[0][0];
	for (int i = 0; i < 16; ++i)
		out[i] = inv[i] * invDet;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixScaling(D3DXMATRIX *pOut, FLOAT sx, FLOAT sy, FLOAT sz)
{
	memset(pOut, 0, sizeof(*pOut));
	pOut->_11 = sx;
	pOut->_22 = sy;
	pOut->_33 = sz;
	pOut->_44 = 1.0f;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixTranslation(D3DXMATRIX *pOut, FLOAT x, FLOAT y, FLOAT z)
{
	memset(pOut, 0, sizeof(*pOut));
	pOut->_11 = pOut->_22 = pOut->_33 = pOut->_44 = 1.0f;
	pOut->_41 = x;
	pOut->_42 = y;
	pOut->_43 = z;
	return pOut;
}

D3DXMATRIX* WINAPI D3DXMatrixRotationZ(D3DXMATRIX *pOut, FLOAT Angle)
{
	float s = sinf(Angle);
	float c = cosf(Angle);
	memset(pOut, 0, sizeof(*pOut));
	pOut->_11 = c;
	pOut->_12 = s;
	pOut->_21 = -s;
	pOut->_22 = c;
	pOut->_33 = 1.0f;
	pOut->_44 = 1.0f;
	return pOut;
}

D3DXVECTOR4* WINAPI D3DXVec4Transform(D3DXVECTOR4 *pOut, CONST D3DXVECTOR4 *pV, CONST D3DXMATRIX *pM)
{
	D3DXVECTOR4 v = *pV;
	pOut->x = v.x * pM->_11 + v.y * pM->_21 + v.z * pM->_31 + v.w * pM->_41;
	pOut->y = v.x * pM->_12 + v.y * pM->_22 + v.z * pM->_32 + v.w * pM->_42;
	pOut->z = v.x * pM->_13 + v.y * pM->_23 + v.z * pM->_33 + v.w * pM->_43;
	pOut->w = v.x * pM->_14 + v.y * pM->_24 + v.z * pM->_34 + v.w * pM->_44;
	return pOut;
}

D3DXVECTOR4* WINAPI D3DXVec3Transform(D3DXVECTOR4 *pOut, CONST D3DXVECTOR3 *pV, CONST D3DXMATRIX *pM)
{
	D3DXVECTOR4 v(pV->x, pV->y, pV->z, 1.0f);
	return D3DXVec4Transform(pOut, &v, pM);
}

//-------------------------------------------------------------------------------------------------
// Errors, fonts
//-------------------------------------------------------------------------------------------------

HRESULT WINAPI D3DXGetErrorStringA(HRESULT hr, LPSTR pBuffer, UINT BufferLen)
{
	if (pBuffer == nullptr || BufferLen == 0)
		return E_INVALIDARG;
	_snprintf_s(pBuffer, BufferLen, _TRUNCATE, "HRESULT 0x%08lX", (unsigned long)hr);
	return S_OK;
}

HRESULT WINAPI D3DXCreateFont(LPDIRECT3DDEVICE8 pDevice, HFONT hFont, LPD3DXFONT *ppFont)
{
	if (ppFont)
		*ppFont = nullptr;
	return E_NOTIMPL;
}

//-------------------------------------------------------------------------------------------------
// Textures
//-------------------------------------------------------------------------------------------------

static bool Is_Alpha_Format(D3DFORMAT format)
{
	switch (format)
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_A4R4G4B4:
	case D3DFMT_A8:
	case D3DFMT_A8L8:
	case D3DFMT_A4L4:
	case D3DFMT_A8R3G3B2:
	case D3DFMT_DXT2:
	case D3DFMT_DXT3:
	case D3DFMT_DXT4:
	case D3DFMT_DXT5:
		return true;
	default:
		return false;
	}
}

// D3DX picks a similar format when the requested one is not supported; try the common 32-bit ones.
static HRESULT Create_Texture_With_Fallback(LPDIRECT3DDEVICE8 device, UINT width, UINT height, UINT levels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, LPDIRECT3DTEXTURE8 *texture)
{
	HRESULT hr = device->CreateTexture(width, height, levels, usage, format, pool, texture);
	if (SUCCEEDED(hr) || hr == D3DERR_OUTOFVIDEOMEMORY)
		return hr;

	const D3DFORMAT fallback = Is_Alpha_Format(format) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
	if (fallback != format)
	{
		HRESULT fallback_hr = device->CreateTexture(width, height, levels, usage, fallback, pool, texture);
		if (SUCCEEDED(fallback_hr))
			return fallback_hr;
	}
	return hr;
}

HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE8 pDevice, UINT Width, UINT Height, UINT MipLevels,
	DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, LPDIRECT3DTEXTURE8 *ppTexture)
{
	if (pDevice == nullptr || ppTexture == nullptr)
		return D3DERR_INVALIDCALL;
	if (Width == D3DX_DEFAULT)
		Width = 256;
	if (Height == D3DX_DEFAULT)
		Height = 256;
	if (MipLevels == D3DX_DEFAULT)
		MipLevels = 0;
	return Create_Texture_With_Fallback(pDevice, Width, Height, MipLevels, Usage, Format, Pool, ppTexture);
}

HRESULT WINAPI D3DXCreateCubeTexture(LPDIRECT3DDEVICE8 pDevice, UINT Size, UINT MipLevels, DWORD Usage,
	D3DFORMAT Format, D3DPOOL Pool, LPDIRECT3DCUBETEXTURE8 *ppCubeTexture)
{
	if (pDevice == nullptr || ppCubeTexture == nullptr)
		return D3DERR_INVALIDCALL;
	if (Size == D3DX_DEFAULT)
		Size = 256;
	if (MipLevels == D3DX_DEFAULT)
		MipLevels = 0;
	return pDevice->CreateCubeTexture(Size, MipLevels, Usage, Format, Pool, ppCubeTexture);
}

HRESULT WINAPI D3DXCreateVolumeTexture(LPDIRECT3DDEVICE8 pDevice, UINT Width, UINT Height, UINT Depth,
	UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, LPDIRECT3DVOLUMETEXTURE8 *ppVolumeTexture)
{
	if (pDevice == nullptr || ppVolumeTexture == nullptr)
		return D3DERR_INVALIDCALL;
	if (MipLevels == D3DX_DEFAULT)
		MipLevels = 0;
	return pDevice->CreateVolumeTexture(Width, Height, Depth, MipLevels, Usage, Format, Pool, ppVolumeTexture);
}

// Image files are loaded by the engine's own texture loader; this D3DX path is not supported here and
// the caller shows its missing texture.
HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE8 pDevice, LPCSTR pSrcFile, UINT Width,
	UINT Height, UINT MipLevels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, DWORD Filter, DWORD MipFilter,
	D3DCOLOR ColorKey, D3DXIMAGE_INFO *pSrcInfo, PALETTEENTRY *pPalette, LPDIRECT3DTEXTURE8 *ppTexture)
{
	if (ppTexture)
		*ppTexture = nullptr;
	return D3DERR_NOTAVAILABLE;
}

//-------------------------------------------------------------------------------------------------
// Pixels
//-------------------------------------------------------------------------------------------------

namespace
{

struct Color
{
	float r, g, b, a;
};

// Bytes per pixel of the uncompressed formats this file converts, 0 for any other format.
unsigned Pixel_Bytes(D3DFORMAT format)
{
	switch (format)
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
		return 4;
	case D3DFMT_R8G8B8:
		return 3;
	case D3DFMT_R5G6B5:
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_A4R4G4B4:
	case D3DFMT_X4R4G4B4:
	case D3DFMT_A8L8:
	case D3DFMT_V8U8:
		return 2;
	case D3DFMT_A8:
	case D3DFMT_L8:
	case D3DFMT_A4L4:
		return 1;
	default:
		return 0;
	}
}

bool Is_Block_Format(D3DFORMAT format)
{
	return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3 ||
		format == D3DFMT_DXT4 || format == D3DFMT_DXT5;
}

unsigned Block_Bytes(D3DFORMAT format)
{
	return format == D3DFMT_DXT1 ? 8 : 16;
}

inline float Unpack(unsigned value, unsigned bits)
{
	return (float)value / (float)((1u << bits) - 1);
}

inline unsigned Pack(float value, unsigned bits)
{
	if (value < 0.0f)
		value = 0.0f;
	if (value > 1.0f)
		value = 1.0f;
	return (unsigned)(value * (float)((1u << bits) - 1) + 0.5f);
}

Color Decode(D3DFORMAT format, const unsigned char *p)
{
	Color c = { 0.0f, 0.0f, 0.0f, 1.0f };
	unsigned v;
	switch (format)
	{
	case D3DFMT_A8R8G8B8:
		c.b = p[0] / 255.0f; c.g = p[1] / 255.0f; c.r = p[2] / 255.0f; c.a = p[3] / 255.0f;
		break;
	case D3DFMT_X8R8G8B8:
	case D3DFMT_R8G8B8:
		c.b = p[0] / 255.0f; c.g = p[1] / 255.0f; c.r = p[2] / 255.0f;
		break;
	case D3DFMT_R5G6B5:
		v = p[0] | (p[1] << 8);
		c.r = Unpack((v >> 11) & 31, 5); c.g = Unpack((v >> 5) & 63, 6); c.b = Unpack(v & 31, 5);
		break;
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
		v = p[0] | (p[1] << 8);
		c.r = Unpack((v >> 10) & 31, 5); c.g = Unpack((v >> 5) & 31, 5); c.b = Unpack(v & 31, 5);
		if (format == D3DFMT_A1R5G5B5)
			c.a = (v & 0x8000) ? 1.0f : 0.0f;
		break;
	case D3DFMT_A4R4G4B4:
	case D3DFMT_X4R4G4B4:
		v = p[0] | (p[1] << 8);
		c.r = Unpack((v >> 8) & 15, 4); c.g = Unpack((v >> 4) & 15, 4); c.b = Unpack(v & 15, 4);
		if (format == D3DFMT_A4R4G4B4)
			c.a = Unpack((v >> 12) & 15, 4);
		break;
	case D3DFMT_A8L8:
		c.r = c.g = c.b = p[0] / 255.0f; c.a = p[1] / 255.0f;
		break;
	case D3DFMT_V8U8:
		// signed bump offsets, kept as raw bytes in r and g
		c.r = p[0] / 255.0f; c.g = p[1] / 255.0f; c.b = 0.0f;
		break;
	case D3DFMT_A8:
		c.r = c.g = c.b = 0.0f; c.a = p[0] / 255.0f;
		break;
	case D3DFMT_L8:
		c.r = c.g = c.b = p[0] / 255.0f;
		break;
	case D3DFMT_A4L4:
		c.r = c.g = c.b = Unpack(p[0] & 15, 4); c.a = Unpack(p[0] >> 4, 4);
		break;
	default:
		break;
	}
	return c;
}

void Encode(D3DFORMAT format, const Color &c, unsigned char *p)
{
	unsigned v;
	switch (format)
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
		p[0] = (unsigned char)Pack(c.b, 8); p[1] = (unsigned char)Pack(c.g, 8); p[2] = (unsigned char)Pack(c.r, 8);
		p[3] = format == D3DFMT_A8R8G8B8 ? (unsigned char)Pack(c.a, 8) : 0xff;
		break;
	case D3DFMT_R8G8B8:
		p[0] = (unsigned char)Pack(c.b, 8); p[1] = (unsigned char)Pack(c.g, 8); p[2] = (unsigned char)Pack(c.r, 8);
		break;
	case D3DFMT_R5G6B5:
		v = (Pack(c.r, 5) << 11) | (Pack(c.g, 6) << 5) | Pack(c.b, 5);
		p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
		break;
	case D3DFMT_X1R5G5B5:
	case D3DFMT_A1R5G5B5:
		v = (Pack(c.r, 5) << 10) | (Pack(c.g, 5) << 5) | Pack(c.b, 5);
		if (format == D3DFMT_X1R5G5B5 || c.a >= 0.5f)
			v |= 0x8000;
		p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
		break;
	case D3DFMT_A4R4G4B4:
	case D3DFMT_X4R4G4B4:
		v = (Pack(c.r, 4) << 8) | (Pack(c.g, 4) << 4) | Pack(c.b, 4);
		v |= (format == D3DFMT_A4R4G4B4 ? Pack(c.a, 4) : 15u) << 12;
		p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
		break;
	case D3DFMT_A8L8:
		p[0] = (unsigned char)Pack((c.r + c.g + c.b) / 3.0f, 8); p[1] = (unsigned char)Pack(c.a, 8);
		break;
	case D3DFMT_V8U8:
		p[0] = (unsigned char)Pack(c.r, 8); p[1] = (unsigned char)Pack(c.g, 8);
		break;
	case D3DFMT_A8:
		p[0] = (unsigned char)Pack(c.a, 8);
		break;
	case D3DFMT_L8:
		p[0] = (unsigned char)Pack((c.r + c.g + c.b) / 3.0f, 8);
		break;
	case D3DFMT_A4L4:
		p[0] = (unsigned char)(Pack((c.r + c.g + c.b) / 3.0f, 4) | (Pack(c.a, 4) << 4));
		break;
	default:
		break;
	}
}

// D3DCOLOR color key in the source format's precision; matching texels become transparent black.
bool Matches_Color_Key(const Color &c, D3DCOLOR key)
{
	unsigned a = Pack(c.a, 8), r = Pack(c.r, 8), g = Pack(c.g, 8), b = Pack(c.b, 8);
	return ((a << 24) | (r << 16) | (g << 8) | b) == key;
}

RECT Full_Rect(const D3DSURFACE_DESC &desc)
{
	RECT rect = { 0, 0, (LONG)desc.Width, (LONG)desc.Height };
	return rect;
}

} // namespace

HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE8 pDestSurface, CONST PALETTEENTRY *pDestPalette,
	CONST RECT *pDestRect, LPDIRECT3DSURFACE8 pSrcSurface, CONST PALETTEENTRY *pSrcPalette, CONST RECT *pSrcRect,
	DWORD Filter, D3DCOLOR ColorKey)
{
	if (pDestSurface == nullptr || pSrcSurface == nullptr)
		return D3DERR_INVALIDCALL;

	D3DSURFACE_DESC src_desc, dest_desc;
	pSrcSurface->GetDesc(&src_desc);
	pDestSurface->GetDesc(&dest_desc);

	const RECT src_rect = pSrcRect ? *pSrcRect : Full_Rect(src_desc);
	const RECT dest_rect = pDestRect ? *pDestRect : Full_Rect(dest_desc);
	const int src_w = src_rect.right - src_rect.left;
	const int src_h = src_rect.bottom - src_rect.top;
	const int dest_w = dest_rect.right - dest_rect.left;
	const int dest_h = dest_rect.bottom - dest_rect.top;
	if (src_w <= 0 || src_h <= 0 || dest_w <= 0 || dest_h <= 0)
		return D3DERR_INVALIDCALL;

	const bool same_format = src_desc.Format == dest_desc.Format;
	const bool same_size = src_w == dest_w && src_h == dest_h;
	const bool block = Is_Block_Format(src_desc.Format) || Is_Block_Format(dest_desc.Format);

	// Compressed data can only be copied as it is.
	if (block && !(same_format && same_size && ColorKey == 0))
		return E_NOTIMPL;
	if (!block && (Pixel_Bytes(src_desc.Format) == 0 || Pixel_Bytes(dest_desc.Format) == 0))
		return E_NOTIMPL;

	D3DLOCKED_RECT src_lock, dest_lock;
	HRESULT hr = pSrcSurface->LockRect(&src_lock, &src_rect, D3DLOCK_READONLY);
	if (FAILED(hr))
		return hr;
	hr = pDestSurface->LockRect(&dest_lock, &dest_rect, 0);
	if (FAILED(hr))
	{
		pSrcSurface->UnlockRect();
		return hr;
	}

	const unsigned char *src_bits = (const unsigned char *)src_lock.pBits;
	unsigned char *dest_bits = (unsigned char *)dest_lock.pBits;

	if (same_format && same_size && ColorKey == 0)
	{
		unsigned row_bytes, rows;
		if (block)
		{
			row_bytes = ((src_w + 3) / 4) * Block_Bytes(src_desc.Format);
			rows = (src_h + 3) / 4;
		}
		else
		{
			row_bytes = src_w * Pixel_Bytes(src_desc.Format);
			rows = src_h;
		}
		for (unsigned y = 0; y < rows; ++y)
			memcpy(dest_bits + y * dest_lock.Pitch, src_bits + y * src_lock.Pitch, row_bytes);
	}
	else
	{
		const unsigned src_bpp = Pixel_Bytes(src_desc.Format);
		const unsigned dest_bpp = Pixel_Bytes(dest_desc.Format);
		const DWORD filter = Filter & 0xff;
		const bool point = filter == D3DX_FILTER_NONE || filter == D3DX_FILTER_POINT;

		for (int y = 0; y < dest_h; ++y)
		{
			// footprint of this destination row in the source
			int sy0 = (int)((long long)y * src_h / dest_h);
			int sy1 = (int)((long long)(y + 1) * src_h / dest_h);
			if (sy1 <= sy0 || point)
				sy1 = sy0 + 1;

			unsigned char *dest_row = dest_bits + y * dest_lock.Pitch;
			for (int x = 0; x < dest_w; ++x)
			{
				int sx0 = (int)((long long)x * src_w / dest_w);
				int sx1 = (int)((long long)(x + 1) * src_w / dest_w);
				if (sx1 <= sx0 || point)
					sx1 = sx0 + 1;

				Color sum = { 0.0f, 0.0f, 0.0f, 0.0f };
				int count = 0;
				for (int sy = sy0; sy < sy1; ++sy)
				{
					const unsigned char *src_row = src_bits + sy * src_lock.Pitch;
					for (int sx = sx0; sx < sx1; ++sx)
					{
						Color c = Decode(src_desc.Format, src_row + sx * src_bpp);
						if (ColorKey != 0 && Matches_Color_Key(c, ColorKey))
							c.r = c.g = c.b = c.a = 0.0f;
						sum.r += c.r; sum.g += c.g; sum.b += c.b; sum.a += c.a;
						++count;
					}
				}
				const float scale = 1.0f / (float)count;
				Color avg = { sum.r * scale, sum.g * scale, sum.b * scale, sum.a * scale };
				Encode(dest_desc.Format, avg, dest_row + x * dest_bpp);
			}
		}
	}

	pDestSurface->UnlockRect();
	pSrcSurface->UnlockRect();
	return D3D_OK;
}

HRESULT WINAPI D3DXFilterTexture(LPDIRECT3DBASETEXTURE8 pBaseTexture, CONST PALETTEENTRY *pPalette,
	UINT SrcLevel, DWORD Filter)
{
	if (pBaseTexture == nullptr)
		return D3DERR_INVALIDCALL;
	if (pBaseTexture->GetType() != D3DRTYPE_TEXTURE)
		return D3D_OK;

	IDirect3DTexture8 *texture = (IDirect3DTexture8 *)pBaseTexture;
	if (SrcLevel == D3DX_DEFAULT)
		SrcLevel = 0;
	if (Filter == D3DX_DEFAULT)
		Filter = D3DX_FILTER_BOX;

	D3DSURFACE_DESC desc;
	texture->GetLevelDesc(0, &desc);
	// Mip levels of compressed textures cannot be generated here; they keep what they have.
	if (Is_Block_Format(desc.Format) || Pixel_Bytes(desc.Format) == 0)
		return D3D_OK;

	const DWORD count = texture->GetLevelCount();
	for (DWORD level = SrcLevel + 1; level < count; ++level)
	{
		IDirect3DSurface8 *src = nullptr, *dest = nullptr;
		HRESULT hr = texture->GetSurfaceLevel(level - 1, &src);
		if (SUCCEEDED(hr))
			hr = texture->GetSurfaceLevel(level, &dest);
		if (SUCCEEDED(hr))
			hr = D3DXLoadSurfaceFromSurface(dest, nullptr, nullptr, src, nullptr, nullptr, Filter, 0);
		if (dest)
			dest->Release();
		if (src)
			src->Release();
		if (FAILED(hr))
			return hr;
	}
	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
// Shaders
//-------------------------------------------------------------------------------------------------

namespace
{

struct AssembledShader
{
	const char *source;
	const DWORD *code;
	unsigned size;
};

#include "AssembledShaders.inl"

// A plain ID3DXBuffer holding a copy of some bytes.
class Buffer : public ID3DXBuffer
{
public:
	Buffer(const void *data, DWORD size) : m_refs(1), m_size(size)
	{
		m_data = new unsigned char[size ? size : 1];
		if (size)
			memcpy(m_data, data, size);
	}

	STDMETHOD(QueryInterface)(REFIID iid, LPVOID *ppv)
	{
		if (ppv == nullptr)
			return E_POINTER;
		if (iid == IID_IUnknown || iid == IID_ID3DXBuffer)
		{
			*ppv = this;
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHOD_(ULONG, AddRef)() { return ++m_refs; }
	STDMETHOD_(ULONG, Release)()
	{
		ULONG refs = --m_refs;
		if (refs == 0)
			delete this;
		return refs;
	}
	STDMETHOD_(LPVOID, GetBufferPointer)() { return m_data; }
	STDMETHOD_(DWORD, GetBufferSize)() { return m_size; }

private:
	~Buffer() { delete[] m_data; }

	ULONG m_refs;
	unsigned char *m_data;
	DWORD m_size;
};

// Same normalization as the tool that made AssembledShaders.inl: drop comments, trim, collapse
// whitespace to one space, skip empty lines, join lines with '\n'.
void Normalize_Shader(const char *src, unsigned length, char *out, unsigned out_size)
{
	unsigned used = 0;
	bool first_line = true;
	unsigned i = 0;
	while (i <= length)
	{
		// one line
		unsigned end = i;
		while (end < length && src[end] != '\n' && src[end] != '\r' && src[end] != 0)
			++end;

		unsigned line_end = end;
		for (unsigned k = i; k < end; ++k)
		{
			if (src[k] == ';')
			{
				line_end = k;
				break;
			}
		}

		bool any = false;
		bool space = false;
		for (unsigned k = i; k < line_end; ++k)
		{
			char c = src[k];
			if (c == ' ' || c == '\t')
			{
				space = any;
				continue;
			}
			if (!any && !first_line && used + 1 < out_size)
				out[used++] = '\n';
			if (space && used + 1 < out_size)
				out[used++] = ' ';
			space = false;
			any = true;
			if (used + 1 < out_size)
				out[used++] = c;
		}
		if (any)
			first_line = false;

		if (end >= length || src[end] == 0)
			break;
		i = end + 1;
	}
	out[used] = 0;
}

} // namespace

const IID IID_ID3DXBuffer = { 0x932e6a7e, 0xc68e, 0x45dd, { 0xa7, 0xbf, 0x53, 0xd1, 0x9c, 0x86, 0xdb, 0x1f } };

HRESULT WINAPI D3DXAssembleShader(LPCVOID pSrcData, UINT SrcDataLen, DWORD Flags, LPD3DXBUFFER *ppConstants,
	LPD3DXBUFFER *ppCompiledShader, LPD3DXBUFFER *ppCompilationErrors)
{
	if (ppConstants)
		*ppConstants = nullptr;
	if (ppCompiledShader)
		*ppCompiledShader = nullptr;
	if (ppCompilationErrors)
		*ppCompilationErrors = nullptr;
	if (pSrcData == nullptr || ppCompiledShader == nullptr)
		return D3DERR_INVALIDCALL;

	char normalized[4096];
	Normalize_Shader((const char *)pSrcData, SrcDataLen, normalized, sizeof(normalized));

	for (unsigned i = 0; i < sizeof(g_assembledShaders) / sizeof(g_assembledShaders[0]); ++i)
	{
		if (strcmp(normalized, g_assembledShaders[i].source) == 0)
		{
			*ppCompiledShader = new Buffer(g_assembledShaders[i].code, g_assembledShaders[i].size);
			return D3D_OK;
		}
	}

	if (ppCompilationErrors)
	{
		static const char message[] = "D3DX8Compat: this shader is not in AssembledShaders.inl";
		*ppCompilationErrors = new Buffer(message, sizeof(message));
	}
	return D3DERR_INVALIDCALL;
}
