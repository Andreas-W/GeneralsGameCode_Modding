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

// FILE: W3DShadowMap.h ////////////////////////////////////////////////////////
// Soft shadows from a depth map of the shadow volume casters, seen from the sun, instead of the
// stencil shadow volumes. Only with the Direct3D 12 renderer and a ShadowMap INI block
// (GameClient/PostProcess.h); otherwise the volumes draw as before.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Lib/BaseType.h"
#include "WWMath/vector3.h"

class CameraClass;

class W3DShadowMap
{
public:
	W3DShadowMap();
	~W3DShadowMap();

	Bool isActive();							///< shadow maps replace the volumes this frame
	void update(CameraClass *viewCamera);		///< draws the casters into the map; before the frame's Begin_Render
	Bool apply();								///< darkens the scene in place of the volumes; FALSE when it did not

	void setDebug(Bool debug) { m_debug = debug; }	///< shows the amount of shadow as grey
	Bool getDebug() const { return m_debug; }
	Bool wasRendered() const { return m_rendered; }
	Int getCasterCount() const { return m_casterCount; }
	Real getCoverage() const { return m_coverage; }	///< world units the map is wide
	Real getSunElevation();							///< degrees above the horizon, after MinSunElevation

private:
	Bool fitLightCamera(CameraClass *viewCamera, Int size);
	Bool getToLight(Vector3 &toLight);

	CameraClass *m_lightCamera;
	Bool m_rendered;			///< the map was drawn for this frame
	Bool m_debug;
	Int m_casterCount;
	Real m_coverage;
};

extern W3DShadowMap *TheW3DShadowMap;
