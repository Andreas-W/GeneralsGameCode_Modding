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

// FILE: WaterDisturbance.h //////////////////////////////////////////////////
// What moves the water surface besides its own waves: ships, explosions. Client side only, nothing of
// it reaches the game logic. The Direct3D 12 water draws it (ripples, foam, a little height); without
// that water every call does nothing.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"

class WaterDisturbance
{
public:
	enum Shape
	{
		SHAPE_HULL,		///< a boat's hull: a trough along it, crests at bow and stern, foam around it
		SHAPE_DISC,		///< a soft round bump with foam
		SHAPE_RING,		///< a ring: crest on it, trough inside
	};

	virtual ~WaterDisturbance() {}

	/** A shape on the water at a world position, angle in radians, length along the angle and width across,
		* in world units. height (crest and trough), foam and highlight are strengths, usually 0..1.
		* sustained: drawn every frame while it lasts (a hull), so it builds up to the strength instead of
		* adding it every frame; else drawn once (a splash of a wake trail). */
	virtual void addStamp(const Coord3D& pos, Real angle, Real length, Real width, Real height, Real foam,
		Real highlight, Shape shape, Bool sustained) {}

	/** A ring that grows from startRadius to endRadius over the given number of logic frames and dies away. */
	virtual void addRipple(const Coord3D& pos, Real startRadius, Real endRadius, Real height, Real foam,
		UnsignedInt frames) {}

	/** Whether anything added shows (the Direct3D 12 water is there and the effect is switched on). */
	virtual Bool isActive() const { return FALSE; }

	virtual void reset() {}
};

/// Never null: a disturbance that does nothing until the Direct3D 12 water sets its own.
extern WaterDisturbance *TheWaterDisturbance;
