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

// FILE: W3DWaterField.cpp ////////////////////////////////////////////////////
// The water field texture of the Direct3D 12 water materials.
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DWaterField.h"

#include "Common/MapObject.h"
#include "GameClient/PostProcess.h"
#include "GameLogic/PolygonTrigger.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DMaterials.h"
#include "W3DDevice/GameClient/W3DShroud.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "WW3D2/dx8wrapper.h"
#include "WW3D2/texture.h"

#include <math.h>

#ifdef RTS_RENDERER_DX12
#include <d3d8on12_ext.h>
#endif

static const Int MAX_FIELD_SIZE = 2048;
static const Int TERRAIN_CHECK_FRAMES = 30;		///< the heightmap is hashed this often, it is large
static const UnsignedInt MIN_REBUILD_MS = 500;	///< while the water level moves
static const float FAR_AWAY = 1e20f;

//-------------------------------------------------------------------------------------------------
static unsigned short toHalf(float value)
{
	union { float f; unsigned int u; } bits;
	bits.f = value;
	const unsigned int sign = (bits.u >> 16) & 0x8000;
	const int exponent = (int)((bits.u >> 23) & 0xff) - 127 + 15;
	const unsigned int mantissa = bits.u & 0x7fffff;
	if (exponent <= 0)
		return (unsigned short)sign;
	if (exponent >= 31)
		return (unsigned short)(sign | 0x7bff);	// the largest finite half
	return (unsigned short)(sign | (exponent << 10) | (mantissa >> 13));
}

//-------------------------------------------------------------------------------------------------
static UnsignedInt hashBytes(UnsignedInt hash, const void *data, size_t size)
{
	const unsigned char *bytes = (const unsigned char *)data;
	for (size_t i = 0; i < size; ++i)
		hash = (hash ^ bytes[i]) * 16777619u;
	return hash;
}

//-------------------------------------------------------------------------------------------------
/** Squared distance to the nearest point where f is 0, in one dimension (Felzenszwalb and Huttenlocher,
	* "Distance Transforms of Sampled Functions"). f holds 0 or FAR_AWAY; d gets the result. */
//-------------------------------------------------------------------------------------------------
static void distance1D(const float *f, Int n, float *d, Int *v, float *z)
{
	Int k = 0;
	v[0] = 0;
	z[0] = -FAR_AWAY;
	z[1] = FAR_AWAY;
	for (Int q = 1; q < n; ++q)
	{
		float s;
		for (;;)
		{
			const Int p = v[k];
			s = ((f[q] + (float)q * q) - (f[p] + (float)p * p)) / (2.0f * (q - p));
			if (s <= z[k] && k > 0)
			{
				--k;
				continue;
			}
			break;
		}
		if (s <= z[k])
		{
			// Only possible with k == 0: q replaces the first parabola.
			v[0] = q;
			z[0] = -FAR_AWAY;
			z[1] = FAR_AWAY;
			continue;
		}
		++k;
		v[k] = q;
		z[k] = s;
		z[k + 1] = FAR_AWAY;
	}
	k = 0;
	for (Int q = 0; q < n; ++q)
	{
		while (z[k + 1] < (float)q)
			++k;
		const float delta = (float)(q - v[k]);
		d[q] = delta * delta + f[v[k]];
	}
}

//-------------------------------------------------------------------------------------------------
/** Squared distance in texels of each texel to the nearest one where target is set. */
//-------------------------------------------------------------------------------------------------
static void distance2D(const std::vector<UnsignedByte>& target, Int width, Int height, std::vector<float>& result)
{
	const Int n = max(width, height);
	std::vector<float> f(n), d(n), z(n + 1);
	std::vector<Int> v(n);
	result.resize((size_t)width * height);
	for (Int x = 0; x < width; ++x)
	{
		for (Int y = 0; y < height; ++y)
			f[y] = target[(size_t)y * width + x] ? 0.0f : FAR_AWAY;
		distance1D(&f[0], height, &d[0], &v[0], &z[0]);
		for (Int y = 0; y < height; ++y)
			result[(size_t)y * width + x] = d[y];
	}
	for (Int y = 0; y < height; ++y)
	{
		float *row = &result[(size_t)y * width];
		for (Int x = 0; x < width; ++x)
			f[x] = row[x];
		distance1D(&f[0], width, &d[0], &v[0], &z[0]);
		for (Int x = 0; x < width; ++x)
			row[x] = d[x];
	}
}

//-------------------------------------------------------------------------------------------------
W3DWaterField::W3DWaterField() :
	m_texture(nullptr),
	m_dirty(TRUE),
	m_waterHash(0),
	m_terrainHash(0),
	m_frame(0),
	m_lastBuildTime(0),
	m_hasShroud(FALSE)
{
	for (Int i = 0; i < 4; ++i)
		m_transform[i] = m_shroudTransform[i] = 0.0f;
}

//-------------------------------------------------------------------------------------------------
W3DWaterField::~W3DWaterField()
{
	release();
}

//-------------------------------------------------------------------------------------------------
void W3DWaterField::release()
{
	if (TheW3DMaterials != nullptr)
	{
		IDirect3DBaseTexture8 *const none[2] = { nullptr, nullptr };
		TheW3DMaterials->setSharedTextures(0, 2, none);
	}
	if (m_texture != nullptr)
	{
		m_texture->Release();
		m_texture = nullptr;
	}
	m_dirty = TRUE;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt W3DWaterField::hashWater() const
{
	UnsignedInt hash = 2166136261u;
	for (PolygonTrigger *trigger = PolygonTrigger::getFirstPolygonTrigger(); trigger; trigger = trigger->getNext())
	{
		if (!trigger->isWaterArea())
			continue;
		const Int count = trigger->getNumPoints();
		const Bool river = trigger->isRiver();
		hash = hashBytes(hash, &count, sizeof(count));
		hash = hashBytes(hash, &river, sizeof(river));
		for (Int i = 0; i < count; ++i)
			hash = hashBytes(hash, trigger->getPoint(i), sizeof(ICoord3D));
	}
	return hash;
}

//-------------------------------------------------------------------------------------------------
UnsignedInt W3DWaterField::hashTerrain() const
{
	if (TheTerrainRenderObject == nullptr || TheTerrainRenderObject->getMap() == nullptr)
		return 0;
	WorldHeightMap *map = TheTerrainRenderObject->getMap();
	const Int width = map->getXExtent();
	const Int height = map->getYExtent();
	UnsignedInt hash = hashBytes(2166136261u, &width, sizeof(width));
	hash = hashBytes(hash, &height, sizeof(height));
	return hashBytes(hash, map->getDataPtr(), (size_t)width * height * sizeof(*map->getDataPtr()));
}

//-------------------------------------------------------------------------------------------------
/** The shroud texture and how world positions map onto it, as FlatShroudTextureShader draws it. */
//-------------------------------------------------------------------------------------------------
void W3DWaterField::updateShroud()
{
	m_hasShroud = FALSE;
	W3DShroud *shroud = TheTerrainRenderObject != nullptr ? TheTerrainRenderObject->getShroud() : nullptr;
	if (shroud == nullptr || shroud->getShroudTexture() == nullptr)
		return;
	const Real cellWidth = shroud->getCellWidth();
	const Real cellHeight = shroud->getCellHeight();
	const Real offsetX = -(Real)shroud->getDrawOriginX() + cellWidth;
	const Real offsetY = -(Real)shroud->getDrawOriginY() + cellHeight;
	const Real scaleX = 1.0f / (cellWidth * shroud->getTextureWidth());
	const Real scaleY = 1.0f / (cellHeight * shroud->getTextureHeight());
	m_shroudTransform[0] = scaleX;
	m_shroudTransform[1] = scaleY;
	m_shroudTransform[2] = offsetX * scaleX;
	m_shroudTransform[3] = offsetY * scaleY;
	m_hasShroud = TRUE;
}

//-------------------------------------------------------------------------------------------------
Bool W3DWaterField::update()
{
#ifdef RTS_RENDERER_DX12
	if (TheW3DMaterials == nullptr || !TheW3DMaterials->isAvailable() || TheTerrainRenderObject == nullptr
		|| TheTerrainRenderObject->getMap() == nullptr)
		return FALSE;

	const UnsignedInt waterHash = hashWater();
	UnsignedInt terrainHash = m_terrainHash;
	if (m_dirty || m_texture == nullptr || (m_frame++ % TERRAIN_CHECK_FRAMES) == 0)
		terrainHash = hashTerrain();
	if (m_dirty || m_texture == nullptr || waterHash != m_waterHash || terrainHash != m_terrainHash)
	{
		const UnsignedInt now = ::GetTickCount();
		if (m_dirty || m_texture == nullptr || now - m_lastBuildTime >= MIN_REBUILD_MS)
		{
			build();
			m_waterHash = waterHash;
			m_terrainHash = terrainHash;
			m_lastBuildTime = now;
			m_dirty = FALSE;
		}
	}

	updateShroud();
	IDirect3DBaseTexture8 *textures[2];
	textures[0] = m_texture;
	textures[1] = nullptr;
	if (m_hasShroud)
	{
		TextureClass *shroudTexture = TheTerrainRenderObject->getShroud()->getShroudTexture();
		if (!shroudTexture->Is_Initialized())
			shroudTexture->Init();
		textures[1] = shroudTexture->Peek_D3D_Base_Texture();
		m_hasShroud = textures[1] != nullptr;
	}
	TheW3DMaterials->setSharedTextures(0, 2, textures);
	return m_texture != nullptr;
#else
	return FALSE;
#endif
}

//-------------------------------------------------------------------------------------------------
/** Terrain height, signed distance to the shore and the direction away from it, over the whole heightmap
	* (with its border). A texel is water where a water area covers it and the terrain is below its surface. */
//-------------------------------------------------------------------------------------------------
Bool W3DWaterField::build()
{
#ifdef RTS_RENDERER_DX12
	if (m_texture != nullptr)
	{
		m_texture->Release();
		m_texture = nullptr;
	}
	WorldHeightMap *map = TheTerrainRenderObject->getMap();
	const Int border = map->getBorderSizeInline();
	const Real originX = -(Real)border * MAP_XY_FACTOR;
	const Real originY = -(Real)border * MAP_XY_FACTOR;
	const Real extentX = (Real)(map->getXExtent() - 1) * MAP_XY_FACTOR;
	const Real extentY = (Real)(map->getYExtent() - 1) * MAP_XY_FACTOR;
	if (extentX <= 0.0f || extentY <= 0.0f)
		return FALSE;

	const WaterRenderingSettings *settings = TheWaterRenderingSettings;
	const Int resolution = settings != nullptr ? max(1, min(settings->m_shoreFieldResolution, 8)) : 2;
	const Real range = settings != nullptr ? max(settings->m_shoreFieldRange, 1.0f) : 100.0f;
	Real texel = MAP_XY_FACTOR / (Real)resolution;
	texel = max(texel, max(extentX, extentY) / (Real)(MAX_FIELD_SIZE - 1));
	const Int width = min(MAX_FIELD_SIZE, (Int)(extentX / texel) + 1);
	const Int height = min(MAX_FIELD_SIZE, (Int)(extentY / texel) + 1);
	const size_t count = (size_t)width * height;

	// Terrain height and the water surface above each texel.
	std::vector<float> terrain(count);
	std::vector<float> surface(count, -FAR_AWAY);
	for (Int y = 0; y < height; ++y)
		for (Int x = 0; x < width; ++x)
			terrain[(size_t)y * width + x] = TheTerrainRenderObject->getHeightMapHeight(originX + x * texel, originY + y * texel, nullptr);
	for (PolygonTrigger *trigger = PolygonTrigger::getFirstPolygonTrigger(); trigger; trigger = trigger->getNext())
	{
		if (!trigger->isWaterArea() || trigger->getNumPoints() < 3)
			continue;
		Int minX = trigger->getPoint(0)->x, maxX = minX, minY = trigger->getPoint(0)->y, maxY = minY;
		for (Int i = 1; i < trigger->getNumPoints(); ++i)
		{
			minX = min(minX, trigger->getPoint(i)->x);
			maxX = max(maxX, trigger->getPoint(i)->x);
			minY = min(minY, trigger->getPoint(i)->y);
			maxY = max(maxY, trigger->getPoint(i)->y);
		}
		const float level = (float)trigger->getPoint(0)->z;
		const Int x0 = max(0, (Int)floorf((minX - originX) / texel));
		const Int x1 = min(width - 1, (Int)ceilf((maxX - originX) / texel));
		const Int y0 = max(0, (Int)floorf((minY - originY) / texel));
		const Int y1 = min(height - 1, (Int)ceilf((maxY - originY) / texel));
		for (Int y = y0; y <= y1; ++y)
		{
			for (Int x = x0; x <= x1; ++x)
			{
				float& top = surface[(size_t)y * width + x];
				if (level <= top)
					continue;
				ICoord3D point;
				point.x = REAL_TO_INT(originX + x * texel);
				point.y = REAL_TO_INT(originY + y * texel);
				point.z = 0;
				if (trigger->pointInTrigger(point))
					top = level;
			}
		}
	}

	std::vector<UnsignedByte> water(count), dry(count);
	for (size_t i = 0; i < count; ++i)
	{
		water[i] = surface[i] > terrain[i] ? 1 : 0;
		dry[i] = water[i] ? 0 : 1;
	}
	std::vector<float> toDry, toWater;
	distance2D(dry, width, height, toDry);
	distance2D(water, width, height, toWater);
	std::vector<float> signedDistance(count);
	for (size_t i = 0; i < count; ++i)
	{
		const float distance = water[i] ? sqrtf(toDry[i]) * texel : -sqrtf(toWater[i]) * texel;
		signedDistance[i] = max(-range, min(range, distance));
	}

	IDirect3DDevice8 *device = DX8Wrapper::_Get_D3D_Device8();
	if (device == nullptr || FAILED(device->CreateTexture(width, height, 1, 0, D3DFMT_A16B16G16R16F_D3D8ON12, D3DPOOL_MANAGED, &m_texture)))
	{
		m_texture = nullptr;
		return FALSE;
	}
	D3DLOCKED_RECT locked;
	if (FAILED(m_texture->LockRect(0, &locked, nullptr, 0)))
	{
		m_texture->Release();
		m_texture = nullptr;
		return FALSE;
	}
	for (Int y = 0; y < height; ++y)
	{
		unsigned short *row = (unsigned short *)((unsigned char *)locked.pBits + y * locked.Pitch);
		for (Int x = 0; x < width; ++x)
		{
			const size_t i = (size_t)y * width + x;
			// Away from the shore: the slope of the distance.
			const float dx = signedDistance[y * width + min(x + 1, width - 1)] - signedDistance[y * width + max(x - 1, 0)];
			const float dy = signedDistance[min(y + 1, height - 1) * width + x] - signedDistance[max(y - 1, 0) * width + x];
			const float length = sqrtf(dx * dx + dy * dy);
			row[x * 4 + 0] = toHalf(terrain[i]);
			row[x * 4 + 1] = toHalf(signedDistance[i]);
			row[x * 4 + 2] = toHalf(length > 1e-4f ? dx / length : 0.0f);
			row[x * 4 + 3] = toHalf(length > 1e-4f ? dy / length : 0.0f);
		}
	}
	m_texture->UnlockRect(0);

	// World xy to the texel centres.
	m_transform[0] = 1.0f / (texel * width);
	m_transform[1] = 1.0f / (texel * height);
	m_transform[2] = 0.5f / width - originX * m_transform[0];
	m_transform[3] = 0.5f / height - originY * m_transform[1];
	DEBUG_LOG(("Water field: %dx%d texels of %g world units", width, height, texel));
	return TRUE;
#else
	return FALSE;
#endif
}
