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

// FILE: ChronoSphereUpdateModule.cpp ///////////////////////////////////////////////////////////////
// Desc:   Special power update module for a Red Alert 2 style chronosphere. The player selects two
//         locations (source and destination); this increment captures both points and wires the
//         module - the teleport effect is TODO.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/GameCommon.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"

#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/ChronoSphereUpdateModule.h"

#include <vector>

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ChronoSphereUpdateModuleData::ChronoSphereUpdateModuleData()
{
	m_specialPowerTemplate = nullptr;
	m_teleportDelayFrames = 0;
	m_radius = 0.0f;
	m_sourceFX = nullptr;
	m_targetFX = nullptr;
	m_unitSourceFX = nullptr;
	m_unitTargetFX = nullptr;
	m_sourceOCL = nullptr;
	m_targetOCL = nullptr;
	// m_requiredKindOf / m_forbiddenKindOf default-construct empty
}

//-------------------------------------------------------------------------------------------------
/*static*/ void ChronoSphereUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "SpecialPowerTemplate",	INI::parseSpecialPowerTemplate,	nullptr,				offsetof( ChronoSphereUpdateModuleData, m_specialPowerTemplate ) },
		{ "TeleportDelay",				INI::parseDurationUnsignedInt,	nullptr,				offsetof( ChronoSphereUpdateModuleData, m_teleportDelayFrames ) },
		{ "RequiredKindOf",				KindOfMaskType::parseFromINI,		nullptr,				offsetof( ChronoSphereUpdateModuleData, m_requiredKindOf ) },
		{ "ForbiddenKindOf",			KindOfMaskType::parseFromINI,		nullptr,				offsetof( ChronoSphereUpdateModuleData, m_forbiddenKindOf ) },
		{ "Radius",								INI::parseReal,									nullptr,				offsetof( ChronoSphereUpdateModuleData, m_radius ) },
		{ "SourceFX",							INI::parseFXList,								nullptr,				offsetof( ChronoSphereUpdateModuleData, m_sourceFX ) },
		{ "TargetFX",							INI::parseFXList,								nullptr,				offsetof( ChronoSphereUpdateModuleData, m_targetFX ) },
		{ "UnitSourceFX",					INI::parseFXList,								nullptr,				offsetof( ChronoSphereUpdateModuleData, m_unitSourceFX ) },
		{ "UnitTargetFX",					INI::parseFXList,								nullptr,				offsetof( ChronoSphereUpdateModuleData, m_unitTargetFX ) },
		{ "SourceOCL",						INI::parseObjectCreationList,		nullptr,				offsetof( ChronoSphereUpdateModuleData, m_sourceOCL ) },
		{ "TargetOCL",						INI::parseObjectCreationList,		nullptr,				offsetof( ChronoSphereUpdateModuleData, m_targetOCL ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ChronoSphereUpdateModule::ChronoSphereUpdateModule( Thing *thing, const ModuleData* moduleData ) : SpecialPowerUpdateModule( thing, moduleData )
{
	m_specialPowerModule = nullptr;
	m_sourceLocation.zero();
	m_destLocation.zero();
	m_teleportFrame = 0;
	m_active = FALSE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
ChronoSphereUpdateModule::~ChronoSphereUpdateModule( void )
{
}

//-------------------------------------------------------------------------------------------------
// Cache the paired SpecialPowerModule (recharge/cost/timer) so we can trigger it later.
//-------------------------------------------------------------------------------------------------
void ChronoSphereUpdateModule::onObjectCreated()
{
	const ChronoSphereUpdateModuleData *data = getChronoSphereUpdateModuleData();
	Object *obj = getObject();

	if( !data->m_specialPowerTemplate )
	{
		DEBUG_CRASH( ("%s object's ChronoSphereUpdateModule lacks access to the SpecialPowerTemplate. Needs to be specified in ini.", obj->getTemplate()->getName().str() ) );
		return;
	}

	m_specialPowerModule = obj->getSpecialPowerModule( data->m_specialPowerTemplate );
}

//-------------------------------------------------------------------------------------------------
// First click: the source point arrives here as targetPos. The destination (second click) is
// delivered separately via setSpecialPowerOverridableDestination().
//-------------------------------------------------------------------------------------------------
Bool ChronoSphereUpdateModule::initiateIntentToDoSpecialPower(const SpecialPowerTemplate *specialPowerTemplate, const Object *targetObj, const Coord3D *targetPos, const Waypoint *way, UnsignedInt commandOptions )
{
	if( m_specialPowerModule == nullptr || m_specialPowerModule->getSpecialPowerTemplate() != specialPowerTemplate )
	{
		// Make sure our modules are connected.
		return FALSE;
	}

	if( targetPos )
	{
		m_sourceLocation.set( targetPos );
	}
	else if( targetObj )
	{
		m_sourceLocation.set( targetObj->getPosition() );
	}

	m_active = TRUE;

	DEBUG_LOG(( "ChronoSphereUpdateModule: source selected at (%.1f, %.1f, %.1f)",
		m_sourceLocation.x, m_sourceLocation.y, m_sourceLocation.z ));

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Both clicks arrive together: locs[0] = source (already captured by initiateIntentToDoSpecialPower),
// locs[1] = destination. This is the N=2 case of the generic N-point delivery.
//-------------------------------------------------------------------------------------------------
void ChronoSphereUpdateModule::setSpecialPowerMultiLocations( const std::vector<Coord3D>& locs )
{
	if( locs.size() < 2 )
		return;

	m_sourceLocation.set( &locs[0] );
	m_destLocation.set( &locs[1] );

	DEBUG_LOG(( "ChronoSphereUpdateModule: destination selected at (%.1f, %.1f, %.1f)",
		m_destLocation.x, m_destLocation.y, m_destLocation.z ));

	// Both points are now chosen (second click). Trigger the paired SpecialPowerModule so it
	// charges the cost, plays the initiate sound/EVA and starts its recharge cooldown. Because
	// the SpecialAbility uses UpdateModuleStartsAttack = Yes, nothing fired until this call.
	if( m_active && m_specialPowerModule )
	{
		m_specialPowerModule->markSpecialPowerTriggered( &m_sourceLocation );
	}

	const ChronoSphereUpdateModuleData *data = getChronoSphereUpdateModuleData();

	// Fire the source/destination OCLs instantly on activation (they ignore TeleportDelay).
	// (Bool) disambiguates the createOwner overload from the angle overload.
	ObjectCreationList::create( data->m_sourceOCL, getObject(), &m_sourceLocation, nullptr, (Bool)FALSE );
	ObjectCreationList::create( data->m_targetOCL, getObject(), &m_destLocation, nullptr, (Bool)FALSE );

	// Arm the teleport: it fires TeleportDelay frames from now. m_active stays TRUE (pending) until
	// update() performs the teleport. We must arm the wake here (outside update()); setWakeFrame is
	// ignored if called from within update().
	m_teleportFrame = TheGameLogic->getFrame() + data->m_teleportDelayFrames;
	setWakeFrame( getObject(), data->m_teleportDelayFrames > 0 ? UPDATE_SLEEP( data->m_teleportDelayFrames ) : UPDATE_SLEEP_NONE );
}

//-------------------------------------------------------------------------------------------------
/** The update callback. Fires the teleport once the armed TeleportDelay has elapsed. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime ChronoSphereUpdateModule::update()
{
	if( !m_active || TheGameLogic->getFrame() < m_teleportFrame )
	{
		// Not armed (or awoken early for some other reason) - go back to sleep.
		return UPDATE_SLEEP_FOREVER;
	}

	doChronoTeleport();

	m_active = FALSE;
	return UPDATE_SLEEP_FOREVER;
}

//-------------------------------------------------------------------------------------------------
/** Relocate every object matching the KindOf filter within Radius of the source point to the
	* destination point, preserving each object's offset from the area center. Plays the area FX at
	* source/destination and the per-unit FX at each object's source and destination position. */
//-------------------------------------------------------------------------------------------------
void ChronoSphereUpdateModule::doChronoTeleport()
{
	const ChronoSphereUpdateModuleData *data = getChronoSphereUpdateModuleData();

	// Area FX at the two points (static form is null-safe).
	FXList::doFXPos( data->m_sourceFX, &m_sourceLocation );
	FXList::doFXPos( data->m_targetFX, &m_destLocation );

	// Gather matching objects first, then relocate them (avoid mutating the partition while iterating).
	std::vector<Object *> affected;
	{
		PartitionFilterAcceptByKindOf	kindFilter( data->m_requiredKindOf, data->m_forbiddenKindOf );
		PartitionFilterAlive					aliveFilter;
		PartitionFilter *filters[] = { &kindFilter, &aliveFilter, nullptr };

		SimpleObjectIterator *iter = ThePartitionManager->iterateObjectsInRange(
			&m_sourceLocation, data->m_radius, FROM_CENTER_2D, filters );
		MemoryPoolObjectHolder hold( iter );

		for( Object *o = iter->first(); o; o = iter->next() )
		{
			if( o != getObject() )	// never teleport the caster itself
				affected.push_back( o );
		}
	}

	Pathfinder *pathfinder = TheAI->pathfinder();

	for( std::vector<Object *>::iterator it = affected.begin(); it != affected.end(); ++it )
	{
		Object *obj = *it;

		// Destination keeps the object's offset from the source center; snap Z to the ground there.
		const Coord3D *objPos = obj->getPosition();
		Coord3D dest;
		dest.x = m_destLocation.x + (objPos->x - m_sourceLocation.x);
		dest.y = m_destLocation.y + (objPos->y - m_sourceLocation.y);
		dest.z = TheTerrainLogic->getGroundHeight( dest.x, dest.y );

		// Per-unit FX at the source (before the move).
		FXList::doFXObj( data->m_unitSourceFX, obj );

		// Break interpolated visuals (tread marks) BEFORE the move: setPosition() adds a tread edge
		// synchronously (reactToTransformChange), so breaking the strip afterwards is too late - the
		// stretched bridge edge would already be in the buffer. Breaking first makes that edge start
		// a fresh anchor at the destination, and the pre-teleport tracks are kept.
		Drawable *draw = obj->getDrawable();
		if( draw )
			draw->reactToTeleport();

		if( obj->isKindOf( KINDOF_STRUCTURE ) )
		{
			// Structures are baked into the pathfind map as obstacles - bracket the move.
			pathfinder->removeObjectFromPathfindMap( obj );
			obj->setPosition( &dest );
			pathfinder->addObjectToPathfindMap( obj );
		}
		else
		{
			// Mobile units: stop current path/goal, move, then re-register footprint cells.
			AIUpdateInterface *ai = obj->getAIUpdateInterface();
			if( ai )
				ai->aiIdle( CMD_FROM_AI );
			obj->setPosition( &dest );
			pathfinder->updatePos( obj, &dest );
		}

		// Per-unit FX at the destination (after the move).
		FXList::doFXObj( data->m_unitTargetFX, obj );
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void ChronoSphereUpdateModule::crc( Xfer *xfer )
{
	// extend base class
	SpecialPowerUpdateModule::crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Added m_teleportFrame (delayed teleport) */
// ------------------------------------------------------------------------------------------------
void ChronoSphereUpdateModule::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = 2;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	SpecialPowerUpdateModule::xfer( xfer );

	// we do not need to tie up the special power module pointer, it is done on object creation
	// SpecialPowerModuleInterface *m_specialPowerModule;

	xfer->xferCoord3D( &m_sourceLocation );
	xfer->xferCoord3D( &m_destLocation );
	if( version >= 2 )
		xfer->xferUnsignedInt( &m_teleportFrame );
	xfer->xferBool( &m_active );
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ChronoSphereUpdateModule::loadPostProcess( void )
{
	// extend base class
	SpecialPowerUpdateModule::loadPostProcess();

	// If a teleport was pending when the game was saved, re-arm the wake so it still fires.
	if( m_active )
	{
		UnsignedInt now = TheGameLogic->getFrame();
		UnsignedInt delay = (m_teleportFrame > now) ? (m_teleportFrame - now) : 0;
		setWakeFrame( getObject(), delay > 0 ? UPDATE_SLEEP( delay ) : UPDATE_SLEEP_NONE );
	}
}
