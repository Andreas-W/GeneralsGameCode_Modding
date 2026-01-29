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

// FILE: ShipSlowDeathBehavior.h //////////////////////////////////////////////////////////////
// Author: Andi W, 01 2026
// Desc:   ship slow deaths
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "Common/AudioEventRTS.h"
#include "GameLogic/Module/SlowDeathBehavior.h"

// FORWARD DECLARATIONS ///////////////////////////////////////////////////////////////////////////
class ParticleSystemTemplate;
enum ModelConditionFlagType CPP_11(: Int);

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
enum ShipToppleType CPP_11(: Int)
{
	TOPPLE_NONE = 0,
	TOPPLE_FRONT,
	TOPPLE_BACK,
	TOPPLE_SIDE_LEFT,
	TOPPLE_SIDE_RIGHT,
	TOPPLE_SIDE,
	TOPPLE_RANDOM,

	TOPPLE_COUNT
};

#ifdef DEFINE_SHIP_TOPPLE_NAMES
static const char* const TheShipToppleNames[] =
{
	"NONE",
	"FRONT",
	"BACK",
	"LEFT",
	"RIGHT",
	"SIDE",
	"RANDOM",
	NULL
};
static_assert(ARRAY_SIZE(TheShipToppleNames) == TOPPLE_COUNT + 1, "Incorrect array size");
#endif

//-------------------------------------------------------------------------------------------------
class ShipSlowDeathBehaviorModuleData : public SlowDeathBehaviorModuleData
{

public:

	ShipSlowDeathBehaviorModuleData( void );

	static void buildFieldParse(MultiIniFieldParse &p );

	UnsignedInt m_initialDelay;
	UnsignedInt m_initialDelayVariance;
	ShipToppleType m_toppleType;

	Real m_wobbleMaxPitch;
	Real m_wobbleMaxYaw;
	Real m_wobbleMaxRoll;
	UnsignedInt m_wobbleInterval;

	Real m_toppleFrontMinPitch;
	Real m_toppleFrontMaxPitch;
	Real m_toppleBackMinPitch;
	Real m_toppleBackMaxPitch;
	Real m_toppleSideMinRoll;
	Real m_toppleSideMaxRoll;
	Real m_toppleHeightMinOffset;
	Real m_toppleHeightMaxOffset;
	//Real m_toppleDamping;
	Real m_toppleAngleCorrectionRate;
	UnsignedInt m_toppleDuration;
	UnsignedInt m_toppleDurationVariance;

	Real m_toppleMinPushForce;
	Real m_toppleMaxPushForce;
	Real m_toppleMinPushForceSide;
	Real m_toppleMaxPushForceSide;

	//Real m_sinkWobbleMaxPitch;
	//Real m_sinkWobbleMaxYaw;
	//Real m_sinkWobbleMaxRoll;
	//UnsignedInt m_sinkWobbleInterval;

	Real m_sinkHowFast;

	AudioEventRTS m_deathSound;						///< Sound played during death sequence.
	const ObjectCreationList* m_oclEjectPilot;	///< OCL for ejecting pilot

	const FXList* m_fxHitGround;					///< fx for hitting the ground
	const ObjectCreationList* m_oclHitGround;  ///< OCL at hit ground event

	const FXList* m_fxStartTopple;					///< fx after init phase
	const ObjectCreationList* m_oclStartTopple;  ///< OCL  after init phase

	const FXList* m_fxStartSink;					///< fx after topple / before sinking phase
	const ObjectCreationList* m_oclStartSink;  ///< OCL  after topple / before sinking phase

	const ParticleSystemTemplate* m_attachParticleSystem;		///< particle system to attach while sinking

	std::vector<AsciiString> m_attachParticleBoneNames;


	ModelConditionFlagType m_conditionFlagInit;
	ModelConditionFlagType m_conditionFlagTopple;
	ModelConditionFlagType m_conditionFlagSink;
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class ShipSlowDeathBehavior : public SlowDeathBehavior
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( ShipSlowDeathBehavior, "ShipSlowDeathBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( ShipSlowDeathBehavior, ShipSlowDeathBehaviorModuleData )

public:

	ShipSlowDeathBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void beginSlowDeath( const DamageInfo *damageInfo );	///< begin the slow death cycle
	virtual UpdateSleepTime update();

protected:

	void doInitPhase();
	void doTopplePhase();
	void doSinkPhase();

	void doWobble();

	void beginInitPhase();
	void beginTopplePhase();
	void beginSinkPhase();

	// @todo propagate this up to SlowDeathBehavior. I don't wanna do it today, cause its 4/3. jkmcd
	AudioEventRTS m_deathSound;						///< Sound played during death sequence.

	UnsignedInt m_initStartFrame;
	UnsignedInt m_toppleStartFrame;
	UnsignedInt m_sinkStartFrame;
	Bool m_shipSinkStarted;
	Int m_chosenToppleType;

	Real m_toppleVarianceRoll;

	Real m_curPitch;
	Real m_curYaw;
	Real m_curRoll;
	Real m_curZ;
};
