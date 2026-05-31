
// FILE: DrawBridgeTowerUpdate.cpp //////////////////////////////////////////////////////////////////////////
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

#include "GameLogic/GameLogic.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectIter.h"
#include "GameLogic/TerrainLogic.h"
#include "GameLogic/Module/BridgeBehavior.h"
#include "GameLogic/Module/BridgeTowerBehavior.h"
#include "GameLogic/Module/SpecialPowerModule.h"
#include "GameLogic/Module/DrawBridgeTowerUpdate.h"
#include "GameLogic/Module/PhysicsUpdate.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/AIUpdate.h"


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DrawBridgeTowerUpdateModuleData::DrawBridgeTowerUpdateModuleData()
{
	m_specialPowerTemplate = nullptr;

}

//-------------------------------------------------------------------------------------------------
/*static*/ void DrawBridgeTowerUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "SpecialPowerTemplate",									INI::parseSpecialPowerTemplate,	nullptr, offsetof(DrawBridgeTowerUpdateModuleData, m_specialPowerTemplate) },

		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
DrawBridgeTowerUpdate::DrawBridgeTowerUpdate(Thing* thing, const ModuleData* moduleData) :
	SpecialPowerUpdateModule(thing, moduleData)
{
	m_specialPowerModule = nullptr;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
DrawBridgeTowerUpdate::~DrawBridgeTowerUpdate(void)
{
}

// ------------------------------------------------------------------------------------------------
/** On delete */
// ------------------------------------------------------------------------------------------------
void DrawBridgeTowerUpdate::onDelete()
{
	// extend base class
	UpdateModule::onDelete();

}

//-------------------------------------------------------------------------------------------------
// Validate that we have the necessary data from the ini file.
//-------------------------------------------------------------------------------------------------
void DrawBridgeTowerUpdate::onObjectCreated()
{
	const DrawBridgeTowerUpdateModuleData* data = getDrawBridgeTowerUpdateModuleData();
	Object* obj = getObject();

	if (!data->m_specialPowerTemplate)
	{
		DEBUG_CRASH(("%s object's BattlePlanUpdate lacks access to the SpecialPowerTemplate. Needs to be specified in ini.", obj->getTemplate()->getName().str()));
		return;
	}

	m_specialPowerModule = obj->getSpecialPowerModule(data->m_specialPowerTemplate);
}

//-------------------------------------------------------------------------------------------------
Bool DrawBridgeTowerUpdate::initiateIntentToDoSpecialPower(const SpecialPowerTemplate* specialPowerTemplate, const Object* targetObj, const Coord3D* targetPos, const Waypoint* way, UnsignedInt commandOptions)
{
	if (m_specialPowerModule->getSpecialPowerTemplate() != specialPowerTemplate)
	{
		DEBUG_LOG(("DRAWBRIDGE: Special Power Temmplate is not connected!."));
		return false;
	}

	bool drawBridgeOpened{ false };

	//Set the desired status based on the command button option!
	if (BitIsSet(commandOptions, OPTION_ONE))
	{
		drawBridgeOpened = false;

	}
	else if (BitIsSet(commandOptions, OPTION_TWO))
	{
		drawBridgeOpened = true;

	}
	else
	{
		DEBUG_LOG(("DRAWBRIDGE: Selected an unsupported state for draw bridge."));
		return false;
	}

	auto* update = getDrawBridgeUpdate();
	if (update != nullptr) {

		return update->setDrawBridgeState(drawBridgeOpened, getObject());
	}
	return false;
}

Bool DrawBridgeTowerUpdate::isPowerCurrentlyInUse(const CommandButton* command) const
{
	//@todo -- perhaps we may need this one day...
	return false;
}

//-------------------------------------------------------------------------------------------------
CommandOption DrawBridgeTowerUpdate::getCommandOption() const
{
	auto* update = getDrawBridgeUpdate();
	if (update != nullptr) {
		return update->getCommandOption();
	}
	else {
		return (CommandOption)0;
	}
}

DrawBridgeUpdate* DrawBridgeTowerUpdate::getDrawBridgeUpdate() const
{
	Object* bridge = getBridge();
	if (bridge == nullptr) {
		DEBUG_CRASH(("%s: DrawBridgeTowerUpdate requires a BridgeTowerInterface with linked bridge!", obj->getTemplate()->getName().str()));
		return nullptr;
	}

	static NameKeyType key_drawBridgeUpdate = NAMEKEY("DrawBridgeUpdate");
	DrawBridgeUpdate* update = (DrawBridgeUpdate*)bridge->findUpdateModule(key_drawBridgeUpdate);

	return update;
}

Object* DrawBridgeTowerUpdate::getBridge() const
{
	BehaviorModule** bmi;
	BridgeTowerBehaviorInterface* bridgeTowerInterface = nullptr;
	for (bmi = getObject()->getBehaviorModules(); *bmi; ++bmi)
	{
		bridgeTowerInterface = (*bmi)->getBridgeTowerBehaviorInterface();
		if (bridgeTowerInterface)
			break;
	}

	if (bridgeTowerInterface == nullptr) {
		DEBUG_CRASH(("%s: DrawBridgeTowerUpdate requires a BridgeTowerInterface!", obj->getTemplate()->getName().str()));
		return nullptr;
	}

	Object* bridge = TheGameLogic->findObjectByID(bridgeTowerInterface->getBridgeID());

	return bridge;
}


//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime DrawBridgeTowerUpdate::update()
{

	return UPDATE_SLEEP_NONE;
}

//------------------------------------------------------------------------------------------------
void DrawBridgeTowerUpdate::crc(Xfer* xfer)
{

	// extend base class
	UpdateModule::crc(xfer);

}

//------------------------------------------------------------------------------------------------
// Xfer method
//	Version Info:
//	1: Initial version
//------------------------------------------------------------------------------------------------
void DrawBridgeTowerUpdate::xfer(Xfer* xfer)
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion(&version, currentVersion);

	// extend base class
	UpdateModule::xfer(xfer);
}

//------------------------------------------------------------------------------------------------
void DrawBridgeTowerUpdate::loadPostProcess(void)
{
	
	// extend base class
	UpdateModule::loadPostProcess();

}
