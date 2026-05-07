// FILE: BridgeTowerBody.cpp ////////////////////////////////////////////////////////////////////////
// Desc:	 Takes damage propagated from bridge only.  Can die from Unresistable though
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine
#include "Common/Xfer.h"

#include "GameLogic/Module/BridgeTowerBody.h"
#include "GameLogic/Module/BridgeTowerBehavior.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
BridgeTowerBody::BridgeTowerBody( Thing *thing, const ModuleData* moduleData )
						 : ActiveBody( thing, moduleData )
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
BridgeTowerBody::~BridgeTowerBody( void )
{

}

// ------------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------------
void BridgeTowerBody::attemptDamage( DamageInfo *damageInfo )
{
	Object* source = TheGameLogic->findObjectByID(damageInfo->in.m_sourceID);

	Object* self = getObject();

	// get the bridge tower behavior module
	BehaviorModule** bmi;
	BridgeTowerBehaviorInterface* bridgeTowerInterface = nullptr;
	for (bmi = self->getBehaviorModules(); *bmi; ++bmi)
	{
		bridgeTowerInterface = (*bmi)->getBridgeTowerBehaviorInterface();
		if (bridgeTowerInterface)
			break;
	}

	bool bridgeAlive{ false };
	if (bridgeTowerInterface != nullptr) {
		ObjectID bridgeID = bridgeTowerInterface->getBridgeID();
		Object* bridge = TheGameLogic->findObjectByID(bridgeID);
		if (bridge != nullptr) {
			auto * bbody = bridge->getBodyModule();
			if (bbody != nullptr) {
				bridgeAlive = bbody->getHealth() > 0.0f;
			}
		}
	}
	bool fromBridge = source != nullptr && source->isKindOf(KINDOF_BRIDGE);

	//Only allow damage from bridges if bridge is alive
	if ((bridgeAlive && fromBridge) || !bridgeAlive) {

		// Bind to one hitpoint remaining afterwards, unless it is Unresistable damage
		if (damageInfo->in.m_damageType != DAMAGE_UNRESISTABLE)
			damageInfo->in.m_amount = min(damageInfo->in.m_amount, getHealth() - 1);

			ActiveBody::attemptDamage(damageInfo);
	}
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void BridgeTowerBody::crc( Xfer *xfer )
{

	// extend base class
	ActiveBody::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void BridgeTowerBody::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	ActiveBody::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void BridgeTowerBody::loadPostProcess( void )
{

	// extend base class
	ActiveBody::loadPostProcess();

}
