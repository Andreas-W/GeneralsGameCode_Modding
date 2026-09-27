#pragma once

#ifndef __RIFTSlowDeathBehavior_H_
#define __RIFTSlowDeathBehavior_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/KindOf.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/SlowDeathBehavior.h"

class FXList;
class ObjectCreationList;
class Object;

// ------------------------------------------------------------------------------------------------
/** The rift runs through a fixed sequence of phases once the slow death is activated. Each phase
	* yields a "strength" in the 0..1 range that scales suction and damage. */
// ------------------------------------------------------------------------------------------------
enum RiftPhaseType
{
	RIFTPHASE_INACTIVE = 0,	///< slow death not begun yet
	RIFTPHASE_CHARGE,				///< initial delay; charge FX only, nothing is pulled yet
	RIFTPHASE_RAMPUP,				///< strength climbs 0 -> 1
	RIFTPHASE_MAIN,					///< full strength
	RIFTPHASE_FADEOUT,			///< strength falls 1 -> 0
	RIFTPHASE_DONE					///< collapsed; waiting for the base class to destroy us
};

// ------------------------------------------------------------------------------------------------
/** State we keep for each object the rift currently holds. We cannot re-derive this from the
	* world each frame: the capture flags have to be undone on release, the one-shot setup must not
	* re-fire, and objects without a PhysicsBehavior have nowhere else to store their velocity. */
// ------------------------------------------------------------------------------------------------
struct RiftVictim
{
	enum
	{
		RIFTSAVE_WAS_DISABLED_FREEFALL	= 0x01,	///< victim was already in freefall before we grabbed it
		RIFTSAVE_WAS_IN_FREEFALL				= 0x02,	///< physics IS_IN_FREEFALL was already set
		RIFTSAVE_USES_LOCOMOTOR					= 0x04	///< flyer we steer via its own locomotor
	};

	ObjectID m_id;
	Coord3D m_kinematicVel;				///< only used when the victim has no PhysicsBehavior
	Int m_spinSign;								///< +1/-1, drawn once at capture
	UnsignedByte m_savedFlags;		///< RIFTSAVE_* bits
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class RiftSlowDeathBehaviorModuleData : public SlowDeathBehaviorModuleData
{

public:

	RiftSlowDeathBehaviorModuleData(void);

	static void buildFieldParse(MultiIniFieldParse& p);

	UnsignedInt m_initialDelay;					///< Frames before effect starts
	UnsignedInt m_rampupTime;						///< Frames till full effect
	UnsignedInt m_mainDuration;					///< Frames with full effect after rampupTime
	UnsignedInt m_fadeOutTime;					///< Frames till zero effect after mainDuration

	Real m_radius;											///< Radius of the effect
	Real m_eventHorizonRadius;					///< Inside this radius victims are consumed; 0 disables
	Real m_liftHeight;									///< How far above the ground the rift center sits

	Real m_pullAcceleration;						///< Inward suction, dist per frame squared
	Real m_swirlFactor;									///< Bends the pull sideways; 0 falls straight in, ~1 orbits.
																			///< Deliberately a direction bias and not a separate thrust: a
																			///< tangential force would pump energy in and spiral victims out.
	Real m_pullDamping;									///< Fraction of velocity bled off per frame. Without this the
																			///< inward speed grows without bound and victims slingshot clear.
	Real m_liftAcceleration;						///< Vertical acceleration toward the rift plane
	Real m_maxPullSpeed;								///< Speed cap for held victims, dist per frame; 0 is uncapped
	Real m_initialLiftVelocity;					///< Upward kick on capture, dist per frame
	Bool m_cancelGravity;								///< Counteract world gravity while a victim is held
	Real m_spinRate;										///< Tumble budget, rads per frame
	Real m_hugeVehiclePullScalar;				///< Suction multiplier for KINDOF_HUGE_VEHICLE

	Real m_damagePerSecond;							///< Damage dealt per second of exposure at full strength
	UnsignedInt m_damageInterval;				///< Frames between damage ticks
	DamageType m_damageType;						///< Damage type used for the continuous damage
	DeathType m_consumeDeathType;				///< Death type used when a victim crosses the event horizon

	Real m_finalPushForce;							///< Shockwave amount of the collapse blast
	Real m_finalPushSpeed;							///< Outward velocity given to held victims on collapse
	DeathType m_finalPushDeathType;			///< Death type of the collapse blast

	KindOfMaskType m_pullKindOf;				///< If any bit is set, only these kinds are pulled
	KindOfMaskType m_noPullKindOf;			///< These kinds are never pulled

	const FXList* m_FXmain;							///< FX played at the start of the rampup
	const FXList* m_FXcharge;						///< FX played at activation
	const FXList* m_FXfinal;						///< FX played on collapse
	const FXList* m_FXconsume;					///< FX played on each victim crossing the event horizon
	const ObjectCreationList* m_OCLconsume;	///< OCL fired on each victim crossing the event horizon
};

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class RiftSlowDeathBehavior : public SlowDeathBehavior
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(RiftSlowDeathBehavior, "RiftSlowDeathBehavior")
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA(RiftSlowDeathBehavior, RiftSlowDeathBehaviorModuleData)

public:

	RiftSlowDeathBehavior(Thing* thing, const ModuleData* moduleData);
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update(void);				 ///< the update call

protected:

	void onActivated(UnsignedInt currFrame);						///< lock in the rift position and phase timings
	RiftPhaseType computePhase(UnsignedInt currFrame, Real* strength) const;
	void onPhaseEnter(RiftPhaseType phase);							///< one-shot effects for a phase transition

	void doRiftTick(Real strength, UnsignedInt currFrame);	///< one pass over everything in range
	void doCollapse(void);															///< release the held victims and blow them outwards

	Bool isPullable(const Object* victim) const;				///< can this object be dragged around at all
	Bool usesLocomotorCapture(const Object* victim) const;	///< flyer whose locomotor keeps running while disabled
	RiftVictim* findVictim(ObjectID id);								///< NULL if we are not holding it
	void captureVictim(Object* victim);									///< one-shot setup when a victim is first grabbed
	void steerVictim(RiftVictim& victim, Object* obj, const Coord3D& toRift, Real dist, Real strength);
	void releaseVictim(const RiftVictim& victim, Object* obj);	///< undo what capture cannot self-heal
	void releaseAllVictims(void);

	UnsignedInt m_activationFrame;			///< frame we were activated on
	UnsignedInt m_chargeEndFrame;				///< absolute frame the rampup begins
	UnsignedInt m_rampEndFrame;					///< absolute frame the main phase begins
	UnsignedInt m_mainEndFrame;					///< absolute frame the fade out begins
	UnsignedInt m_fadeEndFrame;					///< absolute frame the rift collapses
	UnsignedInt m_nextDamageFrame;			///< absolute frame of the next damage tick

	Coord3D m_riftPos;									///< Position where the rift sits, locked in at activation
	Int m_phase;												///< current RiftPhaseType

	std::vector<RiftVictim> m_victims;	///< objects we currently hold
};

#endif  // end __RIFTSlowDeathBehavior_H_
