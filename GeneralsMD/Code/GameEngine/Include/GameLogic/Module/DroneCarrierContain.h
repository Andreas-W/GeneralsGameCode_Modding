// FILE: DroneCarrierContain.h ////////////////////////////////////////////////////////////////////////
// Desc:   expanded transport contain to work with drone carrier
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#ifndef __DRONE_CARRIER_CONTAIN_H_
#define __DRONE_CARRIER_CONTAIN_H_

// USER INCLUDES //////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/OpenContain.h"
#include "GameLogic/Module/TransportContain.h"

//-------------------------------------------------------------------------------------------------
class DroneCarrierContain: public TransportContain
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(DroneCarrierContain, "DroneCarrierContain")
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA(DroneCarrierContain, TransportContainModuleData)

public:

	DroneCarrierContain(Thing* thing, const ModuleData* moduleData);
	// virtual destructor prototype provided by memory pool declaration

	virtual Bool isValidContainerFor(const Object* obj, Bool checkCapacity) const;
};

#endif // __TransportContain_H_
