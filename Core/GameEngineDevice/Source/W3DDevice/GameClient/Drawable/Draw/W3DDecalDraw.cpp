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

// FILE: W3DDecalDraw.cpp ////////////////////////////////////////////////////////////////////////
// Author: Andi W, May 26
// Desc:   Draw module for Decal FXNugget
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include <stdlib.h>

#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameClient/Drawable.h"
#include "W3DDevice/GameClient/Module/W3DDecalDraw.h"
#include "W3DDevice/GameClient/BaseHeightMap.h"


//-------------------------------------------------------------------------------------------------
W3DDecalDrawModuleData::W3DDecalDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
W3DDecalDrawModuleData::~W3DDecalDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
void W3DDecalDrawModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  ModuleData::buildFieldParse(p);
	static const FieldParse dataFieldParse[] =
	{
		// TODO: Add all the fields here and it will be defined in ini
		//{ "ModelName", INI::parseAsciiString, nullptr, offsetof(W3DDecalDrawModuleData, m_modelName) },
		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
//
//void W3DDecalDraw::initDecal(AsciiString texture, Real opacity, Int color, ShadowType type, UnsignedInt lifetime, UnsignedInt fadeOutTime, UnsignedInt fadeInTime, Real sizeX, Real sizeY)
//{
//
//}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DDecalDraw::W3DDecalDraw( Thing *thing, const ModuleData* moduleData ) : DrawModule( thing, moduleData ),
m_propAdded(false)
{

}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DDecalDraw::~W3DDecalDraw( void )
{
}

//-------------------------------------------------------------------------------------------------
//void W3DDecalDraw::reactToTransformChange( const Matrix3D *oldMtx,
//																							 const Coord3D *oldPos,
//																							 Real oldAngle )
//{
//	Drawable *draw = getDrawable();
//	if (m_propAdded) {
//		return;
//	}
//	if (draw->getPosition()->x==0.0f && draw->getPosition()->y == 0.0f) {
//		return;
//	}
//	m_propAdded = true;
//	const W3DDecalDrawModuleData *moduleData = getW3DDecalDrawModuleData();
//	if (!moduleData) {
//		return;
//	}
//	Real scale = draw->getScale();
//	TheTerrainRenderObject->addProp((Int)draw->getID(), *draw->getPosition(),
//		draw->getOrientation(), scale, moduleData->m_modelName);
//
//}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void W3DDecalDraw::doDrawModule(const Matrix3D* transformMtx)
{

	return;

}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DDecalDraw::crc( Xfer *xfer )
{

	// extend base class
	DrawModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DDecalDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	DrawModule::xfer( xfer );

	// no data to save here, nobody will ever notice

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DDecalDraw::loadPostProcess( void )
{

	// extend base class
	DrawModule::loadPostProcess();

}
