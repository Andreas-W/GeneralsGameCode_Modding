
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
	m_openingDuration = 0U;
	m_closingDuration = 0U;
}

//-------------------------------------------------------------------------------------------------
/*static*/ void DrawBridgeUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "OpeningDuration", INI::parseDurationUnsignedInt, nullptr, offsetof(DrawBridgeUpdateModuleData, m_openingDuration)},
		{ "ClosingDuration", INI::parseDurationUnsignedInt, nullptr, offsetof(DrawBridgeUpdateModuleData, m_closingDuration)},
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
DrawBridgeUpdate::DrawBridgeUpdate(Thing* thing, const ModuleData* moduleData) :
	UpdateModule(thing, moduleData)
{
	m_bridgeOpened = false;
	m_nextReadyFrame = 0U;
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
}

//-------------------------------------------------------------------------------------------------
CommandOption DrawBridgeUpdate::getCommandOption() const
{
	return m_bridgeOpened ? OPTION_TWO : OPTION_ONE;
}

bool DrawBridgeUpdate::setDrawBridgeState(bool opened, const Object* fromTower)
{
	UnsignedInt now = TheGameLogic->getFrame();

	if (m_nextReadyFrame <= now) {

		const DrawBridgeUpdateModuleData* data = getDrawBridgeUpdateModuleData();

		m_nextReadyFrame = now + (m_bridgeOpened ? data->m_closingDuration : data->m_openingDuration);

		if (m_bridgeOpened != opened) {
			m_bridgeOpened = opened;

			Object* obj = getObject();

			// bridge state was changed, we need to update
			Bridge* bridge = TheTerrainLogic->findBridgeAt(obj->getPosition());
			if (bridge != nullptr) {
				TheAI->pathfinder()->changeBridgeState(bridge->getLayer(), !m_bridgeOpened);
				bridge->setDrawBridgeStage(m_bridgeOpened);
			}

			if (m_bridgeOpened) {
				obj->clearAndSetModelConditionState(MODELCONDITION_DOOR_1_CLOSING, MODELCONDITION_DOOR_1_OPENING);
				GeometryInfo openBridgeGeom(GeometryType::GEOMETRY_BOX, true, 0.0f, 0.0f, 0.0f);
				obj->setGeometryInfo(openBridgeGeom);
			}
			else {
				obj->clearAndSetModelConditionState(MODELCONDITION_DOOR_1_OPENING, MODELCONDITION_DOOR_1_CLOSING);
				obj->setGeometryInfo(obj->getTemplate()->getTemplateGeometryInfo());
			}
		}
		return true;
	}
	return false;
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

	xfer->xferUnsignedInt(&m_nextReadyFrame);
}

//------------------------------------------------------------------------------------------------
void DrawBridgeUpdate::loadPostProcess(void)
{
	
	// extend base class
	UpdateModule::loadPostProcess();

}
