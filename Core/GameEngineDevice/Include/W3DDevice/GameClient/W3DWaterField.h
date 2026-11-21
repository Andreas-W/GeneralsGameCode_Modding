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

// FILE: W3DWaterField.h //////////////////////////////////////////////////////
// What the Direct3D 12 water materials know about the map around the water, as a texture (WaterField, t8):
// per texel the terrain height, the signed distance to the shore (positive in the water) and the direction
// away from the shore. Built on the CPU when the map loads and again when the water or the terrain change.
// Also sets the shroud for the water materials (t9). With Direct3D 8 it does nothing.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

#include <vector>

struct IDirect3DTexture8;

class W3DWaterField
{
public:
	W3DWaterField();
	~W3DWaterField();

	void invalidate() { m_dirty = TRUE; }	///< the map was loaded: build it again
	void release();							///< frees the texture (device reset, map change)

	/// Builds it when it is out of date and sets it and the shroud for the materials; true when they can
	/// use it. Call once per frame before the water is drawn.
	Bool update();

	/// The material parameters: WaterFieldTransform (world xy -> uv: scale x, y, offset x, y) and
	/// ShroudTransform (the same for the shroud texture), 4 values each.
	const Real *getTransform() const { return m_transform; }
	const Real *getShroudTransform() const { return m_shroudTransform; }
	Bool hasShroud() const { return m_hasShroud; }

private:
	UnsignedInt hashWater() const;
	UnsignedInt hashTerrain() const;
	Bool build();
	void updateShroud();

	IDirect3DTexture8 *m_texture;
	Bool m_dirty;
	UnsignedInt m_waterHash;
	UnsignedInt m_terrainHash;
	UnsignedInt m_frame;			///< update calls, for checking the terrain now and then
	UnsignedInt m_lastBuildTime;	///< ms, to build at most twice a second while the water moves
	Real m_transform[4];
	Real m_shroudTransform[4];
	Bool m_hasShroud;
};
