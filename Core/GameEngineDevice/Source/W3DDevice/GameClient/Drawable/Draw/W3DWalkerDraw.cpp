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

// FILE: W3DWalkerDraw.cpp ////////////////////////////////////////////////////////////////////////////
// Author: Andi W, May 26
// Desc: TODO
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "W3DDevice/GameClient/Module/W3DWalkerDraw.h"

#include "Common/GameUtility.h"
#include "Common/Player.h"
#include "Common/Xfer.h"

//-------------------------------------------------------------------------------------------------
W3DWalkerDrawModuleData::W3DWalkerDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
W3DWalkerDrawModuleData::~W3DWalkerDrawModuleData()
{
}

//-------------------------------------------------------------------------------------------------
void W3DWalkerDrawModuleData::buildFieldParse(MultiIniFieldParse& p)
{
  W3DModelDrawModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		//{ "RequiredScience", INI::parseScience, nullptr, offsetof(W3DWalkerDrawModuleData, m_requiredScience) },

		{ nullptr, nullptr, nullptr, 0 }
	};
  p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
W3DWalkerDraw::W3DWalkerDraw( Thing *thing, const ModuleData* moduleData ) : W3DModelDraw( thing, moduleData )
{
}

//-------------------------------------------------------------------------------------------------
W3DWalkerDraw::~W3DWalkerDraw()
{
}

//-------------------------------------------------------------------------------------------------
// All this does is stop the call path if we haven't been cleared to draw yet
void W3DWalkerDraw::doDrawModule(const Matrix3D* transformMtx)
{
	W3DModelDraw::doDrawModule(transformMtx);
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void W3DWalkerDraw::crc( Xfer *xfer )
{

	// extend base class
	W3DModelDraw::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void W3DWalkerDraw::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	W3DModelDraw::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void W3DWalkerDraw::loadPostProcess( void )
{

	// extend base class
	W3DModelDraw::loadPostProcess();

}


