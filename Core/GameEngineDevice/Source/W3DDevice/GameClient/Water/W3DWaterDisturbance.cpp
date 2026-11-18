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

// FILE: W3DWaterDisturbance.cpp //////////////////////////////////////////////
// See W3DWaterDisturbance.h. Drawn with the fixed function pipeline into a render target: a quad that
// multiplies everything by the fade, then additive quads with small shape textures made here.
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DWaterDisturbance.h"

#include "Common/GlobalData.h"
#include "GameClient/PostProcess.h"
#include "W3DDevice/GameClient/W3DMaterials.h"
#include "WW3D2/dx8wrapper.h"

#include <math.h>

#ifdef RTS_RENDERER_DX12
#include <d3d8on12_ext.h>
#endif

static const Int TARGET_SIZE = 512;				///< texels of the disturbance, square
static const Int SHAPE_SIZE = 64;				///< texels of a shape
static const Real STEP_SECONDS = 1.0f / 30.0f;	///< the disturbance fades and takes new shapes this often
static const Int MAX_STEPS = 3;					///< steps caught up at most in one frame
static const size_t MAX_STAMPS = 512;			///< per kind, against runaway callers

//-------------------------------------------------------------------------------------------------
static unsigned short toHalf(float value)
{
	union { float f; UnsignedInt u; } bits;
	bits.f = value;
	const UnsignedInt sign = (bits.u >> 16) & 0x8000;
	Int exponent = (Int)((bits.u >> 23) & 0xFF) - 127 + 15;
	const UnsignedInt mantissa = bits.u & 0x7FFFFF;
	if (exponent <= 0)
		return (unsigned short)sign;
	if (exponent >= 31)
		return (unsigned short)(sign | 0x7BFF);
	return (unsigned short)(sign | (exponent << 10) | (mantissa >> 13));
}

//-------------------------------------------------------------------------------------------------
static float gauss(float x, float width)
{
	return expf(-(x * x) / (width * width));
}

//-------------------------------------------------------------------------------------------------
/** A shape over -1..1 in both directions: crest, trough, foam and highlight. */
//-------------------------------------------------------------------------------------------------
static void shapeTexel(WaterDisturbance::Shape shape, float x, float y, float out[4])
{
	out[0] = out[1] = out[2] = out[3] = 0.0f;
	switch (shape)
	{
		case WaterDisturbance::SHAPE_HULL:
		{
			// The hull fills the ellipse of radius 0.7: a trough along it, crests at bow and stern, foam around
			// its outline and a little at the bow, the highlight over it.
			const float ex = x / 0.7f;
			const float ey = y / 0.7f;
			const float r = sqrtf(ex * ex + ey * ey);
			out[1] = gauss(r, 0.75f) * 0.8f;
			out[0] = (gauss(ex - 1.0f, 0.25f) + 0.6f * gauss(ex + 1.0f, 0.25f)) * gauss(ey, 0.6f);
			out[2] = gauss(r - 1.0f, 0.18f) + 0.5f * gauss(ex - 1.05f, 0.2f) * gauss(ey, 0.5f);
			out[3] = r < 1.0f ? 1.0f : gauss(r - 1.0f, 0.12f);
			break;
		}
		case WaterDisturbance::SHAPE_DISC:
		{
			const float r = sqrtf(x * x + y * y);
			out[0] = gauss(r, 0.45f);
			out[2] = gauss(r, 0.6f);
			out[3] = gauss(r, 0.6f);
			break;
		}
		case WaterDisturbance::SHAPE_RING:
		{
			// The crest on radius 0.8 (the ring's radius), the trough just inside.
			const float r = sqrtf(x * x + y * y);
			out[0] = gauss(r - 0.8f, 0.07f);
			out[1] = gauss(r - 0.66f, 0.07f) * 0.8f;
			out[2] = gauss(r - 0.8f, 0.09f) * 0.6f;
			break;
		}
	}
	// Nothing at the edge of the quad.
	const float edge = 1.0f - fmaxf(fabsf(x), fabsf(y));
	const float fadeOut = edge < 0.08f ? fmaxf(edge, 0.0f) / 0.08f : 1.0f;
	for (Int i = 0; i < 4; ++i)
		out[i] *= fadeOut;
}

//-------------------------------------------------------------------------------------------------
static IDirect3DTexture8 *makeShape(WaterDisturbance::Shape shape, float scale)
{
#ifdef RTS_RENDERER_DX12
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	IDirect3DTexture8 *texture = nullptr;
	if (device == nullptr || FAILED(device->CreateTexture(SHAPE_SIZE, SHAPE_SIZE, 1, 0, D3DFMT_A16B16G16R16F_D3D8ON12, D3DPOOL_MANAGED, &texture)))
		return nullptr;
	D3DLOCKED_RECT locked;
	if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
	{
		texture->Release();
		return nullptr;
	}
	for (Int y = 0; y < SHAPE_SIZE; ++y)
	{
		unsigned short *row = (unsigned short *)((unsigned char *)locked.pBits + y * locked.Pitch);
		for (Int x = 0; x < SHAPE_SIZE; ++x)
		{
			float texel[4];
			shapeTexel(shape, (x + 0.5f) / SHAPE_SIZE * 2.0f - 1.0f, (y + 0.5f) / SHAPE_SIZE * 2.0f - 1.0f, texel);
			// Texture order red, green, blue, alpha.
			for (Int c = 0; c < 4; ++c)
				row[x * 4 + c] = toHalf(texel[c] * scale);
		}
	}
	texture->UnlockRect(0);
	return texture;
#else
	return nullptr;
#endif
}

//-------------------------------------------------------------------------------------------------
W3DWaterDisturbance::W3DWaterDisturbance() :
	m_previous(TheWaterDisturbance),
	m_target(nullptr),
	m_shapesFade(-1.0f),
	m_range(1024.0f),
	m_lastTime(0),
	m_stepTime(0.0f),
	m_active(FALSE),
	m_clear(TRUE)
{
	for (Int i = 0; i < 3; ++i)
		m_sustainedShapes[i] = m_onceShapes[i] = nullptr;
	m_transform[0] = 1.0f / m_range;
	m_transform[1] = m_transform[2] = 0.0f;
	m_transform[3] = m_range;
	TheWaterDisturbance = this;
}

//-------------------------------------------------------------------------------------------------
W3DWaterDisturbance::~W3DWaterDisturbance()
{
	release();
	if (TheWaterDisturbance == this)
		TheWaterDisturbance = m_previous;
}

//-------------------------------------------------------------------------------------------------
void W3DWaterDisturbance::release()
{
	if (m_target != nullptr)
	{
		m_target->Release();
		m_target = nullptr;
	}
	for (Int i = 0; i < 3; ++i)
	{
		if (m_sustainedShapes[i] != nullptr)
			m_sustainedShapes[i]->Release();
		if (m_onceShapes[i] != nullptr)
			m_onceShapes[i]->Release();
		m_sustainedShapes[i] = m_onceShapes[i] = nullptr;
	}
	m_shapesFade = -1.0f;
	m_active = FALSE;
	m_clear = TRUE;
}

//-------------------------------------------------------------------------------------------------
void W3DWaterDisturbance::reset()
{
	m_sustained.clear();
	m_once.clear();
	m_ripples.clear();
	m_clear = TRUE;
}

//-------------------------------------------------------------------------------------------------
void W3DWaterDisturbance::addStamp(const Coord3D& pos, Real angle, Real length, Real width, Real height, Real foam,
	Real highlight, Shape shape, Bool sustained)
{
	if (!m_active)
		return;
	std::vector<Stamp>& list = sustained ? m_sustained : m_once;
	if (list.size() >= MAX_STAMPS)
		return;
	Stamp stamp;
	stamp.pos = pos;
	stamp.angle = angle;
	stamp.length = length;
	stamp.width = width;
	stamp.height = height;
	stamp.foam = foam;
	stamp.highlight = highlight;
	stamp.shape = shape;
	list.push_back(stamp);
}

//-------------------------------------------------------------------------------------------------
void W3DWaterDisturbance::addRipple(const Coord3D& pos, Real startRadius, Real endRadius, Real height, Real foam,
	UnsignedInt frames)
{
	if (!m_active || frames == 0 || m_ripples.size() >= MAX_STAMPS)
		return;
	Ripple ripple;
	ripple.pos = pos;
	ripple.startRadius = startRadius;
	ripple.endRadius = endRadius;
	ripple.height = height;
	ripple.foam = foam;
	ripple.frames = frames;
	ripple.age = 0;
	m_ripples.push_back(ripple);
}

//-------------------------------------------------------------------------------------------------
Bool W3DWaterDisturbance::ensureResources(Real fade)
{
#ifdef RTS_RENDERER_DX12
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	if (device == nullptr)
		return FALSE;
	if (m_target == nullptr)
	{
		if (FAILED(device->CreateTexture(TARGET_SIZE, TARGET_SIZE, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F_D3D8ON12,
			D3DPOOL_DEFAULT, &m_target)))
		{
			m_target = nullptr;
			return FALSE;
		}
		m_clear = TRUE;
	}
	// Sustained shapes are drawn every step, so at (1 - fade) of their strength they settle at it.
	if (m_shapesFade != fade)
	{
		for (Int i = 0; i < 3; ++i)
		{
			if (m_sustainedShapes[i] != nullptr)
				m_sustainedShapes[i]->Release();
			m_sustainedShapes[i] = makeShape((Shape)i, 1.0f - fade);
			if (m_onceShapes[i] == nullptr)
				m_onceShapes[i] = makeShape((Shape)i, 1.0f);
		}
		m_shapesFade = fade;
	}
	return m_sustainedShapes[0] != nullptr && m_onceShapes[0] != nullptr;
#else
	return FALSE;
#endif
}

//-------------------------------------------------------------------------------------------------
/** One shape as a quad, in texels, and again shifted by the texture size where it crosses an edge. */
//-------------------------------------------------------------------------------------------------
void W3DWaterDisturbance::drawStamp(const Stamp& stamp, IDirect3DTexture8 *texture, Real scale)
{
#ifdef RTS_RENDERER_DX12
	struct Vertex
	{
		float x, y, z, rhw;
		DWORD color;
		float u, v;
	};
	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	const float texelsPerUnit = TARGET_SIZE / m_range;
	float cx = fmodf(stamp.pos.x / m_range, 1.0f);
	float cy = fmodf(stamp.pos.y / m_range, 1.0f);
	if (cx < 0.0f)
		cx += 1.0f;
	if (cy < 0.0f)
		cy += 1.0f;
	cx *= TARGET_SIZE;
	cy *= TARGET_SIZE;
	// The quad: hull shapes fill 0.7 of it, rings have their crest at 0.8 of it.
	float halfLength = stamp.length * 0.5f * texelsPerUnit;
	float halfWidth = stamp.width * 0.5f * texelsPerUnit;
	if (stamp.shape == SHAPE_HULL)
	{
		halfLength /= 0.7f;
		halfWidth /= 0.7f;
	}
	else if (stamp.shape == SHAPE_RING)
	{
		halfLength /= 0.8f;
		halfWidth /= 0.8f;
	}
	if (halfLength < 0.5f && halfWidth < 0.5f)
		return;
	const float c = cosf(stamp.angle);
	const float s = sinf(stamp.angle);
	const float clamp01[4] = {
		fminf(fmaxf(stamp.height * scale, 0.0f), 1.0f), fminf(fmaxf(stamp.height * scale, 0.0f), 1.0f),
		fminf(fmaxf(stamp.foam * scale, 0.0f), 1.0f), fminf(fmaxf(stamp.highlight * scale, 0.0f), 1.0f) };
	const DWORD color = D3DCOLOR_ARGB((DWORD)(clamp01[3] * 255.0f + 0.5f), (DWORD)(clamp01[0] * 255.0f + 0.5f),
		(DWORD)(clamp01[1] * 255.0f + 0.5f), (DWORD)(clamp01[2] * 255.0f + 0.5f));
	const float reach = sqrtf(halfLength * halfLength + halfWidth * halfWidth);
	device->SetTexture(0, texture);
	for (Int oy = -1; oy <= 1; ++oy)
	{
		for (Int ox = -1; ox <= 1; ++ox)
		{
			const float px = cx + ox * TARGET_SIZE;
			const float py = cy + oy * TARGET_SIZE;
			if (px + reach < 0.0f || px - reach > TARGET_SIZE || py + reach < 0.0f || py - reach > TARGET_SIZE)
				continue;
			static const float corners[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
			Vertex quad[4];
			for (Int i = 0; i < 4; ++i)
			{
				const float lx = corners[i][0] * halfLength;
				const float ly = corners[i][1] * halfWidth;
				quad[i].x = px + lx * c - ly * s;
				quad[i].y = py + lx * s + ly * c;
				quad[i].z = 0.0f;
				quad[i].rhw = 1.0f;
				quad[i].color = color;
				quad[i].u = corners[i][0] * 0.5f + 0.5f;
				quad[i].v = corners[i][1] * 0.5f + 0.5f;
			}
			device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad, sizeof(Vertex));
		}
	}
#endif
}

//-------------------------------------------------------------------------------------------------
void W3DWaterDisturbance::update(const Coord3D& center)
{
#ifdef RTS_RENDERER_DX12
	const WaterRenderingSettings *settings = TheWaterRenderingSettings;
	const Bool enabled = TheW3DMaterials != nullptr && TheW3DMaterials->isAvailable() && TheW3DMaterials->hasWater()
		&& (settings == nullptr || settings->m_disturbance);
	if (!enabled)
	{
		if (m_active)
			reset();
		m_active = FALSE;
		return;
	}

	m_range = settings != nullptr ? max(settings->m_disturbanceRange, 64.0f) : 1024.0f;
	const Real fadeTime = settings != nullptr ? max(settings->m_disturbanceFadeTime, 0.05f) : 1.0f;
	const Real fade = powf(0.5f, STEP_SECONDS / fadeTime);
	if (!ensureResources(fade))
	{
		m_active = FALSE;
		return;
	}
	m_active = TRUE;
	m_transform[0] = 1.0f / m_range;
	m_transform[1] = center.x;
	m_transform[2] = center.y;
	m_transform[3] = m_range;

	// Whole steps since the last frame; without one the texture stays as it is.
	const UnsignedInt now = ::GetTickCount();
	if (m_lastTime != 0)
		m_stepTime += (now - m_lastTime) * 0.001f;
	m_lastTime = now;
	Int steps = (Int)(m_stepTime / STEP_SECONDS);
	if (steps <= 0 && !m_clear)
	{
		m_sustained.clear();
		return;
	}
	m_stepTime -= steps * STEP_SECONDS;
	if (steps > MAX_STEPS)
	{
		steps = MAX_STEPS;
		m_stepTime = 0.0f;
	}

	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	IDirect3DSurface8 *surface = nullptr;
	if (FAILED(m_target->GetSurfaceLevel(0, &surface)))
		return;
	DX8Wrapper::Set_Render_Target(surface);
	if (m_clear)
	{
		device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
		m_clear = FALSE;
	}

	device->SetRenderState(D3DRS_ZENABLE, FALSE);
	device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	device->SetRenderState(D3DRS_LIGHTING, FALSE);
	device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
	device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
	device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
	device->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
	device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
	device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	device->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	device->SetPixelShader(0);

	for (Int step = 0; step < steps; ++step)
	{
		// Everything fades: multiplied by the vertex colour.
		struct FadeVertex { float x, y, z, rhw; DWORD color; float u, v; };
		const DWORD level = (DWORD)(fade * 255.0f + 0.5f);
		const DWORD fadeColor = D3DCOLOR_ARGB(level, level, level, level);
		const FadeVertex quad[4] = {
			{ 0.0f, 0.0f, 0.0f, 1.0f, fadeColor, 0, 0 }, { (float)TARGET_SIZE, 0.0f, 0.0f, 1.0f, fadeColor, 1, 0 },
			{ (float)TARGET_SIZE, (float)TARGET_SIZE, 0.0f, 1.0f, fadeColor, 1, 1 }, { 0.0f, (float)TARGET_SIZE, 0.0f, 1.0f, fadeColor, 0, 1 } };
		device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ZERO);
		device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_SRCCOLOR);
		device->SetTexture(0, nullptr);
		device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
		device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
		device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
		device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
		device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad, sizeof(FadeVertex));

		// Then the shapes add up.
		device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
		device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
		device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
		device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
		device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
		device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
		device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
		device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
		for (size_t i = 0; i < m_sustained.size(); ++i)
			drawStamp(m_sustained[i], m_sustainedShapes[m_sustained[i].shape], 1.0f);
		if (step == 0)
		{
			for (size_t i = 0; i < m_once.size(); ++i)
				drawStamp(m_once[i], m_onceShapes[m_once[i].shape], 1.0f);
		}
		// Ripples grow a little each step. A ring adds up where it overlaps itself from step to step, so it is
		// drawn at the share of its thickness it moved, which builds up to about its strength and leaves a short
		// wave train behind.
		for (size_t i = 0; i < m_ripples.size(); )
		{
			Ripple& ripple = m_ripples[i];
			const Real t = (Real)ripple.age / (Real)ripple.frames;
			const Real radius = ripple.startRadius + (ripple.endRadius - ripple.startRadius) * t;
			const Real thickness = max(radius * 0.11f, 1.0f);
			const Real advance = fabsf(ripple.endRadius - ripple.startRadius) / (Real)ripple.frames;
			Stamp ring;
			ring.pos = ripple.pos;
			ring.angle = 0.0f;
			ring.length = ring.width = 2.0f * radius;
			ring.height = ripple.height;
			ring.foam = ripple.foam;
			ring.highlight = 0.0f;
			ring.shape = SHAPE_RING;
			drawStamp(ring, m_onceShapes[SHAPE_RING], (1.0f - t) * min(advance / thickness, 1.0f));
			if (++ripple.age >= ripple.frames)
			{
				ripple = m_ripples.back();
				m_ripples.pop_back();
			}
			else
			{
				++i;
			}
		}
	}

	m_sustained.clear();
	m_once.clear();
	surface->Release();
	DX8Wrapper::Set_Render_Target((IDirect3DSurface8 *)nullptr);
	DX8Wrapper::Invalidate_Cached_Render_States();
#endif
}
