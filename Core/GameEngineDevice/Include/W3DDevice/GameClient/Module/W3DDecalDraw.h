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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: W3DDecalDraw.h //////////////////////////////////////////////////////////////////////////
// Author: Andi W, May 26
// Desc:   Draw module for Decal FXNugget
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/DrawModule.h"
#include "WW3D2/line3d.h"
#include "W3DDevice/GameClient/W3DShadow.h"
#include "WW3D2/boxrobj.h"

//-------------------------------------------------------------------------------------------------
class W3DDecalDrawModuleData : public ModuleData
{
public:

	AsciiString	m_textureName;
	Real m_opacity;		///< value between 0 and 1
	UnsignedInt m_color;		///< color in ARGB format. (Alpha is ignored).
	// UnsignedInt m_lifetime;
	UnsignedInt m_fadeOutTime;
	UnsignedInt m_fadeInTime;
	ShadowType m_type;		/// type of projection
	Real m_decalSizeX;		/// 1/(world space extent of texture in x direction)
	Real m_decalSizeY;		/// 1/(world space extent of texture in y direction)

	W3DDecalDrawModuleData();
	~W3DDecalDrawModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);
	// ugh, hack
	virtual const W3DDecalDrawModuleData* getAsW3DDecalDrawModuleData() const { return this; }
};

//-------------------------------------------------------------------------------------------------
/** W3D prop draw */
//-------------------------------------------------------------------------------------------------
class W3DDecalDraw : public DrawModule //, public DecalDrawInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( W3DDecalDraw, "W3DDecalDraw" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( W3DDecalDraw, W3DDecalDrawModuleData )

public:

	W3DDecalDraw( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void doDrawModule(const Matrix3D* transformMtx);
	virtual void setShadowsEnabled(Bool enable) { }
	virtual void releaseShadows(void) {};	///< we don't care about preserving temporary shadows.
	virtual void allocateShadows(void) {};	///< we don't care about preserving temporary shadows.
	virtual void setFullyObscuredByShroud(Bool fullyObscured);
	virtual void reactToTransformChange(const Matrix3D* oldMtx, const Coord3D* oldPos, Real oldAngle);
	virtual void reactToGeometryChange() { }


	//virtual DecalDrawInterface* getDecalDrawInterface() { return this; }
	//virtual const DecalDrawInterface* getDecalDrawInterface() const { return this; }

	//virtual void initDecal(AsciiString texture, Real opacity, Int color, ShadowType type, UnsignedInt lifetime, UnsignedInt fadeOutTime, UnsignedInt fadeInTime, Real sizeX, Real sizeY);


protected:
	//Bool m_propAdded;

	OBBoxRenderObjClass* m_renderBox;  // The render object
	Shadow* m_shadow;  // the decal
	Bool m_fullyObscuredByShroud;
	UnsignedInt m_frameCreated;

private:
	void init_shadow();
	void init_renderBox(const Matrix3D* transformMtx);
};
