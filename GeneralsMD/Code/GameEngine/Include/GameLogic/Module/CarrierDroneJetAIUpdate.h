// CarrierDroneJetAIUpdate.h //////////
#pragma once

#include "Common/STLTypedefs.h"
#include "Common/GameMemory.h"
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/Module/AIUpdate.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------------
//class CarrierDroneJetAIUpdateModuleData : public AIUpdateModuleData
//{
//public:
//
//
//	CarrierDroneJetAIUpdateModuleData();
//	static void buildFieldParse(MultiIniFieldParse& p);
//};

//-------------------------------------------------------------------------------------------------
class CarrierDroneJetAIUpdate : public AIUpdateInterface
{

		MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(CarrierDroneJetAIUpdate, "CarrierDroneJetAIUpdate")
		MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA(CarrierDroneJetAIUpdate, AIUpdateModuleData)

		//virtual UpdateSleepTime update();

public:

	CarrierDroneJetAIUpdate(Thing* thing, const ModuleData* moduleData);
	// virtual destructor prototype provided by memory pool declaration
};
