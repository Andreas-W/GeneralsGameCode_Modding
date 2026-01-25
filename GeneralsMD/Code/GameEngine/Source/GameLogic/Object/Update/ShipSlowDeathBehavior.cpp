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

// FILE: ShipSlowDeathBehavior.cpp ////////////////////////////////////////////////////////////
// Author: Andi W, 01 2026
// Desc:   ship slow deaths
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameAudio.h"
#include "Common/GlobalData.h"
#include "Common/RandomValue.h"
#include "Common/ThingFactory.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ParticleSys.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/EjectPilotDie.h"
#include "GameLogic/Module/ShipSlowDeathBehavior.h"
#include "GameLogic/Module/PhysicsUpdate.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
// Helicopter slow death update module data ///////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////




//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ShipSlowDeathBehaviorModuleData::ShipSlowDeathBehaviorModuleData( void )
{
	m_attachParticleBone.clear();
	m_attachParticleSystem = NULL;
	m_attachParticleLoc.x = 0.0f;
	m_attachParticleLoc.y = 0.0f;
	m_attachParticleLoc.z = 0.0f;
	m_oclEjectPilot = NULL;
	m_fxHitGround = NULL;
	m_oclHitGround = NULL;
}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
/*static*/ void ShipSlowDeathBehaviorModuleData::buildFieldParse( MultiIniFieldParse &p )
{
  SlowDeathBehaviorModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		{ "InitialDelay", INI::parseDurationUnsignedInt, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_initialDelay) },
		{ "InitialDelayVariance", INI::parseDurationUnsignedInt, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_initialDelayVariance) },
		{ "InitWobbleMaxAnglePitch", INI::parseAngleReal, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_initWobbleMaxPitch) },
		{ "InitWobbleMaxAngleYaw", INI::parseAngleReal, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_initWobbleMaxYaw) },
		{ "InitWobbleMaxAngleRoll", INI::parseAngleReal, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_initWobbleMaxRoll) },
		{ "InitWobbleInterval", INI::parseDurationUnsignedInt, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_initWobbleInterval) },

		{ "ToppleFrontMaxPitch", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleFrontMaxPitch) },
		{ "ToppleBackMaxPitch", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleBackMaxPitch) },
		{ "ToppleSideMaxRoll", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleSideMaxRoll) },
		{ "ToppleDuration", INI::parseDurationUnsignedInt, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleDuration) },

		{ "SinkWobbleMaxAnglePitch", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_sinkWobbleMaxPitch) },
		{ "SinkWobbleMaxAngleYaw", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_sinkWobbleMaxYaw) },
		{ "SinkWobbleMaxAngleRoll", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_sinkWobbleMaxRoll) },
		{ "SinkWobbleInterval", INI::parseDurationUnsignedInt, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_sinkWobbleInterval) },

		{ "SinkHowFast", INI::parsePercentToReal, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_sinkHowFast) },


		{ "AttachParticle", INI::parseParticleSystemTemplate, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_attachParticleSystem ) },
		{ "AttachParticleBone", INI::parseAsciiString, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_attachParticleBone ) },
		{ "AttachParticleLoc", INI::parseCoord3D, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_attachParticleLoc ) },
		
		{ "OCLEjectPilot", INI::parseObjectCreationList, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_oclEjectPilot ) },
		
		{ "FXHitGround", INI::parseFXList, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_fxHitGround ) },
		{ "OCLHitGround", INI::parseObjectCreationList, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_oclHitGround ) },
		
		{ "SoundSinkLoop", INI::parseAudioEventRTS, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_deathSound) },

		{ 0, 0, 0, 0 }

	};

  p.add(dataFieldParse);

}

///////////////////////////////////////////////////////////////////////////////////////////////////
// Ship slow death update      ///////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ShipSlowDeathBehavior::ShipSlowDeathBehavior( Thing *thing, const ModuleData *moduleData )
													: SlowDeathBehavior( thing, moduleData )
{

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ShipSlowDeathBehavior::~ShipSlowDeathBehavior( void )
{

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::beginSlowDeath( const DamageInfo *damageInfo )
{
	// extending functionality
	SlowDeathBehavior::beginSlowDeath( damageInfo );

	// get the module data
	const ShipSlowDeathBehaviorModuleData* d = getShipSlowDeathBehaviorModuleData();

	UnsignedInt now = TheGameLogic->getFrame();
	m_initStartFrame = now;
	m_toppleStartFrame = now + (d->m_initialDelay + GameLogicRandomValue(0, d->m_initialDelayVariance));

	switch (d->m_toppleType)
	{
		case TOPPLE_RANDOM:
			m_chosenToppleType = GameLogicRandomValue(TOPPLE_FRONT, TOPPLE_SIDE_RIGHT);
			break;
		case TOPPLE_SIDE:
			m_chosenToppleType = GameLogicRandomValue(TOPPLE_SIDE_LEFT, TOPPLE_SIDE_RIGHT);
			break;
		default:
			m_chosenToppleType = d->m_toppleType;
			break;
	}

	beginInitPhase();

}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime ShipSlowDeathBehavior::update( void )
{
/// @todo srj use SLEEPY_UPDATE here
	// call the base class cause we're extending functionality
	SlowDeathBehavior::update();

	// get out of here if we're not activated yet
	if( isSlowDeathActivated() == FALSE )
		return UPDATE_SLEEP_NONE;

	// get the module data
	const ShipSlowDeathBehaviorModuleData *d = getShipSlowDeathBehaviorModuleData();


	UnsignedInt now = TheGameLogic->getFrame();
	if (now < m_toppleStartFrame) {
		doInitPhase();
		return UPDATE_SLEEP_NONE;
	}
	else if (m_sinkStartFrame <= 0) {
		if (d->m_toppleDuration > 0) {
			m_sinkStartFrame = now + d->m_toppleDuration;
			beginTopplePhase();
		}
		else {
			m_sinkStartFrame = now;
		}
	}
	if (now < m_sinkStartFrame) {
		doTopplePhase();
		return UPDATE_SLEEP_NONE;
	}
	else {
		if (!m_shipSinkStarted)
			beginSinkPhase();

		doSinkPhase();
		return UPDATE_SLEEP_NONE;
	}

	return UPDATE_SLEEP_NONE;

}

// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::beginInitPhase()
{

}
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::beginTopplePhase()
{

}
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::beginSinkPhase()
{

}
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::doInitPhase()
{
	//const ShipSlowDeathBehaviorModuleData* d = getShipSlowDeathBehaviorModuleData();

	//if (d->m_initWobbleInterval <= 0)
	//	return;

	//// do the wobble

	//UnsignedInt now = TheGameLogic->getFrame();
	//UnsignedInt timePassed = now - m_initStartFrame;
	//Real progress = INT_TO_REAL(timePassed % d->m_initWobbleInterval) / INT_TO_REAL(d->m_initWobbleInterval);


}
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::doTopplePhase()
{
		const ShipSlowDeathBehaviorModuleData* d = getShipSlowDeathBehaviorModuleData();

		if (d->m_toppleDuration <= 0)
			return;

		Object* obj = getObject();

		UnsignedInt now = TheGameLogic->getFrame();
		UnsignedInt timePassed = now - m_toppleStartFrame;
		Real progress = INT_TO_REAL(timePassed) / INT_TO_REAL(d->m_toppleDuration);

		Matrix3D mtx = *obj->getTransformMatrix();

		Real pitchRate;
		Real yawRate;
		Real rollRate;

		// Topple the boat
		switch (m_chosenToppleType)
		{
			case TOPPLE_FRONT:
				break;
			case TOPPLE_BACK:
				break;
			case TOPPLE_SIDE_LEFT:
				break;
			case TOPPLE_SIDE_RIGHT:
				break;
			case TOPPLE_NONE:
				return;
			default:
				DEBUG_CRASH(("Invalid topple type in ShipSlowDeathBehavior"));
		}



}
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::doSinkPhase()
{

}
// ------------------------------------------------------------------------------------------------





// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::crc( Xfer *xfer )
{

	// extend base class
	SlowDeathBehavior::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	SlowDeathBehavior::xfer( xfer );


	xfer->xferUnsignedInt( &m_initStartFrame);
	xfer->xferUnsignedInt( &m_toppleStartFrame);
	xfer->xferUnsignedInt( &m_sinkStartFrame);
	xfer->xferBool( &m_shipSinkStarted);
	xfer->xferInt( &m_chosenToppleType);

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::loadPostProcess( void )
{

	// extend base class
	SlowDeathBehavior::loadPostProcess();

}
