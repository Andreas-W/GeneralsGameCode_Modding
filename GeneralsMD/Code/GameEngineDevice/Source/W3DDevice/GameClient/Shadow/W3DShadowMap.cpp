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

// FILE: W3DShadowMap.cpp //////////////////////////////////////////////////////
// Soft shadows from a depth map of the shadow volume casters, drawn by the Direct3D 12 renderer.
////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/W3DShadowMap.h"

#include "Common/GlobalData.h"
#include "Common/MapObject.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableInfo.h"
#include "GameClient/PostProcess.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"
#include "W3DDevice/GameClient/W3DProjectedShadow.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "W3DDevice/GameClient/W3DTreeBuffer.h"
#include "W3DDevice/GameClient/W3DVolumetricShadow.h"
#include "W3DDevice/GameClient/WorldHeightMap.h"
#include "WW3D2/camera.h"
#include "WW3D2/dx8indexbuffer.h"
#include "WW3D2/dx8renderer.h"
#include "WW3D2/dx8vertexbuffer.h"
#include "WW3D2/dx8wrapper.h"
#include "WWMath/colmath.h"
#include "WW3D2/rinfo.h"
#include "WW3D2/rendobj.h"
#include "WW3D2/ww3d.h"

#ifdef RTS_RENDERER_DX12
#include <d3d8on12_ext.h>
#endif

// The light camera looks from this far towards the visible area, like the sun of the shadow volumes;
// a perspective camera that far away is nearly parallel and culls correctly, which orthographic
// cameras do not (FrustumClass::Init).
static const Real SUN_DISTANCE = 10000.0f;
// How high above the visible terrain casters may be and still put their shadow into view.
static const Real CASTER_HEIGHT = 200.0f;
// The map is never wider than this, so that far zoom does not blur the shadows away.
static const Real MAX_HALF_COVERAGE = 4000.0f;
// Terrain cells per side of a chunk of the terrain mesh.
static const Int TERRAIN_CHUNK_CELLS = 64;

W3DShadowMap *TheW3DShadowMap = nullptr;

//-------------------------------------------------------------------------------------------------
W3DShadowMap::W3DShadowMap() :
	m_lightCamera(nullptr),
	m_rendered(FALSE),
	m_debug(FALSE),
	m_casterCount(0),
	m_coverage(0.0f),
	m_terrainIndices(nullptr),
	m_terrainMap(nullptr)
{
	m_lightCamera = NEW_REF( CameraClass, () );
}

//-------------------------------------------------------------------------------------------------
W3DShadowMap::~W3DShadowMap()
{
	releaseTerrain();
	REF_PTR_RELEASE(m_lightCamera);
}

//-------------------------------------------------------------------------------------------------
void W3DShadowMap::releaseTerrain()
{
	for (size_t i = 0; i < m_terrainChunks.size(); ++i)
		REF_PTR_RELEASE(m_terrainChunks[i].vertices);
	m_terrainChunks.clear();
	REF_PTR_RELEASE(m_terrainIndices);
	m_terrainMap = nullptr;
}

//-------------------------------------------------------------------------------------------------
/** The whole height map, border included, as chunks of positions; the terrain renderer only holds
	* the part around the view, and hills outside it still cast shadows into it. */
//-------------------------------------------------------------------------------------------------
void W3DShadowMap::buildTerrain(WorldHeightMap *map)
{
	releaseTerrain();
	const Int width = map->getXExtent();
	const Int height = map->getYExtent();
	const Int border = map->getBorderSizeInline();
	if (width < 2 || height < 2)
		return;
	const Int side = TERRAIN_CHUNK_CELLS + 1;

	m_terrainIndices = NEW_REF(DX8IndexBufferClass, (TERRAIN_CHUNK_CELLS * TERRAIN_CHUNK_CELLS * 6));
	{
		DX8IndexBufferClass::WriteLockClass lock(m_terrainIndices);
		UnsignedShort *index = lock.Get_Index_Array();
		for (Int y = 0; y < TERRAIN_CHUNK_CELLS; ++y)
		{
			for (Int x = 0; x < TERRAIN_CHUNK_CELLS; ++x)
			{
				const UnsignedShort corner = (UnsignedShort)(y * side + x);
				*index++ = corner;
				*index++ = corner + 1;
				*index++ = corner + side;
				*index++ = corner + 1;
				*index++ = corner + side + 1;
				*index++ = corner + side;
			}
		}
	}

	const HeightSampleType *data = map->getDataPtr();
	for (Int originY = 0; originY < height - 1; originY += TERRAIN_CHUNK_CELLS)
	{
		for (Int originX = 0; originX < width - 1; originX += TERRAIN_CHUNK_CELLS)
		{
			TerrainChunk chunk;
			chunk.vertices = NEW_REF(DX8VertexBufferClass, (DX8_FVF_XYZ, side * side, DX8VertexBufferClass::USAGE_DEFAULT));
			Vector3 low(1e30f, 1e30f, 1e30f);
			Vector3 high(-1e30f, -1e30f, -1e30f);
			{
				DX8VertexBufferClass::WriteLockClass lock(chunk.vertices);
				Vector3 *vertex = (Vector3 *)lock.Get_Vertex_Array();
				for (Int y = 0; y < side; ++y)
				{
					const Int mapY = min(originY + y, height - 1);
					for (Int x = 0; x < side; ++x)
					{
						const Int mapX = min(originX + x, width - 1);
						vertex->Set((mapX - border) * MAP_XY_FACTOR, (mapY - border) * MAP_XY_FACTOR, data[mapX + mapY * width] * MAP_HEIGHT_SCALE);
						low.Update_Min(*vertex);
						high.Update_Max(*vertex);
						++vertex;
					}
				}
			}
			chunk.box.Init_Min_Max(low, high);
			m_terrainChunks.push_back(chunk);
		}
	}
	m_terrainMap = map;
}

//-------------------------------------------------------------------------------------------------
void W3DShadowMap::drawTerrain()
{
	if (TheTerrainRenderObject == nullptr || TheTerrainRenderObject->getMap() == nullptr)
		return;
	if (m_terrainMap != TheTerrainRenderObject->getMap())
		buildTerrain(TheTerrainRenderObject->getMap());
	if (m_terrainChunks.empty())
		return;

	// Both sides, so that hills shadow the sun side too where the light grazes them.
	ShaderClass shader = ShaderClass::_PresetOpaqueShader;
	shader.Set_Cull_Mode(ShaderClass::CULL_MODE_DISABLE);
	DX8Wrapper::Set_Shader(shader);
	DX8Wrapper::Set_Texture(0, nullptr);
	DX8Wrapper::Set_Texture(1, nullptr);
	DX8Wrapper::Set_Material(nullptr);
	DX8Wrapper::Set_Transform(D3DTS_WORLD, Matrix3D(true));
	DX8Wrapper::Set_Index_Buffer(m_terrainIndices, 0);
	const FrustumClass &frustum = m_lightCamera->Get_Frustum();
	const Int side = TERRAIN_CHUNK_CELLS + 1;
	for (size_t i = 0; i < m_terrainChunks.size(); ++i)
	{
		const TerrainChunk &chunk = m_terrainChunks[i];
		if (CollisionMath::Overlap_Test(frustum, chunk.box) == CollisionMath::OUTSIDE)
			continue;
		DX8Wrapper::Set_Vertex_Buffer(chunk.vertices);
		DX8Wrapper::Draw_Triangles(0, TERRAIN_CHUNK_CELLS * TERRAIN_CHUNK_CELLS * 2, 0, side * side);
	}
	DX8Wrapper::Set_Vertex_Buffer(nullptr);
	DX8Wrapper::Set_Index_Buffer(nullptr, 0);
}

//-------------------------------------------------------------------------------------------------
Bool W3DShadowMap::isActive()
{
	return TheShadowMapSettings != nullptr && TheShadowMapSettings->m_enabled && TheGlobalData->m_useShadowVolumes
		&& DX8Wrapper::Get_Shadow_Mapper() != nullptr;
}

//-------------------------------------------------------------------------------------------------
/** The direction towards the sun of the shadow volumes, raised to MinSunElevation. */
//-------------------------------------------------------------------------------------------------
Bool W3DShadowMap::drawsTrees()
{
	return isActive() && TheShadowMapSettings->m_trees;
}

//-------------------------------------------------------------------------------------------------
Bool W3DShadowMap::isTree(RenderObjClass *robj)
{
	const DrawableInfo *info = robj ? (const DrawableInfo *)robj->Get_User_Data() : nullptr;
	return info != nullptr && info->m_drawable != nullptr && info->m_drawable->isKindOf(KINDOF_SHRUBBERY);
}

//-------------------------------------------------------------------------------------------------
Bool W3DShadowMap::getToLight(Vector3 &toLight)
{
	if (TheW3DShadowManager == nullptr)
		return FALSE;
	toLight = TheW3DShadowManager->getLightPosWorld(0);
	if (toLight.Length2() < 1.0f)
		return FALSE;
	toLight.Normalize();
	const Real minElevation = TheShadowMapSettings != nullptr ? TheShadowMapSettings->m_minSunElevation : 0.0f;
	if (minElevation > 0.0f && toLight.Z < sinf(DEG_TO_RADF(minElevation)))
	{
		const Real flat = sqrtf(toLight.X * toLight.X + toLight.Y * toLight.Y);
		const Real scale = flat > 0.0001f ? cosf(DEG_TO_RADF(minElevation)) / flat : 0.0f;
		toLight.Set(toLight.X * scale, toLight.Y * scale, sinf(DEG_TO_RADF(minElevation)));
	}
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
Real W3DShadowMap::getSunElevation()
{
	Vector3 toLight;
	if (!getToLight(toLight))
		return 0.0f;
	return RAD_TO_DEGF(asinf(max(-1.0f, min(1.0f, toLight.Z))));
}

//-------------------------------------------------------------------------------------------------
/** Points the light camera at the visible terrain, raised by the casters' height, so that the map
	* holds every shadow that can fall into view. Its centre moves in whole texels, so that the
	* shadows do not shimmer while the view scrolls. */
//-------------------------------------------------------------------------------------------------
Bool W3DShadowMap::fitLightCamera(CameraClass *viewCamera, Int size)
{
	Vector3 toLight;
	if (TheTerrainRenderObject == nullptr || !getToLight(toLight))
		return FALSE;
	AABoxClass box;
	if (!TheTerrainRenderObject->getMaximumVisibleBox(viewCamera->Get_Frustum(), &box, TRUE))
		return FALSE;
	const Vector3 up = fabs(toLight.Z) < 0.99f ? Vector3(0.0f, 0.0f, 1.0f) : Vector3(0.0f, 1.0f, 0.0f);
	Vector3 xAxis;
	Vector3::Cross_Product(up, toLight, &xAxis);
	xAxis.Normalize();
	Vector3 yAxis;
	Vector3::Cross_Product(toLight, xAxis, &yAxis);

	Real minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f, minZ = 1e30f, maxZ = -1e30f;
	for (Int corner = 0; corner < 8; ++corner)
	{
		const Vector3 point(box.Center.X + ((corner & 1) ? box.Extent.X : -box.Extent.X),
			box.Center.Y + ((corner & 2) ? box.Extent.Y : -box.Extent.Y),
			(corner & 4) ? box.Center.Z + box.Extent.Z + CASTER_HEIGHT : box.Center.Z - box.Extent.Z);
		const Real x = Vector3::Dot_Product(point, xAxis);
		const Real y = Vector3::Dot_Product(point, yAxis);
		const Real z = Vector3::Dot_Product(point, toLight);
		minX = min(minX, x); maxX = max(maxX, x);
		minY = min(minY, y); maxY = max(maxY, y);
		minZ = min(minZ, z); maxZ = max(maxZ, z);
	}

	// Coverage in steps of 64 units, so that it does not change with every small camera move.
	Real half = max(maxX - minX, maxY - minY) * 0.5f;
	half = min(ceilf(half / 64.0f) * 64.0f, MAX_HALF_COVERAGE);
	const Real texel = 2.0f * half / size;
	const Real centerX = floorf((minX + maxX) * 0.5f / texel + 0.5f) * texel;
	const Real centerY = floorf((minY + maxY) * 0.5f / texel + 0.5f) * texel;
	const Vector3 eye = xAxis * centerX + yAxis * centerY + toLight * (maxZ + SUN_DISTANCE);

	Matrix3D transform;
	transform.Set(xAxis, yAxis, toLight, eye);
	m_lightCamera->Set_Transform(transform);
	// The near plane leaves room for casters above the assumed height, such as aircraft.
	m_lightCamera->Set_Clip_Planes(SUN_DISTANCE - 1000.0f, SUN_DISTANCE + (maxZ - minZ) + 100.0f);
	const Real extent = half / SUN_DISTANCE;
	m_lightCamera->Set_View_Plane(Vector2(-extent, -extent), Vector2(extent, extent));
	m_coverage = 2.0f * half;
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
void W3DShadowMap::update(CameraClass *viewCamera)
{
	m_rendered = FALSE;
	m_casterCount = 0;
#ifdef RTS_RENDERER_DX12
	if (viewCamera == nullptr || TheW3DVolumetricShadowManager == nullptr || !isActive())
		return;
	const ShadowMapSettings &settings = *TheShadowMapSettings;
	const Int size = max(256, min(settings.m_size, 8192));
	if (!fitLightCamera(viewCamera, size))
		return;

	ID3D8On12ShadowMap *mapper = DX8Wrapper::Get_Shadow_Mapper();
	if (FAILED(mapper->Begin_Shadow_Map(size, settings.m_depthBias, settings.m_slopeBias)))
		return;

	// The matrices exactly as the renderer gets them, for finding points in the map later.
	m_lightCamera->Apply();
	DX8Wrapper::Apply_Render_State_Changes();
	D3DMATRIX view;
	D3DMATRIX projection;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, projection);

	if (WW3D::Begin_Render(false, true, Vector3(0.0f, 0.0f, 0.0f)) == WW3D_ERROR_OK)
	{
		RenderInfoClass rinfo(*m_lightCamera);
		m_lightCamera->Apply();
		TheDX8MeshRenderer.Set_Camera(&rinfo.Camera);
		WW3D::Set_Depth_Only_Pass(true);
		// The casters of the shadow volumes, also those just outside the view whose shadows reach into
		// it; their meshes cull themselves against the light camera.
		for (W3DVolumetricShadow *shadow = TheW3DVolumetricShadowManager->m_shadowList; shadow; shadow = shadow->m_next)
		{
			if (shadow->m_isEnabled && !shadow->m_isInvisibleEnabled && shadow->m_robj != nullptr)
			{
				shadow->m_robj->Render(rinfo);
				++m_casterCount;
			}
		}
		if (settings.m_terrain)
			drawTerrain();
		if (settings.m_trees)
		{
			// Trees are models with decal shadows, or drawn by the terrain's tree buffer.
			if (TheW3DProjectedShadowManager != nullptr)
			{
				for (W3DProjectedShadow *shadow = TheW3DProjectedShadowManager->m_shadowList; shadow; shadow = shadow->m_next)
				{
					if ((shadow->m_type & SHADOW_DECAL) && shadow->m_isEnabled && !shadow->m_isInvisibleEnabled && isTree(shadow->m_robj))
					{
						shadow->m_robj->Render(rinfo);
						++m_casterCount;
					}
				}
			}
			if (TheTerrainRenderObject != nullptr && TheTerrainRenderObject->getTreeBuffer() != nullptr)
				TheTerrainRenderObject->getTreeBuffer()->drawShadowCasters();
		}
		WW3D::Flush(rinfo);
		WW3D::Set_Depth_Only_Pass(false);
		WW3D::End_Render(false);
		m_rendered = TRUE;
	}
	mapper->End_Shadow_Map(&view, &projection);
#endif
}

//-------------------------------------------------------------------------------------------------
/** In the frame's scene, where the stencil shadows would be drawn: after the opaque objects and trees,
	* before water, decals, translucent objects and particles. */
//-------------------------------------------------------------------------------------------------
Bool W3DShadowMap::apply()
{
#ifdef RTS_RENDERER_DX12
	if (!m_rendered || !isActive())
		return FALSE;
	const ShadowMapSettings &settings = *TheShadowMapSettings;

	// The scene camera is still applied; the objects drawn since only changed the world transform.
	D3DMATRIX view;
	D3DMATRIX projection;
	DX8Wrapper::_Get_DX8_Transform(D3DTS_VIEW, view);
	DX8Wrapper::_Get_DX8_Transform(D3DTS_PROJECTION, projection);

	D3D8ON12_SHADOW_PARAMS params;
	memset(&params, 0, sizeof(params));
	// The map's shadow color, alpha included, as the stencil shadows multiply with it.
	const UnsignedInt color = TheW3DShadowManager->getShadowColor();
	const Int shifts[4] = { 16, 8, 0, 24 };
	for (Int i = 0; i < 4; ++i)
	{
		const Real channel = ((color >> shifts[i]) & 0xff) / 255.0f;
		params.color[i] = max(0.0f, min(1.0f, 1.0f - (1.0f - channel) * settings.m_darkness));
	}
	Vector3 toLight;
	getToLight(toLight);
	params.toLight[0] = toLight.X;
	params.toLight[1] = toLight.Y;
	params.toLight[2] = toLight.Z;
	params.bias = settings.m_bias;
	params.softness = settings.m_softness;
	params.debug = m_debug;
	return SUCCEEDED(DX8Wrapper::Get_Shadow_Mapper()->Apply_Shadows(&params, &view, &projection));
#else
	return FALSE;
#endif
}
