
// FILE: DrawBridgeUpdate.cpp //////////////////////////////////////////////////////////////////////////
// Desc:   Update module to handle state change of draw bridges
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#define DEFINE_MAXHEALTHCHANGETYPE_NAMES						// for TheMaxHealthChangeTypeNames[]

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/BitFlagsIO.h"
#include "Common/Radar.h"
#include "Common/PlayerList.h"
#include "Common/ThingTemplate.h"
#include "Common/ThingFactory.h"
#include "Common/Player.h"
#include "Common/Xfer.h"

#include "GameClient/GameClient.h"
#include "GameClient/Drawable.h"
#include "GameClient/GameText.h"
#include "GameClient/ParticleSys.h"
#include "GameClient/FXList.h"
#include "GameClient/ControlBar.h"

#include "GameLogic/AI.h"
#include "GameLogic/AIPathfind.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/BridgeBehavior.h"
#include "GameLogic/Module/BridgeTowerBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/DrawBridgeUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DrawBridgeUpdateModuleData::DrawBridgeUpdateModuleData()
{
	m_specialPowerTemplate = nullptr;

}

//-------------------------------------------------------------------------------------------------
/*static*/ void DrawBridgeUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "SpecialPowerTemplate",									INI::parseSpecialPowerTemplate,	nullptr, offsetof(DrawBridgeUpdateModuleData, m_specialPowerTemplate) },
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
DrawBridgeUpdate::DrawBridgeUpdate(Thing* thing, const ModuleData* moduleData) :
	SpecialPowerUpdateModule(thing, moduleData)
{
	m_bridgeOpened = false;

	m_specialPowerModule = nullptr;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DrawBridgeUpdate::~DrawBridgeUpdate(void)
{
}

// ------------------------------------------------------------------------------------------------
/** On delete */
// ------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::onDelete()
{
	// extend base class
	UpdateModule::onDelete();

}

//-------------------------------------------------------------------------------------------------
// Validate that we have the necessary data from the ini file.
//-------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::onObjectCreated()
{
	const DrawBridgeUpdateModuleData* data = getDrawBridgeUpdateModuleData();
	Object* obj = getObject();

	if (!data->m_specialPowerTemplate)
	{
		DEBUG_CRASH(("%s object's DrawBridgeUpdate lacks access to the SpecialPowerTemplate. Needs to be specified in ini.", obj->getTemplate()->getName().str()));
		return;
	}

	m_specialPowerModule = obj->getSpecialPowerModule(data->m_specialPowerTemplate);
}

//-------------------------------------------------------------------------------------------------
Bool DrawBridgeUpdate::initiateIntentToDoSpecialPower(const SpecialPowerTemplate* specialPowerTemplate, const Object* targetObj, const Coord3D* targetPos, const Waypoint* way, UnsignedInt commandOptions)
{
	
	return TRUE;
}

Bool DrawBridgeUpdate::isPowerCurrentlyInUse(const CommandButton* command) const
{
	//@todo -- perhaps we may need this one day...
	return false;
}

//-------------------------------------------------------------------------------------------------
CommandOption DrawBridgeUpdate::getCommandOption() const
{
	return m_bridgeOpened ? OPTION_TWO : OPTION_ONE;
}

void DrawBridgeUpdate::setDrawBridgeState(bool opened, const Object* fromTower)
{
	if (m_bridgeOpened != opened) {
		m_bridgeOpened = opened;

		// bridge state was changed, we need to update
		Bridge* bridge = TheTerrainLogic->findBridgeAt(getObject()->getPosition());
		if (bridge != nullptr)
			TheAI->pathfinder()->changeBridgeState(bridge->getLayer(), !m_bridgeOpened);

		if (m_bridgeOpened) {
			getObject()->clearAndSetModelConditionState(MODELCONDITION_DOOR_1_CLOSING, MODELCONDITION_DOOR_1_OPENING);
		}
		else {
			getObject()->clearAndSetModelConditionState(MODELCONDITION_DOOR_1_OPENING, MODELCONDITION_DOOR_1_CLOSING);
		}
	}
}

//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime DrawBridgeUpdate::update()
{

	return UPDATE_SLEEP_FOREVER; //UPDATE_SLEEP_NONE;
}


//------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::crc(Xfer* xfer)
{

	// extend base class
	UpdateModule::crc(xfer);

}

//------------------------------------------------------------------------------------------------
// Xfer method
//	Version Info:
//	1: Initial version
//------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::xfer(Xfer* xfer)
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	// extend base class
	UpdateModule::xfer(xfer);

	xfer->xferBool(&m_bridgeOpened);

}

//------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::loadPostProcess(void)
{
	
	// extend base class
	UpdateModule::loadPostProcess();

}
