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

// FILE:		MissileAIUpdate.h
// Author:	Michael S. Booth, December 2001
// Desc:		Missile behavior

#pragma once

#ifndef _JUMPJET_MISSILE_AI_UPDATE_H_
#define _JUMPJET_MISSILE_AI_UPDATE_H_

#include "Common/GameType.h"
#include "Common/GlobalData.h"
#include "GameLogic/Module/MissileAIUpdate.h"
//#include "GameLogic/WeaponBonusConditionFlags.h"
#include "Common/INI.h"
//#include "WWMath/Matrix3D.h"

enum ParticleSystemID;
//class FXList;


//-------------------------------------------------------------------------------------------------
class JumpjetMissileAIUpdateModuleData : public MissileAIUpdateModuleData
{
public:
	Real					m_initialPitchAngle;      ///< initial pitch angle the missile will be launched at.
	Real					m_scatterRadius;		///< max random offset from target
	Real					m_maxSearchRadius;		///< max radius to search for possible target location
	/*Bool					m_avoidImpassable;		///< try to avoid impassable cells when finding target
	Bool					m_avoidWater;		///< try to avoid water when finding target
	Bool					m_avoidObjects;		///< try to objects when finding target*/
	JumpjetMissileAIUpdateModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);

};

//-------------------------------------------------------------------------------------------------
class JumpjetMissileAIUpdate : public MissileAIUpdate
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JumpjetMissileAIUpdate, "JumpjetMissileAIUpdate")
		MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA(JumpjetMissileAIUpdate, JumpjetMissileAIUpdateModuleData);

public:
	JumpjetMissileAIUpdate(Thing* thing, const ModuleData* moduleData);

	virtual void projectileFireAtObjectOrPosition(const Object* victim, const Coord3D* victimPos, const WeaponTemplate* detWeap, const ParticleSystemTemplate* exhaustSysOverride);
	virtual Bool projectileHandleCollision(Object* other);
	// virtual Bool processCollision(PhysicsBehavior *physics, Object *other); ///< Returns true if the physics collide should apply the force.  Normally not.  jba.

	virtual Bool canLaunchToPosition(const Coord3D* targetPos, Coord3D* newPos);

	// virtual UpdateSleepTime update();
	virtual void onDelete(void);

	// Bool isLanding( void );
	Real getGoalDistance(void);

protected:

	void detonate();

private:

	/*MissileStateType			m_state;									///< the behavior state of the missile
	UnsignedInt						m_stateTimestamp;					///< time of state change
	UnsignedInt						m_nextTargetTrackTime;		///< if nonzero, how often we update our target pos
	ObjectID							m_launcherID;							///< ID of object that launched us (INVALID_ID if not yet launched)
	ObjectID							m_victimID;								///< ID of object that I am rocketing towards (INVALID_ID if not yet launched)
	UnsignedInt						m_fuelExpirationDate;			///< how long 'til we run out of fuel
	Real									m_noTurnDistLeft;					///< when zero, ok to start turning
	Real									m_maxAccel;
	Coord3D								m_originalTargetPos;			///< When firing uphill, we aim high to clear the brow of the hill.  jba.
	Coord3D								m_prevPos;
	WeaponBonusConditionFlags		m_extraBonusFlags;
	const WeaponTemplate*	m_detonationWeaponTmpl;		///< weapon to fire at end (or null)
	const ParticleSystemTemplate* m_exhaustSysTmpl;
	ParticleSystemID			m_exhaustID;								///< our exhaust particle system (if any)
	UnsignedInt						m_framesTillDecoyed;			///< Number of frames before missile will get distracted by decoy countermeasures.
	Bool									m_isTrackingTarget;				///< Was I originally shot at a moving object?
	Bool									m_isArmed;								///< if true, missile will explode on contact
	Bool									m_noDamage;								///< if true, missile will not cause damage when it detonates. (Used for flares).
	Bool									m_isJammed;								///< No target, just shooting at a scattered position

	void doPrelaunchState();
	void doLaunchState();
	void doIgnitionState();
	void doAttackState(Bool turnOK);
	void doKillState();
	void doKillSelfState();
	void doDeadState();

	void airborneTargetGone();											///< My airborne target has died, so I have to do something cool to make up for that

	void tossExhaust();
	void switchToState(MissileStateType s);*/

	Bool findOffsetPosition(const Coord3D* targetPos, Coord3D* newPos, Real minRadius, Real maxRadius);

};

#endif // _JUMPJET_MISSILE_AI_UPDATE_H_

