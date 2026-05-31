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

// FILE: ParachuteContain.h ////////////////////////////////////////////////////////////////////////
// Author: Steven Johnson, March 2002
// Desc:   Contain module for transport units.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __JumpjetContain_H_
#define __JumpjetContain_H_

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/OpenContain.h"

enum ModelConditionFlagType;

//-------------------------------------------------------------------------------------------------
class JumpjetContainModuleData : public OpenContainModuleData
{
public:
	//Real m_pitchRateMax;
	//Real m_rollRateMax;
	//Real m_lowAltitudeDamping;
	//Real m_paraOpenDist;		///< deploy the parachute when we have traveled this far
	//Real m_freeFallDamagePercent;
	ModelConditionFlagType m_conditionFlagFlying;   ///< ModelConditionState to use when flying
	ModelConditionFlagType m_conditionFlagLanding; ///< ModelConditionState to use before landing
	Real m_landingDistance;   ///< Distance from target to initiate landing state
	Bool m_killWhenLandingInWater;    ///< Kill the Rider when landing on water
	Bool m_killWhenLandingOnCliff;	   ///< Kill the Rider when landing on cliffs
	Bool m_killWhenLandingOnImpassable;   ///< Kill the Rider when landing on impassable terrain
	Real m_killWhenLandingInWaterSlop;  ///< Water Surface offset when killing the rider
	//AudioEventRTS m_parachuteOpenSound;

	JumpjetContainModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
class JumpjetContain : public OpenContain
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(JumpjetContain, "JumpjetContain")
		MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA(JumpjetContain, JumpjetContainModuleData)

public:

	JumpjetContain(Thing* thing, const ModuleData* moduleData);
	// virtual destructor prototype provided by memory pool declaration

	//virtual void onDrawableBoundToObject();

	virtual Bool isValidContainerFor(const Object* obj, bool checkCapacity) const;
	virtual Bool isEnclosingContainerFor(const Object* obj) const { return FALSE; }	///< Does this type of Contain Visibly enclose its contents?
	virtual Bool isSpecialZeroSlotContainer() const { return true; }

	virtual void onContaining(Object* obj, Bool wasSelected);		///< object now contains 'obj'
	virtual void onRemoving(Object* obj);			///< object no longer contains 'obj'

	virtual UpdateSleepTime update();							///< called once per frame

	// virtual void containReactToTransformChange();

	virtual void onCollide(Object* other, const Coord3D* loc, const Coord3D* normal);
	virtual void onDie(const DamageInfo* damageInfo);

	// virtual void setOverrideDestination( const Coord3D *override ); ///< Instead of falling peacefully towards a clear spot, I will now aim here

protected:

	virtual Bool isFullyEnclosingContainer() const { return false; }
	virtual void positionContainedObjectsRelativeToContainer();

private:

	void positionRider(Object* obj);
	/*void updateBonePositions();
	void updateOffsetsFromBones();
	void calcSwayMtx(const Coord3D* offset, Matrix3D* mtx);
	void setSwayMatrices();

	Real m_pitch;
	Real m_roll;
	Real m_pitchRate;
	Real m_rollRate;
	Real m_startZ;
	Bool m_isLandingOverrideSet;
	Coord3D m_landingOverride;
	Coord3D m_riderAttachBone;
	Coord3D m_riderSwayBone;
	Coord3D m_paraAttachBone;
	Coord3D m_paraSwayBone;
	Coord3D m_riderAttachOffset;
	Coord3D m_riderSwayOffset;
	Coord3D m_paraAttachOffset;
	Coord3D m_paraSwayOffset;
	Bool m_needToUpdateRiderBones;
	Bool m_needToUpdateParaBones;
	Bool m_opened;*/

	Bool m_landing;
};

#endif // __JumpjetContain_H_

