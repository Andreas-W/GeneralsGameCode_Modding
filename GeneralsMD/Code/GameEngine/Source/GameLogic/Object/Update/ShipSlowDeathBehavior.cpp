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
#define DEFINE_SHIP_TOPPLE_NAMES
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
	m_fxStartTopple = NULL;
	m_oclStartTopple = NULL;
	m_fxStartSink = NULL;
	m_oclStartSink = NULL;
	//m_toppleDamping = 1.0;
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

		{ "WobbleMaxAnglePitch", INI::parseAngleReal, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_wobbleMaxPitch) },
		{ "WobbleMaxAngleYaw", INI::parseAngleReal, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_wobbleMaxYaw) },
		{ "WobbleMaxAngleRoll", INI::parseAngleReal, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_wobbleMaxRoll) },
		{ "WobbleInterval", INI::parseDurationUnsignedInt, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_wobbleInterval) },

		{ "ToppleStyle", INI::parseIndexList, TheShipToppleNames, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleType) },
		{ "ToppleFrontMinPitch", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleFrontMinPitch) },
		{ "ToppleFrontMaxPitch", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleFrontMaxPitch) },
		{ "ToppleBackMinPitch", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleBackMinPitch) },
		{ "ToppleBackMaxPitch", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleBackMaxPitch) },
		{ "ToppleSideMinRoll", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleSideMinRoll) },
		{ "ToppleSideMaxRoll", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleSideMaxRoll) },
		{ "ToppleMinHeightOffset", INI::parseReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleHeightMinOffset) },
		{ "ToppleMaxHeightOffset", INI::parseReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleHeightMaxOffset) },
		{ "ToppleDuration", INI::parseDurationUnsignedInt, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleDuration) },
		{ "ToppleDurationVariance", INI::parseDurationUnsignedInt, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleDurationVariance) },

		{ "ToppleMinPushForce", INI::parseReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleMinPushForce) },
		{ "ToppleMaxPushForce", INI::parseReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleMaxPushForce) },
		{ "ToppleMinPushForceSide", INI::parseReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleMinPushForceSide) },
		{ "ToppleMaxPushForceSide", INI::parseReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleMaxPushForceSide) },


		{ "FXStartTopple", INI::parseFXList, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_fxStartTopple) },
		{ "OCLStartTopple", INI::parseObjectCreationList, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_oclStartTopple) },

		//{ "ToppleDampingDuration", INI::parseDurationUnsignedInt, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleDampingDuration) },
		//{ "ToppleDamping", INI::parseReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleDamping) },
		{ "ToppleAngleCorrectionRate", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_toppleAngleCorrectionRate) },

		//{ "SinkWobbleMaxAnglePitch", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_sinkWobbleMaxPitch) },
		//{ "SinkWobbleMaxAngleYaw", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_sinkWobbleMaxYaw) },
		//{ "SinkWobbleMaxAngleRoll", INI::parseAngleReal, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_sinkWobbleMaxRoll) },
		//{ "SinkWobbleInterval", INI::parseDurationUnsignedInt, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_sinkWobbleInterval) },
		{ "SinkHowFast", INI::parsePercentToReal, NULL, offsetof( ShipSlowDeathBehaviorModuleData, m_sinkHowFast) },
		{ "FXStartSink", INI::parseFXList, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_fxStartSink) },
		{ "OCLStartSink", INI::parseObjectCreationList, NULL, offsetof(ShipSlowDeathBehaviorModuleData, m_oclStartSink) },


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

	m_toppleVarianceRoll = GameLogicRandomValueReal(0.0f, 1.0f);

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
		doWobble();
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
		doWobble();
		return UPDATE_SLEEP_NONE;
	}
	else {
		if (!m_shipSinkStarted)
			beginSinkPhase();

		doSinkPhase();
		doWobble();
		//return UPDATE_SLEEP_NONE;
	}

	// Check for crashing onto ground

	Object* obj = getObject();
	if (obj->getHeightAboveTerrain() <= 1.0) {

		// make effect
		FXList::doFXObj(d->m_fxHitGround, obj);
		ObjectCreationList::create(d->m_oclHitGround, obj, NULL);

		// Stop the sound from playing.
		TheAudio->removeAudioEvent(m_deathSound.getPlayingHandle());

		// destroy the ship finally
		TheGameLogic->destroyObject(obj);
	}


	return UPDATE_SLEEP_NONE;

}

// -------------
// Utils
inline Real smoothStep(Real x)
{
	x = WWMath::Clamp(x, 0.0f, 1.0f);
	return x * x * (3.0f - 2.0f * x);
}

inline Real smoothPlateau(Real x, Real e)
{
	x = std::clamp(x, 0.0f, 1.0f);
	e = std::clamp(e, 0.0f, 0.5f);

	if (e == 0.0f)
		return (x > 0.0f && x < 1.0f) ? 1.0f : 0.0f;

	// Ease-in region
	if (x < e)
	{
		Real t = x / e; // map to 0..1
		return smoothStep(t); // smoothstep
	}

	// Flat top
	if (x <= 1.0f - e)
	{
		return 1.0f;
	}

	// Ease-out region
	Real t = (x - (1.0f - e)) / e; // map to 0..1
	Real s = smoothStep(t); // smoothstep
	return 1.0f - s;
}

inline Real fastSin01(Real x)
{
	x = x * 2.0f * PI - PI;

	// Optional: wrap input to [-pi, pi] for stability
	x = std::fmod(x + PI, 2.0f * PI);
	if (x < 0.0f) x += 2.0f * PI;
	x -= PI;

	// Parabolic approximation
	const Real B = 4.0f / PI;
	const Real C = -4.0f / (PI * PI);

	Real y = B * x + C * x * std::fabs(x);

	// Extra precision correction (improves peaks)
	const Real P = 0.225f;
	y = P * (y * std::fabs(y) - y) + y;

	return y;
}


// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::beginInitPhase()
{

}
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::beginTopplePhase()
{
	const ShipSlowDeathBehaviorModuleData* d = getShipSlowDeathBehaviorModuleData();
	Object* obj = getObject();
	// make effect
	FXList::doFXObj(d->m_fxStartTopple, obj);
	ObjectCreationList::create(d->m_oclStartTopple, obj, NULL);

}
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::beginSinkPhase()
{
	const ShipSlowDeathBehaviorModuleData* d = getShipSlowDeathBehaviorModuleData();
	Object* obj = getObject();
	// make effect
	FXList::doFXObj(d->m_fxStartTopple, obj);
	ObjectCreationList::create(d->m_oclStartTopple, obj, NULL);

	m_deathSound = d->m_deathSound;

	if (m_deathSound.getEventName().isEmpty() == false)
	{
		m_deathSound.setObjectID(getObject()->getID());
		m_deathSound.setPlayingHandle(TheAudio->addAudioEvent(&m_deathSound));
	}

	if (obj->getAIUpdateInterface()) {
			Locomotor* locomotor = obj->getAIUpdateInterface()->getCurLocomotor();
			if (locomotor)
				locomotor->setMaxLift(-TheGlobalData->m_gravity * (1.0f - d->m_sinkHowFast));
	}

}
// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::doInitPhase()
{
	DEBUG_LOG((">>> SSD doInitPhase 0"));

	//const ShipSlowDeathBehaviorModuleData* d = getShipSlowDeathBehaviorModuleData();


	/*const Matrix3D* instMatrix = obj->getDrawable()->getInstanceMatrix();
	Matrix3D newInstMatrix;
	newInstMatrix = *instMatrix;
	newInstMatrix.Set_Translation(shudder);*/

}
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::doTopplePhase()
{
	DEBUG_LOG((">>> SSD doTopplePhase 0"));

		const ShipSlowDeathBehaviorModuleData* d = getShipSlowDeathBehaviorModuleData();

		if (d->m_toppleDuration <= 0)
			return;

		Object* obj = getObject();

		UnsignedInt now = TheGameLogic->getFrame();
		UnsignedInt timePassed = now - m_toppleStartFrame;
		Real p = INT_TO_REAL(timePassed) / INT_TO_REAL(d->m_toppleDuration);

		Real progressAngle = smoothStep(p); //TODO: apply sinus curve
		Real progressHeight = smoothStep(p);
		Real progressPush = smoothPlateau(p, 0.5);

		Matrix3D mtx = *obj->getTransformMatrix();

		Real targetPitch = 0.0f;
		Real targetYaw = 0.0f;
		Real targetRoll = 0.0f;
		Real targetZ = 0.0f;

		const Coord3D* dir = obj->getUnitDirectionVector2D();
		Coord3D force;

		Real accelForce;
		//force.x = accelForce * dir->x;
		//force.y = accelForce * dir->y;
		force.z = 0.0f;

		// Topple the boat
		switch (m_chosenToppleType)
		{
			case TOPPLE_FRONT:
				targetPitch = progressAngle * WWMath::Lerp(d->m_toppleFrontMinPitch, d->m_toppleFrontMaxPitch, m_toppleVarianceRoll);
				accelForce = progressPush * WWMath::Lerp(d->m_toppleMinPushForce, d->m_toppleMaxPushForce, m_toppleVarianceRoll);
				force.x = accelForce * -dir->x;
				force.y = accelForce * -dir->y;
				break;
			case TOPPLE_BACK:
				targetPitch = -progressAngle * WWMath::Lerp(d->m_toppleBackMinPitch, d->m_toppleBackMaxPitch, m_toppleVarianceRoll);
				accelForce = progressPush * WWMath::Lerp(d->m_toppleMinPushForce, d->m_toppleMaxPushForce, m_toppleVarianceRoll);
				force.x = accelForce * dir->x;
				force.y = accelForce * dir->y;
				break;
			case TOPPLE_SIDE_LEFT:
				targetRoll = progressAngle * WWMath::Lerp(d->m_toppleSideMinRoll, d->m_toppleSideMaxRoll, m_toppleVarianceRoll);
				accelForce = progressPush * WWMath::Lerp(d->m_toppleMinPushForceSide, d->m_toppleMaxPushForceSide, m_toppleVarianceRoll);
				force.x = accelForce * -dir->y;
				force.y = accelForce * dir->x;
				break;
			case TOPPLE_SIDE_RIGHT:
				targetRoll = -progressAngle * WWMath::Lerp(d->m_toppleSideMinRoll, d->m_toppleSideMaxRoll, m_toppleVarianceRoll);
				accelForce = progressPush * WWMath::Lerp(d->m_toppleMinPushForceSide, d->m_toppleMaxPushForceSide, m_toppleVarianceRoll);
				force.x = accelForce * dir->y;
				force.y = accelForce * -dir->x;
				break;
			case TOPPLE_NONE:
				return;
			default:
				DEBUG_CRASH(("Invalid topple type in ShipSlowDeathBehavior"));
		}

		PhysicsBehavior* physics = obj->getPhysics();
		DEBUG_ASSERTCRASH(physics, ("ShipSlowDeathBehavior: object '%s' does not have a physics module",
			obj->getTemplate()->getName().str()));
		physics->applyMotiveForce(&force);

		targetZ = progressHeight * WWMath::Lerp(d->m_toppleHeightMinOffset, d->m_toppleHeightMaxOffset, m_toppleVarianceRoll);

		// const Coord3D* pos = obj->getPosition();

		Real deltaPitch = targetPitch - m_curPitch;
		Real deltaYaw = targetYaw - m_curYaw;
		Real deltaRoll = targetRoll - m_curRoll;
		Real deltaZ = targetZ - m_curZ;

		Real R2D = 180.0 / PI;
		DEBUG_LOG((">>> SSD TopplePhase: target(P/Y/R/z) = (%f, %f, %f, %f), delta(P/Y/R/Z) = (%f, %f, %f, %f)",
			targetPitch*R2D, targetYaw*R2D, targetRoll*R2D, targetZ, deltaPitch*R2D, deltaYaw*R2D, deltaRoll*R2D, deltaZ));


		mtx.Rotate_X(deltaRoll);
		mtx.Rotate_Y(deltaPitch);
		mtx.Rotate_Z(deltaYaw);

		//mtx.Adjust_Z_Translation(deltaZ);

		obj->setTransformMatrix(&mtx);
		// obj->setPositionZ(pos->z + deltaZ);

		m_curPitch += deltaPitch;
		m_curYaw += deltaYaw;
		m_curRoll += deltaRoll;
		m_curZ += deltaZ;

		if (obj->getAIUpdateInterface())
		{
			Locomotor* locomotor = obj->getAIUpdateInterface()->getCurLocomotor();
			if (locomotor) {
				locomotor->setPreferredHeight(locomotor->getPreferredHeight() + deltaZ);
			}
		}

}

inline Real getDelta(Real angle, Real delta) {
	if (angle == 0 || delta == 0)
		return 0.0f;
	if (angle < 0)
		return MIN(-angle, delta);
	else
		return -MIN(angle, delta);
}

// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::doSinkPhase()
{
	DEBUG_LOG((">>> SSD doSinkPhase 0"));

	const ShipSlowDeathBehaviorModuleData* d = getShipSlowDeathBehaviorModuleData();

	Object* obj = getObject();

	//if (d->m_toppleDamping < 1.0) {
	{
		Matrix3D mtx = *obj->getTransformMatrix();

		//Real targetPitch = m_curPitch * d->m_toppleDamping;
		//Real targetYaw = m_curYaw * d->m_toppleDamping;
		//Real targetRoll = m_curRoll * d->m_toppleDamping;

		//Real deltaPitch = targetPitch - m_curPitch;
		//Real deltaYaw = targetYaw - m_curYaw;
		//Real deltaRoll = targetRoll - m_curRoll;

		Real angleRate = d->m_toppleAngleCorrectionRate;

		Real deltaPitch = getDelta(m_curPitch, angleRate);
		Real deltaYaw = getDelta(m_curYaw, angleRate);
		Real deltaRoll = getDelta(m_curRoll, angleRate);

		Real R2D = 180.0 / PI;
		DEBUG_LOG((">>> SINK TOPPLE CORRECTION, curAngle(P/Y/R) = (%f, %f, %f), delta(P/Y/R) = (%f, %f, %f)",
			m_curPitch*R2D, m_curYaw*R2D, m_curRoll*R2D,
			deltaPitch*R2D, deltaYaw*R2D, deltaRoll*R2D
			));

		mtx.Rotate_X(deltaRoll);
		mtx.Rotate_Y(deltaPitch);
		mtx.Rotate_Z(deltaYaw);
	  
		obj->setTransformMatrix(&mtx);

		m_curPitch += deltaPitch;
		m_curYaw += deltaYaw;
		m_curRoll += deltaRoll;

	}

	//// do the wobble
	//if (d->m_sinkWobbleInterval > 0) {
	//	UnsignedInt now = TheGameLogic->getFrame();
	//	UnsignedInt timePassed = now - m_sinkStartFrame;
	//	Real pInterval = INT_TO_REAL(timePassed % d->m_sinkWobbleInterval) / INT_TO_REAL(d->m_sinkWobbleInterval);

	//	// short ease in
	//	Real pTotal = INT_TO_REAL(timePassed) / 30.0;  // 1sec

	//	Real progressTotal = smoothStep(pTotal);

	//	Drawable* draw = getObject()->getDrawable();
	//	if (draw) {
	//		Matrix3D mx = *draw->getInstanceMatrix();

	//		//Real pitch = progressInterval * progressTotal * d->m_initWobbleMaxPitch;
	//		//Real yaw = progressInterval * progressTotal * d->m_initWobbleMaxYaw;
	//		//Real roll = progressInterval * progressTotal * d->m_initWobbleMaxRoll;

	//		Real pitch = sin(pInterval * TWO_PI) * progressTotal * d->m_sinkWobbleMaxPitch;
	//		Real yaw = cos(pInterval * TWO_PI) * progressTotal * d->m_sinkWobbleMaxYaw;
	//		Real roll = -sin(pInterval * TWO_PI * 2) * progressTotal * d->m_sinkWobbleMaxRoll;

	//		mx.Make_Identity();
	//		mx.Rotate_Z(roll);
	//		mx.Rotate_Y(yaw);
	//		mx.Rotate_X(pitch);

	//		draw->setInstanceMatrix(&mx);
	//	}
	// }

}
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::doWobble()
{

	const ShipSlowDeathBehaviorModuleData* d = getShipSlowDeathBehaviorModuleData();

	if (d->m_wobbleInterval <= 0)
		return;

	// do the wobble
	UnsignedInt now = TheGameLogic->getFrame();
	UnsignedInt timePassed = now - m_initStartFrame;
	Real pInterval = INT_TO_REAL(timePassed % d->m_wobbleInterval) / INT_TO_REAL(d->m_wobbleInterval);

	// short ease in
	Real pTotal = INT_TO_REAL(timePassed) / 30.0;  // 1sec
	Real progressTotal = smoothStep(pTotal);


	Drawable* draw = getObject()->getDrawable();
	if (draw) {
		Matrix3D mx = *draw->getInstanceMatrix();

		//Real pitch = progressInterval * progressTotal * d->m_initWobbleMaxPitch;
		//Real yaw = progressInterval * progressTotal * d->m_initWobbleMaxYaw;
		//Real roll = progressInterval * progressTotal * d->m_initWobbleMaxRoll;

		Real pitch = sin(pInterval * TWO_PI) * progressTotal * d->m_wobbleMaxPitch;
		Real yaw = cos(pInterval * TWO_PI) * progressTotal * d->m_wobbleMaxYaw;
		Real roll = -sin(pInterval * TWO_PI * 2) * progressTotal * d->m_wobbleMaxRoll;

		mx.Make_Identity();
		mx.Rotate_Z(roll);
		mx.Rotate_Y(yaw);
		mx.Rotate_X(pitch);

		draw->setInstanceMatrix(&mx);
	}
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

	xfer->xferReal( &m_toppleVarianceRoll);

	xfer->xferReal( &m_curPitch);
	xfer->xferReal( &m_curYaw);
	xfer->xferReal( &m_curRoll);
	xfer->xferReal( &m_curZ);

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void ShipSlowDeathBehavior::loadPostProcess( void )
{

	// extend base class
	SlowDeathBehavior::loadPostProcess();

}
