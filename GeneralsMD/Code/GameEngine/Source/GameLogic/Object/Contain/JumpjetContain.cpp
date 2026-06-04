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

// FILE: ParachuteContain.cpp //////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, March 2002
// Desc:   Contain module for transport units.
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/CRCDebug.h"
#include "Common/Player.h"
#include "Common/RandomValue.h"
#include "Common/ThingTemplate.h"
#include "Common/Xfer.h" 

#include "GameLogic/AIPathfind.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/JumpjetContain.h"
#include "GameLogic/Module/JumpjetMissileAIUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"

#include "GameClient/Drawable.h"


const Real NO_START_Z = 1e10;


#ifdef _INTERNAL
// for occasional debugging...
//#pragma optimize("", off)
//#pragma MESSAGE("************************************** WARNING, optimization disabled for debugging purposes")
#endif


// PRIVATE ////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
JumpjetContainModuleData::JumpjetContainModuleData() :
	//m_pitchRateMax(0),
	//m_rollRateMax(0),
	//m_lowAltitudeDamping(0.2f),
	//m_paraOpenDist(0.0f),
	//m_freeFallDamagePercent(0.5f),
	m_conditionFlagFlying(MODELCONDITION_FREEFALL),
	m_conditionFlagLanding(MODELCONDITION_PARACHUTING),
	m_landingDistance(0.0f),
	m_killWhenLandingInWater(FALSE),
	m_killWhenLandingOnCliff(FALSE),
	m_killWhenLandingOnImpassable(FALSE),
	m_killWhenLandingInWaterSlop(10.0f)
{
}

//-------------------------------------------------------------------------------------------------
void JumpjetContainModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	OpenContainModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "FlyingConditionFlag",	ModelConditionFlags::parseSingleBitFromINI,	NULL, offsetof(JumpjetContainModuleData, m_conditionFlagFlying) },
		{ "LandingConditionFlag",	ModelConditionFlags::parseSingleBitFromINI,	NULL, offsetof(JumpjetContainModuleData, m_conditionFlagLanding) },
		{ "LandingDistance",	INI::parseReal,		NULL, offsetof(JumpjetContainModuleData, m_landingDistance) },
		{ "KillWhenLandingInWater",	INI::parseBool,		NULL, offsetof(JumpjetContainModuleData, m_killWhenLandingInWater) },
		{ "KillWhenLandingOnCliff",	INI::parseBool,		NULL, offsetof(JumpjetContainModuleData, m_killWhenLandingOnCliff) },
		{ "KillWhenLandingOnImpassable",	INI::parseBool,		NULL, offsetof(JumpjetContainModuleData, m_killWhenLandingOnImpassable) },
		{ "KillWhenLandingInWaterSlop",	INI::parseReal,		NULL, offsetof(JumpjetContainModuleData, m_killWhenLandingInWaterSlop) },
		{ 0, 0, 0, 0 }
	};
	p.add(dataFieldParse);
}

// PUBLIC /////////////////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
JumpjetContain::JumpjetContain(Thing* thing, const ModuleData* moduleData) :
	OpenContain(thing, moduleData)
{
	m_landing = false;

	//getObject()->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_PARACHUTING ) );
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
JumpjetContain::~JumpjetContain(void)
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/**
	can this container contain this kind of object?
	and, if checkCapacity is TRUE, does this container have enough space left to hold the given unit?
*/
Bool JumpjetContain::isValidContainerFor(const Object* rider, Bool checkCapacity) const
{
	if (!rider)
		return false;

	// extend functionality
	if (OpenContain::isValidContainerFor(rider, checkCapacity) == false)
		return false;

	//Int transportSlotCount = rider->getTransportSlotCount();

	//// if 0, this object isn't transportable.
	//// (exception: infantry are always transportable by parachutes, regardless
	//// of this.... this allows us to paradrop pilots, but not transport them
	//// by other means)
	//if (transportSlotCount == 0 && !rider->isKindOf(KINDOF_INFANTRY) && !rider->isKindOf(KINDOF_PARACHUTABLE))
	//	return false;

	// we can only "hold" one item at a time.
	if (getContainCount() > 0)
		return false;

	return true;
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
UpdateSleepTime JumpjetContain::update(void)
{
	OpenContain::update();

	Object* parachute = getObject();
	if (parachute->isDisabledByType(DISABLED_HELD))
	{
		return UPDATE_SLEEP_NONE; // my, that was easy
	}

	Object* rider = (getContainCount() > 0) ? getContainList().front() : NULL;


	// allow us to land on bridges!
	const Coord3D* paraPos = getObject()->getPosition();
	PathfindLayerEnum newLayer = TheTerrainLogic->getHighestLayerForDestination(paraPos);
	getObject()->setLayer(newLayer);
	if (rider)
		rider->setLayer(newLayer);

	// If we have lost our passenger for whatever reason, die early.  Otherwise we just sit around forever.
	if (getContainCount() == 0)
		getObject()->kill();


	// the collide system doesn't always collide us with the ground if we fall into water.
	// so force the issue.
	// TODO:
	//Real waterZ;
	//if (!getObject()->isEffectivelyDead()
	//		&& getObject()->getLayer() == LAYER_GROUND
	//		&& TheTerrainLogic->isUnderwater(paraPos->x, paraPos->y, &waterZ)
	//		&& (paraPos->z - waterZ) < d->m_killWhenLandingInWaterSlop)
	//{
	//	getObject()->kill();
	//}


	// Check if MissileAI is in Attack state -> set landing conditionflag
	if (!m_landing && (rider)) {
		const JumpjetContainModuleData* d = getJumpjetContainModuleData();

		//Note: Would it make more sense to check target distance here, and not search for the module every frame?
		static NameKeyType key_JumpjetMissileAIUpdate = NAMEKEY("JumpjetMissileAIUpdate");
		JumpjetMissileAIUpdate* missileUpdate = (JumpjetMissileAIUpdate*)getObject()->findUpdateModule(key_JumpjetMissileAIUpdate);

		// if ((missileUpdate) && missileUpdate->isLanding()) {
		if (missileUpdate) {

			Real goalDistance = missileUpdate->getGoalDistance();
			// DEBUG_LOG((">>>JJC: goalDistance = %f; m_landingDistance = %f\n", goalDistance, d->m_landingDistance));
			if (goalDistance > 0.0f && (d->m_landingDistance < 0 || goalDistance < d->m_landingDistance)) {

				if (d->m_conditionFlagFlying != MODELCONDITION_INVALID) {
					rider->clearModelConditionState(d->m_conditionFlagFlying);
				}
				if (d->m_conditionFlagLanding != MODELCONDITION_INVALID) {
					rider->setModelConditionState(d->m_conditionFlagLanding);
				}

				m_landing = true;
			}
		}
	}

	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void JumpjetContain::onContaining(Object* rider, Bool wasSelected)
{
	OpenContain::onContaining(rider, wasSelected);

	// objects inside a transport are held
	rider->setDisabled(DISABLED_HELD);

	// Do we need this status?
	//rider->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_PARACHUTING ) );

	const JumpjetContainModuleData* d = getJumpjetContainModuleData();
	if (!m_landing && d->m_conditionFlagFlying != MODELCONDITION_INVALID) {
		rider->setModelConditionState(d->m_conditionFlagFlying);
	}

	// position him correctly.
	positionRider(rider);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void JumpjetContain::onRemoving(Object* rider)
{
	OpenContain::onRemoving(rider);

	const JumpjetContainModuleData* d = getJumpjetContainModuleData();

	// object is no longer held inside a transport
	rider->clearDisabled(DISABLED_HELD);
	//rider->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_PARACHUTING ) );

	// mark parachute as "no-collisions"... it is just ephemeral at this point,
	// and having the chute collide with the soldier (and both bounce apart) is
	// just dumb-lookin'...
	getObject()->setStatus(MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_NO_COLLISIONS));

	// position him correctly.
	positionRider(rider);


	// Clear condition flags
	if (d->m_conditionFlagLanding != MODELCONDITION_INVALID) {
		rider->clearModelConditionState(d->m_conditionFlagLanding);
	}
	else if (d->m_conditionFlagFlying != MODELCONDITION_INVALID) {
		rider->clearModelConditionState(d->m_conditionFlagFlying);
	}

	// temporarily mark the guy as being allowed to fall 
	// (overriding his locomotor's stick-to-ground attribute).
	// this will be reset (by PhysicsBehavior) when he touches the ground.
	PhysicsBehavior* physics = rider->getPhysics();
	if (physics)
	{
		physics->setAllowToFall(true);

		Coord3D force;
		force.zero();
		physics->applyForce(&force);	// force its physics to wake up... should be done when DISABLED_HELD is cleared, but it not, and scared to do it now.
	}


	AIUpdateInterface* riderAI = rider->getAIUpdateInterface();
	if (riderAI)
	{
		Player* controller = rider->getControllingPlayer();
		if (controller && controller->isSkirmishAIPlayer())
		{
			riderAI->aiHunt(CMD_FROM_AI);	// hunt, as per Dustin's request.
		}
		else
		{
			bool hasRallyPoint = false;
			// Get the transport of the rider
			Object* transport = TheGameLogic->findObjectByID(rider->getProducerID());
			if (transport)
			{
				// Get the building that produced the transport
				Object* transportProducer = TheGameLogic->findObjectByID(transport->getProducerID());
				if (transportProducer)
				{
					// See if we need to set a rally point for the object being parachuted
					ExitInterface* exitInterface = transportProducer->getObjectExitInterface();
					if (exitInterface && exitInterface->useSpawnRallyPoint())
					{
						exitInterface->exitObjectViaDoor(rider, DOOR_1);
						hasRallyPoint = true;
					}
				}
			}

			if (!hasRallyPoint)
				riderAI->aiIdle(CMD_FROM_AI); // become idle.
		}
	}


	// if we land in the water, we die. alas.
	if (d->m_killWhenLandingInWater) {
		const Coord3D* riderPos = rider->getPosition();
		Real waterZ, terrainZ;
		if (TheTerrainLogic->isUnderwater(riderPos->x, riderPos->y, &waterZ, &terrainZ)
			&& riderPos->z <= waterZ + d->m_killWhenLandingInWaterSlop
			&& rider->getLayer() == LAYER_GROUND)
		{
			// don't call kill(); do it manually, so we can specify DEATH_FLOODED
			DamageInfo damageInfo;
			damageInfo.in.m_damageType = DAMAGE_WATER;	// use this instead of UNRESISTABLE so we don't get a dusty damage effect
			damageInfo.in.m_deathType = DEATH_FLOODED;
			damageInfo.in.m_sourceID = INVALID_ID;
			damageInfo.in.m_amount = HUGE_DAMAGE_AMOUNT;
			rider->attemptDamage(&damageInfo);
		}
	}

	// Kill if we landed on impassable ground
	Int cellX = REAL_TO_INT(rider->getPosition()->x / PATHFIND_CELL_SIZE);
	Int cellY = REAL_TO_INT(rider->getPosition()->y / PATHFIND_CELL_SIZE);

	PathfindCell* cell = TheAI->pathfinder()->getCell(rider->getLayer(), cellX, cellY);
	PathfindCell::CellType cellType = cell ? cell->getType() : PathfindCell::CELL_IMPASSABLE;

	// If we land outside the map from a faulty parachute, we die too.  
	// Otherwise we exist outside the PartitionManger like a cheater.
	if (rider->isOffMap()
		|| ((cellType == PathfindCell::CELL_CLIFF) && d->m_killWhenLandingOnCliff)
		|| ((cellType == PathfindCell::CELL_WATER) && d->m_killWhenLandingInWater)
		|| ((cellType == PathfindCell::CELL_IMPASSABLE) && d->m_killWhenLandingOnImpassable)
		)
	{
		// The Paradrop command was legal, the parachute destination was legal, but the parachute
		// can still fail to adjust back on the map.  SO this is the place to cap the cheater.
		rider->kill();
	}

	// Note: The SpecialPower enum determines which locations are allowed, so if we want to actually target
	// water or cliffs, we need to allow it in the ActionManager

}

//-------------------------------------------------------------------------------------------------
void JumpjetContain::positionRider(Object* rider)
{
	rider->setOrientation(getObject()->getOrientation());
}

//-------------------------------------------------------------------------------------------------
void JumpjetContain::positionContainedObjectsRelativeToContainer()
{
	for (ContainedItemsList::const_iterator it = getContainList().begin(); it != getContainList().end(); ++it)
	{
		positionRider(*it);
	}
}

//-------------------------------------------------------------------------------------------------
void JumpjetContain::onDie(const DamageInfo* damageInfo)
{
	// if we are airborne when killed, the guy falls screaming to his death...
	if (getObject()->isSignificantlyAboveTerrain())
	{
		Object* rider = (getContainCount() > 0) ? getContainList().front() : NULL;
		if (rider)
		{
			removeAllContained();
			//const JumpjetContainModuleData* d = getJumpjetContainnModuleData();
			//if (d->m_freeFallDamagePercent > 0.0f)
			//{
			//	// do some damage just for losing your parachute.
			//	// not very realistic, but practical to help ensure that
			//	// you really do die from going "splat" on the ground.
			//	DamageInfo extraDamageInfo;
			//	extraDamageInfo.in.m_damageType = DAMAGE_FALLING;
			//	extraDamageInfo.in.m_deathType = DEATH_SPLATTED;
			//	extraDamageInfo.in.m_sourceID = damageInfo->in.m_sourceID;
			//	extraDamageInfo.in.m_amount = rider->getBodyModule()->getMaxHealth() * d->m_freeFallDamagePercent;
			//	rider->attemptDamage(&extraDamageInfo);
			//}
			PhysicsBehavior* physics = rider->getPhysics();
			if (physics)
			{
				physics->setAllowToFall(true);
				physics->setIsInFreeFall(true);	// bwah ha ha

				Coord3D force;
				force.zero();
				physics->applyForce(&force);	// force its physics to wake up... should be done when DISABLED_HELD is cleared, but it not, and scared to do it now.
			}
		}
	}


	OpenContain::onDie(damageInfo);
}

//-------------------------------------------------------------------------------------------------
void JumpjetContain::onCollide(Object* other, const Coord3D* loc, const Coord3D* normal)
{
	// Note that other == null means "collide with ground"
	if (other == NULL)
	{
		// DEBUG_LOG((">>>JC - onCollide -> kill"));

		// if we're in a container (eg, a transport plane), just ignore this...
		if (getObject()->getContainedBy() != NULL)
			return;

		removeAllContained();

		// TheGameLogic->destroyObject(obj);
		// kill it, so that the chute's SlowDeath will trigger!
		getObject()->kill();
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void JumpjetContain::crc(Xfer* xfer)
{

	// extend base class
	OpenContain::crc(xfer);

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
	// ------------------------------------------------------------------------------------------------
void JumpjetContain::xfer(Xfer* xfer)
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	// extend base class
	OpenContain::xfer(xfer);

	//// landing
	xfer->xferBool(&m_landing);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void JumpjetContain::loadPostProcess(void)
{

	// extend base class
	OpenContain::loadPostProcess();

}  // end loadPostProcess
