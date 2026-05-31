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

// FILE: MissileAIUpdate.cpp
// Author: Michael S. Booth, December 2001
// Desc:   Implementation of missile behavior

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Thing.h"
#include "Common/ThingTemplate.h"
#include "Common/RandomValue.h"
#include "Common/BitFlagsIO.h"

#include "GameLogic/AIPathfind.h"
#include "GameLogic/ExperienceTracker.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/JumpjetMissileAIUpdate.h"
#include "GameLogic/Module/MissileAIUpdate.h"
#include "GameLogic/Module/ContainModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Object.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Weapon.h"

#include "GameClient/Drawable.h"
#include "GameClient/FXList.h"
#include "GameClient/ParticleSys.h"

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
JumpjetMissileAIUpdateModuleData::JumpjetMissileAIUpdateModuleData()
{
	m_initialPitchAngle = 0.0f;
	m_scatterRadius = 0.0f;
	m_maxSearchRadius = 100.0f;
	//m_avoidImpassable = TRUE;
	//m_avoidObjects = TRUE;
	//m_avoidWater = TRUE;
}

//-----------------------------------------------------------------------------
void JumpjetMissileAIUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	MissileAIUpdateModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "ScatterRadius",	INI::parseReal, NULL, offsetof(JumpjetMissileAIUpdateModuleData, m_scatterRadius) },
		{ "MaxSearchRadius",	INI::parseReal, NULL, offsetof(JumpjetMissileAIUpdateModuleData, m_maxSearchRadius) },
		{ "InitialPitchAngle",	INI::parseAngleReal, NULL, offsetof(JumpjetMissileAIUpdateModuleData, m_initialPitchAngle) },
		//{ "TryToAvoidImpassable",	INI::parseBool, NULL, offsetof(JumpjetMissileAIUpdateModuleData, m_avoidImpassable) },
		//{ "TryToAvoidWater",	INI::parseBool, NULL, offsetof(JumpjetMissileAIUpdateModuleData, m_avoidWater) },
		//{ "TryToAvoidObjects",	INI::parseBool, NULL, offsetof(JumpjetMissileAIUpdateModuleData, m_avoidObjects) },
		{ 0, 0, 0, 0 }
	};

	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
JumpjetMissileAIUpdate::JumpjetMissileAIUpdate(Thing* thing, const ModuleData* moduleData) : MissileAIUpdate(thing, moduleData)
{
	// const JumpjetMissileAIUpdateModuleData* d = getJumpjetMissileAIUpdateModuleData();
}

//-------------------------------------------------------------------------------------------------
JumpjetMissileAIUpdate::~JumpjetMissileAIUpdate()
{

}

//-------------------------------------------------------------------------------------------------
void JumpjetMissileAIUpdate::onDelete(void)
{
	MissileAIUpdate::onDelete();
}

//-------------------------------------------------------------------------------------------------
// The actual firing of the missile once setup.
//-------------------------------------------------------------------------------------------------
void JumpjetMissileAIUpdate::projectileFireAtObjectOrPosition(const Object* victim, const Coord3D* victimPos, const WeaponTemplate* detWeap, const ParticleSystemTemplate* exhaustSysOverride)
{
	const JumpjetMissileAIUpdateModuleData* d = getJumpjetMissileAIUpdateModuleData();

	if (d->m_initialPitchAngle != 0.0f) {
		Matrix3D mtx = *getObject()->getTransformMatrix();
		mtx.Rotate_Y(-(d->m_initialPitchAngle));
		getObject()->setTransformMatrix(&mtx);
	}

	MissileAIUpdate::projectileFireAtObjectOrPosition(victim, victimPos, detWeap, exhaustSysOverride);
}

//-------------------------------------------------------------------------------------------------
Bool JumpjetMissileAIUpdate::canLaunchToPosition(const Coord3D* targetPos, Coord3D* newTargetPos, Bool keepFormation) {

	const JumpjetMissileAIUpdateModuleData* d = getJumpjetMissileAIUpdateModuleData();
	//Coord3D newTargetPos = Coord3D();
	Bool positionFound = false;

	if (keepFormation) {
		// Formation group launch: the target is already this unit's formation slot, so don't
		// random-scatter. Start at the exact position and expand only enough to dodge occupied cells.
		positionFound = findOffsetPosition(targetPos, newTargetPos, 0.0f, d->m_scatterRadius);
	}
	else {
		// Look for suitable position (with scatter radius)
		// if (d->m_scatterRadius > 0.0f) {
		positionFound = findOffsetPosition(targetPos, newTargetPos, d->m_scatterRadius * 0.5f, d->m_scatterRadius);
		//}
	}

	// If we found none, increase search radius
	if (!positionFound) {
		positionFound = findOffsetPosition(targetPos, newTargetPos, d->m_scatterRadius, d->m_maxSearchRadius);
	}

	// If we still haven't found one, abort.
	if (!positionFound) {
		DEBUG_LOG((">>>JumpjetMissileAIUpdate: No valid position found. Abort Launch!\n"));
		return false;
	}

	return true;
}


//-------------------------------------------------------------------------------------------------
Bool JumpjetMissileAIUpdate::findOffsetPosition(const Coord3D* targetPos, Coord3D* newPos, Real minRadius, Real maxRadius)
{
	FindPositionOptions fpOptions;
	fpOptions.minRadius = GameLogicRandomValueReal(0, minRadius);
	fpOptions.maxRadius = maxRadius;
	fpOptions.flags = FPF_USE_HIGHEST_LAYER;


	// const JumpjetMissileAIUpdateModuleData* d = getJumpjetMissileAIUpdateModuleData();
	//if (!d->m_avoidWater) fpOptions.flags |= FPF_IGNORE_WATER;
	//if (!d->m_avoidObjects) fpOptions.flags |= FPF_IGNORE_ALL_OBJECTS;

	if (ThePartitionManager->findPositionAround(targetPos, &fpOptions, newPos)) {
		return TheAI->pathfinder()->adjustToLandingDestination(getObject(), newPos);
	}

	return false;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
Bool JumpjetMissileAIUpdate::projectileHandleCollision(Object* other)
{
	Object* obj = getObject();
	// const JumpjetMissileAIUpdateModuleData* d = getJumpjetMissileAIUpdateModuleData();

	if (other == NULL) {
		// we hit the ground.  Check to see if we hit something unexpected.
		Coord3D goal = *getGoalPosition();
		Coord3D pos = *obj->getPosition();
		Coord3D delta;
		delta.x = pos.x - goal.x;
		delta.y = pos.y - goal.y;
		delta.z = pos.z - goal.z;
		if (delta.z > PATHFIND_CELL_SIZE_F) {
			// we're above our target goal.
			if (delta.length() > 3 * PATHFIND_CELL_SIZE_F) {
				// we're somewhere else.
				return true;
			}
		}
		// DEBUG_LOG((">>>JJMAU - projectileHandleCollision - We hit the ground.\n"));
	}

	if (other != NULL)
	{
		// We do not want to collide with other objects
		// TODO: review this

		return true;
	}
	// collided with something... blow'd up!
	// DEBUG_LOG((">>>JJMAU - detonate (projectileHandleCollission).\n"));
	detonate();

	// mark ourself as "no collisions" (since we might still exist in slow death mode)
	obj->setStatus(MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_NO_COLLISIONS));
	return true;
}

//-------------------------------------------------------------------------------------------------
void JumpjetMissileAIUpdate::detonate()
{
	// DEBUG_LOG((">>>JJMAU - detonate.\n"));

	Object* obj = getObject();

	if (obj->getDrawable())
		obj->getDrawable()->setDrawableHidden(true);
	// Delay destroying the object two frames to let the contrail catch up. jba.

	switchToState(KILL_SELF);

	obj->setStatus(MAKE_OBJECT_STATUS_MASK(OBJECT_STATUS_MISSILE_KILLING_SELF));

}

// ------------------------------------------------------------------------------------------------
//Bool JumpjetMissileAIUpdate::isLanding()
//{
//	return getMissileState() == ATTACK;
//}

// ----------------
Real JumpjetMissileAIUpdate::getGoalDistance()
{
	// TODO: We could move this to the JumpjetContain module instead.
	// i.e. when the missile is launched, pass the GoalPosition to the JumpjetContain
	//  and check for proximity there
	MissileStateType state = getMissileState();
	//DEBUG_LOG((">>>JJMAU: getGoalDistance - state = %d, ", state));
	if (state >= ATTACK) {
		Coord3D goal = *getGoalPosition();
		Coord3D pos = *getObject()->getPosition();
		Coord3D delta;
		delta.x = pos.x - goal.x;
		delta.y = pos.y - goal.y;
		delta.z = pos.z - goal.z;
		// DEBUG_LOG(("delta.length = %f\n", delta.length()));
		return delta.length();
	}
	return -1.0f;
}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void JumpjetMissileAIUpdate::crc(Xfer* xfer)
{
	// extend base class
	MissileAIUpdate::crc(xfer);
}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
	// ------------------------------------------------------------------------------------------------
void JumpjetMissileAIUpdate::xfer(Xfer* xfer)
{
	// version
	const XferVersion currentVersion = 6;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	// extend base class
	MissileAIUpdate::xfer(xfer);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void JumpjetMissileAIUpdate::loadPostProcess(void)
{
	// extend base class
	MissileAIUpdate::loadPostProcess();
}  // end loadPostProcess
