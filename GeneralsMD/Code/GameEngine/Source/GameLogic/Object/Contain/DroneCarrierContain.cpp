// FILE: DroneCarrierContain.cpp //////////////////////////////////////////////////////////////////////
// Desc:   Contain module for drone carrier
///////////////////////////////////////////////////////////////////////////////////////////////////

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "Common/Player.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/BodyModule.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/StealthUpdate.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameLogic/Module/DroneCarrierContain.h"
#include "GameLogic/Object.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponSetType.h"

// PUBLIC /////////////////////////////////////////////////////////////////////////////////////////
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DroneCarrierContain::DroneCarrierContain(Thing* thing, const ModuleData* moduleData) :
	TransportContain(thing, moduleData)
{
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DroneCarrierContain::~DroneCarrierContain(void)
{
}

Bool DroneCarrierContain::isValidContainerFor(const Object* rider, Bool checkCapacity) const
{
	// sanity
	if (!rider)
		return false;

	// no... actually, only OUR OWN units can be transported.
	if (rider->getControllingPlayer() != getObject()->getControllingPlayer())
		return false;

	Int transportSlotCount = rider->getTransportSlotCount();

	// if 0, this object isn't transportable.
	if (transportSlotCount == 0)
		return false;

	if (checkCapacity)
	{
		Int containMax = getContainMax();
		Int containCount = getContainCount();

		if (!(m_extraSlotsInUse + containCount + transportSlotCount <= containMax)) {
			return false;
		}
	}

	// Check if this is actually a slaved unit
	for (BehaviorModule** update = rider->getBehaviorModules(); *update; ++update)
	{
		SlavedUpdateInterface* sdu = (*update)->getSlavedUpdateInterface();
		if (sdu != nullptr)
		{
			return sdu->getSlaverID() == getObject()->getID();
		}
	}

	return false;
}


// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void DroneCarrierContain::crc(Xfer* xfer)
{

	// extend base class
	TransportContain::crc(xfer);

}  // end crc

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
	// ------------------------------------------------------------------------------------------------
void DroneCarrierContain::xfer(Xfer* xfer)
{
	// extend base class
	TransportContain::xfer(xfer);

}  // end xfer

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void DroneCarrierContain::loadPostProcess(void)
{

	// extend base class
	TransportContain::loadPostProcess();

}  // end loadPostProcess

