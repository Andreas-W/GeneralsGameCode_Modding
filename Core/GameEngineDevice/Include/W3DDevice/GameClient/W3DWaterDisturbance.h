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

// FILE: W3DWaterDisturbance.h ////////////////////////////////////////////////
// The water disturbance of the Direct3D 12 water (WaterDisturbance, t11 of the water materials): a half float
// texture over the area around the camera, addressed by world position modulo its size (so moving the camera
// copies nothing). Each step (30 per second) it fades towards 0, then the shapes of ships and explosions are
// added into it. R = crest height, G = trough height, B = foam, A = highlight (submarines).
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameClient/WaterDisturbance.h"

#include <vector>

struct IDirect3DTexture8;

class W3DWaterDisturbance : public WaterDisturbance
{
public:
	W3DWaterDisturbance();
	virtual ~W3DWaterDisturbance() override;

	virtual void addStamp(const Coord3D& pos, Real angle, Real length, Real width, Real height, Real foam,
		Real highlight, Shape shape, Bool sustained) override;
	virtual void addRipple(const Coord3D& pos, Real startRadius, Real endRadius, Real height, Real foam,
		UnsignedInt frames) override;
	virtual Bool isActive() const override { return m_active; }
	virtual void reset() override;

	/// Before the frame is drawn: fades and adds what came in since. center = where the camera looks.
	void update(const Coord3D& center);
	/// The Direct3D resources go (device reset, end of the map).
	void release();

	/// The texture for the water materials, or null while switched off.
	IDirect3DTexture8 *getTexture() const { return m_active ? m_target : nullptr; }
	/// For the materials' DisturbanceTransform: 1 / range, the centre x and y, the range.
	const Real *getTransform() const { return m_transform; }

private:
	struct Stamp
	{
		Coord3D pos;
		Real angle, length, width;
		Real height, foam, highlight;
		Shape shape;
	};
	struct Ripple
	{
		Coord3D pos;
		Real startRadius, endRadius, height, foam;
		UnsignedInt frames, age;
	};

	Bool ensureResources(Real fade);
	void drawStamp(const Stamp& stamp, IDirect3DTexture8 *texture, Real scale);

	WaterDisturbance *m_previous;			///< TheWaterDisturbance before this one
	IDirect3DTexture8 *m_target;			///< the disturbance
	IDirect3DTexture8 *m_sustainedShapes[3];	///< per Shape, scaled to build up to their strength
	IDirect3DTexture8 *m_onceShapes[3];		///< per Shape, at full strength
	Real m_shapesFade;						///< the fade the sustained shapes were made for
	std::vector<Stamp> m_sustained;			///< this frame's
	std::vector<Stamp> m_once;				///< until the next step
	std::vector<Ripple> m_ripples;
	Real m_transform[4];
	Real m_range;
	UnsignedInt m_lastTime;
	Real m_stepTime;						///< seconds not yet stepped
	Bool m_active;
	Bool m_clear;
};
