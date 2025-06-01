#pragma once

#ifndef __RIFTSlowDeathBehavior_H_
#define __RIFTSlowDeathBehavior_H_

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/SlowDeathBehavior.h"

class FXList;

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
class RiftSlowDeathBehaviorModuleData : public SlowDeathBehaviorModuleData
{

public:

	RiftSlowDeathBehaviorModuleData(void);

	static void buildFieldParse(MultiIniFieldParse& p);

	UnsignedInt m_initialDelay; ///< Frames before effect starts
	UnsignedInt m_rampupTime; ///< Frames till full effect
	UnsignedInt m_mainDuration; ///< Frames with full effect after rampupTime
	UnsignedInt m_fadeOutTime; ///< Frames till zero effect after mainDuration
	Real m_radius; ///< Radius of the effect
	Real m_damage; ///< damage applied per tick
	const FXList* m_FXmain; ///< FX played at start of main duration
	const FXList* m_FXcharge; ///< FX played at start
	const FXList* m_FXfinal; ///< FX played at final push
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

	void applyPull(float magnitude_multiplier) const;
	void doFinalPush() const;

	UnsignedInt m_activationFrame;									///< frame we were activated on
	Coord3D m_riftPos; ///< Position where the rift starts, center of mass
	bool m_finalPush; ///< final push happened
	bool m_playedMainFX;
};

#endif  // end __RIFTSlowDeathBehavior_H_