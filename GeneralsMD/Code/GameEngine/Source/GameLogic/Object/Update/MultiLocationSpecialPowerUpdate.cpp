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

// FILE: MultiLocationSpecialPowerUpdate.cpp ////////////////////////////////////////////////////////
// Desc:   Generic N-point special power update module. Captures a configurable number of clicked
//         target points and receives them all at once. This foundation increment logs the points
//         and starts the cooldown - no gameplay effect yet.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h"

#include "Common/Player.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/MultiLocationSpecialPowerUpdate.h"

#include <vector>

static const Real MULTILOC_CREATE_ABOVE_LOCATION_HEIGHT = 300.0f;

//-------------------------------------------------------------------------------------------------
// Mirror of TheOCLCreateLocTypeNames (file-static in OCLSpecialPower.cpp, not exported).
//-------------------------------------------------------------------------------------------------
static const char* const TheMultiLocOCLCreateLocTypeNames[] =
{
	"CREATE_AT_EDGE_NEAR_SOURCE",
	"CREATE_AT_EDGE_NEAR_TARGET",
	"CREATE_AT_LOCATION",
	"USE_OWNER_OBJECT",
	"CREATE_ABOVE_LOCATION",
	"CREATE_AT_EDGE_FARTHEST_FROM_TARGET",
	nullptr
};
static_assert(ARRAY_SIZE(TheMultiLocOCLCreateLocTypeNames) == OCL_CREATE_LOC_COUNT + 1, "Incorrect array size");

//-------------------------------------------------------------------------------------------------
// Parse one "UpgradeOCL = <science> <ocl>" pair into the vector (mirrors parseOCLUpgradePair).
//-------------------------------------------------------------------------------------------------
static void parseMultiLocUpgradeOCL( INI* ini, void * /*instance*/, void *store, const void* /*userData*/ )
{
	MultiLocationSpecialPowerUpdateModuleData::Upgrades up;

	INI::parseScience(ini, nullptr, &up.m_science, nullptr);
	INI::parseObjectCreationList(ini, nullptr, &up.m_ocl, nullptr);

	std::vector<MultiLocationSpecialPowerUpdateModuleData::Upgrades>* s =
		(std::vector<MultiLocationSpecialPowerUpdateModuleData::Upgrades>*)store;
	s->push_back(up);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MultiLocationSpecialPowerUpdateModuleData::MultiLocationSpecialPowerUpdateModuleData()
{
	m_specialPowerTemplate = nullptr;
	m_defaultOCL = nullptr;
	m_upgradeOCL.clear();
	m_createLoc = CREATE_AT_EDGE_NEAR_SOURCE;
	m_initialDelay = 0;
	m_delay = 0;
	m_waitForAttackComplete = FALSE;
	m_maxWaitPerTarget = 0;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void MultiLocationSpecialPowerUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "SpecialPowerTemplate",	INI::parseSpecialPowerTemplate,	nullptr, offsetof( MultiLocationSpecialPowerUpdateModuleData, m_specialPowerTemplate ) },
		{ "OCL",									INI::parseObjectCreationList,		nullptr, offsetof( MultiLocationSpecialPowerUpdateModuleData, m_defaultOCL ) },
		{ "CreateLocation",				INI::parseIndexList,	TheMultiLocOCLCreateLocTypeNames, offsetof( MultiLocationSpecialPowerUpdateModuleData, m_createLoc ) },
		{ "UpgradeOCL",						parseMultiLocUpgradeOCL,				nullptr, offsetof( MultiLocationSpecialPowerUpdateModuleData, m_upgradeOCL ) },
		{ "InitialDelay",					INI::parseDurationUnsignedInt,	nullptr, offsetof( MultiLocationSpecialPowerUpdateModuleData, m_initialDelay ) },
		{ "Delay",								INI::parseDurationUnsignedInt,	nullptr, offsetof( MultiLocationSpecialPowerUpdateModuleData, m_delay ) },
		{ "WaitForAttackComplete",	INI::parseBool,								nullptr, offsetof( MultiLocationSpecialPowerUpdateModuleData, m_waitForAttackComplete ) },
		{ "MaxWaitPerTarget",			INI::parseDurationUnsignedInt,	nullptr, offsetof( MultiLocationSpecialPowerUpdateModuleData, m_maxWaitPerTarget ) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MultiLocationSpecialPowerUpdate::MultiLocationSpecialPowerUpdate( Thing *thing, const ModuleData* moduleData ) : SpecialPowerUpdateModule( thing, moduleData )
{
	m_specialPowerModule = nullptr;
	m_locations.clear();
	m_active = FALSE;
	m_spawnIndex = 0;
	m_attackSeen = FALSE;
	m_waitDeadline = 0;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
MultiLocationSpecialPowerUpdate::~MultiLocationSpecialPowerUpdate( void )
{
}

//-------------------------------------------------------------------------------------------------
// Cache the paired SpecialPowerModule (recharge/cost/timer) so we can trigger it later.
//-------------------------------------------------------------------------------------------------
void MultiLocationSpecialPowerUpdate::onObjectCreated()
{
	const MultiLocationSpecialPowerUpdateModuleData *data = getMultiLocationSpecialPowerUpdateModuleData();
	Object *obj = getObject();

	if( !data->m_specialPowerTemplate )
	{
		DEBUG_CRASH( ("%s object's MultiLocationSpecialPowerUpdate lacks access to the SpecialPowerTemplate. Needs to be specified in ini.", obj->getTemplate()->getName().str() ) );
		return;
	}

	m_specialPowerModule = obj->getSpecialPowerModule( data->m_specialPowerTemplate );
}

//-------------------------------------------------------------------------------------------------
// The OCL to fire: the first upgrade whose science the controlling player has, else the default.
//-------------------------------------------------------------------------------------------------
const ObjectCreationList* MultiLocationSpecialPowerUpdate::findOCL() const
{
	const MultiLocationSpecialPowerUpdateModuleData *d = getMultiLocationSpecialPowerUpdateModuleData();
	const Player *controller = getObject()->getControllingPlayer();
	if( controller != nullptr )
	{
		for( std::vector<MultiLocationSpecialPowerUpdateModuleData::Upgrades>::const_iterator it = d->m_upgradeOCL.begin();
					it != d->m_upgradeOCL.end();
					++it )
		{
			if( controller->hasScience( it->m_science ) )
				return it->m_ocl;
		}
	}
	return d->m_defaultOCL;
}

//-------------------------------------------------------------------------------------------------
// Spawn the OCL at one captured point, honoring CreateLocation (mirrors OCLSpecialPower).
//-------------------------------------------------------------------------------------------------
void MultiLocationSpecialPowerUpdate::fireOclAtLocation( const Coord3D *loc )
{
	if( loc == nullptr )
		return;

	const ObjectCreationList *ocl = findOCL();
	if( ocl == nullptr )
		return;

	const MultiLocationSpecialPowerUpdateModuleData *d = getMultiLocationSpecialPowerUpdateModuleData();
	Object *obj = getObject();
	Coord3D targetCoord = *loc;
	Coord3D creationCoord;

	switch( d->m_createLoc )
	{
		case CREATE_AT_EDGE_NEAR_SOURCE:
			creationCoord = TheTerrainLogic->findClosestEdgePoint( obj->getPosition() );
			ObjectCreationList::create( ocl, obj, &creationCoord, &targetCoord, INVALID_ANGLE );
			break;
		case CREATE_AT_EDGE_NEAR_TARGET:
			creationCoord = TheTerrainLogic->findClosestEdgePoint( &targetCoord );
			ObjectCreationList::create( ocl, obj, &creationCoord, &targetCoord, INVALID_ANGLE );
			break;
		case CREATE_AT_EDGE_FARTHEST_FROM_TARGET:
			creationCoord = TheTerrainLogic->findFarthestEdgePoint( &targetCoord );
			creationCoord.z += MULTILOC_CREATE_ABOVE_LOCATION_HEIGHT;
			ObjectCreationList::create( ocl, obj, &creationCoord, &targetCoord, INVALID_ANGLE );
			break;
		case CREATE_AT_LOCATION:
			creationCoord = targetCoord;
			ObjectCreationList::create( ocl, obj, &creationCoord, &targetCoord, INVALID_ANGLE );
			break;
		case USE_OWNER_OBJECT:
			creationCoord.set(targetCoord );
			ObjectCreationList::create( ocl, obj, &creationCoord, &targetCoord, INVALID_ANGLE, false );
			break;
		case CREATE_ABOVE_LOCATION:
			creationCoord = targetCoord;
			creationCoord.z += MULTILOC_CREATE_ABOVE_LOCATION_HEIGHT;
			ObjectCreationList::create( ocl, obj, &creationCoord, &targetCoord, INVALID_ANGLE );
			break;
	}
}

//-------------------------------------------------------------------------------------------------
// Frames between update() wake-ups. In WaitForAttackComplete mode Delay is the poll granularity and
// the minimum gap between points, rather than the whole spacing.
//-------------------------------------------------------------------------------------------------
UnsignedInt MultiLocationSpecialPowerUpdate::getPollInterval() const
{
	const MultiLocationSpecialPowerUpdateModuleData *d = getMultiLocationSpecialPowerUpdateModuleData();
	if( d->m_delay > 0 )
		return d->m_delay;

	return d->m_waitForAttackComplete ? (LOGICFRAMES_PER_SECOND / 4) : 1;
}

//-------------------------------------------------------------------------------------------------
// First click: the first target point arrives here as targetPos. The rest of the points arrive
// together via setSpecialPowerMultiLocations() once the final click commits.
//-------------------------------------------------------------------------------------------------
Bool MultiLocationSpecialPowerUpdate::initiateIntentToDoSpecialPower(const SpecialPowerTemplate *specialPowerTemplate, const Object *targetObj, const Coord3D *targetPos, const Waypoint *way, UnsignedInt commandOptions )
{
	if( m_specialPowerModule == nullptr || m_specialPowerModule->getSpecialPowerTemplate() != specialPowerTemplate )
	{
		// Make sure our modules are connected.
		return FALSE;
	}

	m_active = TRUE;

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
// Final click: all captured target points arrive together (in click order).
//-------------------------------------------------------------------------------------------------
void MultiLocationSpecialPowerUpdate::setSpecialPowerMultiLocations( const std::vector<Coord3D>& locs )
{
	if( locs.empty() )
		return;

	m_locations = locs;

	// Trigger the paired SpecialPowerModule so it charges the cost, plays the initiate sound/EVA and
	// starts its recharge cooldown. Because the SpecialAbility uses UpdateModuleStartsAttack = Yes,
	// nothing fired until this call.
	if( m_active && m_specialPowerModule )
	{
		m_specialPowerModule->markSpecialPowerTriggered( &m_locations[0] );
	}

	// Arm the timed OCL spawn sequence: the first OCL fires InitialDelay frames from now, then one
	// per point (in click order) every Delay frames. update() drives it.
	const MultiLocationSpecialPowerUpdateModuleData *d = getMultiLocationSpecialPowerUpdateModuleData();
	m_spawnIndex = 0;
	m_active = TRUE;
	m_attackSeen = FALSE;
	m_waitDeadline = 0;
	setWakeFrame( getObject(), UPDATE_SLEEP( d->m_initialDelay > 0 ? d->m_initialDelay : 1 ) );
}

//-------------------------------------------------------------------------------------------------
/** Fire one point's OCL per wake-up, waiting Delay frames between points until all are spawned. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime MultiLocationSpecialPowerUpdate::update()
{
	if( !m_active )
		return UPDATE_SLEEP_FOREVER;

	const MultiLocationSpecialPowerUpdateModuleData *d = getMultiLocationSpecialPowerUpdateModuleData();
	const UnsignedInt pollInterval = getPollInterval();

	// If the OCL makes us attack (an Attack nugget orders our own AI), the next point's order would
	// cancel the barrage still in progress. In that mode hold here until the caster is done shooting.
	if( d->m_waitForAttackComplete && m_spawnIndex > 0 && m_spawnIndex < (Int)m_locations.size() )
	{
		const AIUpdateInterface *ai = getObject()->getAIUpdateInterface();
		const Bool attacking = ai != nullptr && ai->isAttacking();

		// The AI needs a frame or two to enter the attack state after aiAttackPosition, so "not
		// attacking" only means "finished" once we have actually seen it attack.
		if( attacking )
			m_attackSeen = TRUE;

		const Bool finished = m_attackSeen && !attacking;
		const Bool timedOut = TheGameLogic->getFrame() >= m_waitDeadline;
		if( !finished && !timedOut )
			return UPDATE_SLEEP( pollInterval );
	}

	if( m_spawnIndex < (Int)m_locations.size() )
	{
		fireOclAtLocation( &m_locations[m_spawnIndex] );
		++m_spawnIndex;

		m_attackSeen = FALSE;
		const UnsignedInt maxWait = d->m_maxWaitPerTarget > 0 ? d->m_maxWaitPerTarget : (30 * LOGICFRAMES_PER_SECOND);
		m_waitDeadline = TheGameLogic->getFrame() + maxWait;
	}

	if( m_spawnIndex >= (Int)m_locations.size() )
	{
		// sequence complete
		m_active = FALSE;
		return UPDATE_SLEEP_FOREVER;
	}

	return UPDATE_SLEEP( pollInterval );
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void MultiLocationSpecialPowerUpdate::crc( Xfer *xfer )
{
	// extend base class
	SpecialPowerUpdateModule::crc( xfer );
}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version
	* 2: Added m_spawnIndex (timed OCL spawn sequence)
	* 3: Added m_attackSeen / m_waitDeadline (WaitForAttackComplete sequencing) */
// ------------------------------------------------------------------------------------------------
void MultiLocationSpecialPowerUpdate::xfer( Xfer *xfer )
{
	// version
	const XferVersion currentVersion = 3;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	SpecialPowerUpdateModule::xfer( xfer );

	// we do not need to tie up the special power module pointer, it is done on object creation
	// SpecialPowerModuleInterface *m_specialPowerModule;

	// captured target points (variable count)
	UnsignedShort pointCount = (UnsignedShort)m_locations.size();
	xfer->xferUnsignedShort( &pointCount );
	if( xfer->getXferMode() == XFER_LOAD )
		m_locations.resize( pointCount );
	for( UnsignedShort idx = 0; idx < pointCount; ++idx )
		xfer->xferCoord3D( &m_locations[idx] );

	xfer->xferBool( &m_active );

	if( version >= 2 )
		xfer->xferInt( &m_spawnIndex );

	if( version >= 3 )
	{
		xfer->xferBool( &m_attackSeen );
		xfer->xferUnsignedInt( &m_waitDeadline );
	}
}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void MultiLocationSpecialPowerUpdate::loadPostProcess( void )
{
	// extend base class
	SpecialPowerUpdateModule::loadPostProcess();

	// Do not call setWakeFrame() here to resume an in-flight spawn sequence. It is not safe from the
	// xfer system: the sleepy update heap has not been rebuilt yet, so our index in the logic is
	// still -1 and friend_awakenUpdateModule would RELEASE_CRASH. The sequence resumes on its own -
	// UpdateModule::xfer already restored our wake frame, and GameLogic::loadPostProcess rebuilds the
	// heap from it, which also preserves the sleep we had banked instead of restarting a full one.
}
