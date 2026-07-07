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

// FILE: ShipEffectsClientUpdate.cpp //////////////////////////////////////////////////////////////////
// Author: Andi W, 02 2026
// Desc:   client update for ship movement particle effects
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/Drawable.h"
#include "GameClient/Module/ShipEffectsClientUpdate.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/RandomValue.h"
#include "Common/DrawModule.h"
#include "Common/PerfTimer.h"
#include "Common/Xfer.h"
#include "GameLogic/Object.h"
#include "GameLogic/ScriptEngine.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ShipEffectsClientUpdate::ShipEffectsClientUpdate( Thing *thing, const ModuleData* moduleData ) :
	ClientUpdateModule( thing, moduleData )
{
	m_life = 0;

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ShipEffectsClientUpdate::~ShipEffectsClientUpdate( void )
{

}


//-------------------------------------------------------------------------------------------------
/** The client update callback. */
//-------------------------------------------------------------------------------------------------
void ShipEffectsClientUpdate::clientUpdate( void )
{
	//THIS IS HAPPENING CLIENT-SIDE
	// I CAN DO WHAT I NEED HERE AND NOT HAVE TO BE LOGIC SYNC-SAFE


	++m_life;

	Drawable *draw = getDrawable();
	if (draw)
	{

		for (DrawModule** dm = draw->getDrawModules(); *dm; ++dm)
		{
			ObjectDrawInterface* di = (*dm)->getObjectDrawInterface();
			if (di)
			{
				if (di->updateBonesForClientParticleSystems())
					break;
			}
		}


	}


}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ShipEffectsClientUpdate::crc( Xfer *xfer )
{

	// extend base class
	ClientUpdateModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ShipEffectsClientUpdate::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	ClientUpdateModule::xfer( xfer );


}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ShipEffectsClientUpdate::loadPostProcess( void )
{

	// extend base class
	ClientUpdateModule::loadPostProcess();

}
