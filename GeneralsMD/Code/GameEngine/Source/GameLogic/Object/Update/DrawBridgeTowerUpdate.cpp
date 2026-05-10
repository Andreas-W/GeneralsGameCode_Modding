
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

	m_openAnimationFrames = 0U;
	m_closeAnimationFrames = 0U;
	m_transitionIdleFrames = 0U;

}

//-------------------------------------------------------------------------------------------------
/*static*/ void DrawBridgeTowerUpdateModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	ModuleData::buildFieldParse(p);

	static const FieldParse dataFieldParse[] =
	{
		{ "SpecialPowerTemplate",									INI::parseSpecialPowerTemplate,	nullptr, offsetof(DrawBridgeTowerUpdateModuleData, m_specialPowerTemplate) },
		{ "OpenAnimationFrames",					INI::parseDurationUnsignedInt,  nullptr, offsetof(DrawBridgeTowerUpdateModuleData, m_openAnimationFrames) },
		{ "CloseAnimationFrames",					INI::parseDurationUnsignedInt,  nullptr, offsetof(DrawBridgeTowerUpdateModuleData, m_closeAnimationFrames) },
		{ "TransitionIdleTime",						INI::parseDurationUnsignedInt,  nullptr, offsetof(DrawBridgeTowerUpdateModuleData, m_transitionIdleFrames) },

		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

//-------------------------------------------------------------------------------------------------
DrawBridgeTowerUpdate::DrawBridgeTowerUpdate(Thing* thing, const ModuleData* moduleData) :
	SpecialPowerUpdateModule(thing, moduleData)
{

	m_currentState = BRIDGESTATE_CLOSE;
	m_desiredState = BRIDGESTATE_CLOSE;

	m_transitionStatus = TRANSITIONSTATUS_CLOSED;
	m_nextReadyFrame = 0;

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

void DrawBridgeTowerUpdate::broadcastTowerState() {
	// find the bridge to broadcast to other towers

	Object* obj = getObject();

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
  	return;
  }
  
  Object* bridge = TheGameLogic->findObjectByID(bridgeTowerInterface->getBridgeID());
  
  if (bridge == nullptr) {
  	DEBUG_CRASH(("%s: DrawBridgeTowerUpdate requires a BridgeTowerInterface with linked bridge!", obj->getTemplate()->getName().str()));
  	return;
  }

	// get the bridge behavior module for our bridge
	BehaviorModule** bmi2;
	BridgeBehaviorInterface* bridgeInterface = nullptr;
	for (bmi2 = bridge->getBehaviorModules(); *bmi2; ++bmi2)
	{
		bridgeInterface = (*bmi2)->getBridgeBehaviorInterface();
		if (bridgeInterface)
			break;
	}

	if (bridgeInterface != nullptr) {
		// forward to bridge
		bridgeInterface->towerDrawBridgeUpdate(getObject(), getTowerInfo());
	}
  
	
}

//-------------------------------------------------------------------------------------------------
Bool DrawBridgeTowerUpdate::initiateIntentToDoSpecialPower(const SpecialPowerTemplate* specialPowerTemplate, const Object* targetObj, const Coord3D* targetPos, const Waypoint* way, UnsignedInt commandOptions)
{
	if (m_specialPowerModule->getSpecialPowerTemplate() != specialPowerTemplate)
	{
		DEBUG_LOG(("DRAWBRIDGE: Special Power Temmplate is not connected!."));
		//Check to make sure our modules are connected.
		return FALSE;
	}
	BridgeState oldstate = m_desiredState;

	//Set the desired status based on the command button option!
	if (BitIsSet(commandOptions, OPTION_ONE))
	{
		m_desiredState = BRIDGESTATE_CLOSE;

	}
	else if (BitIsSet(commandOptions, OPTION_TWO))
	{
		m_desiredState = BRIDGESTATE_OPEN;

	}
	else
	{
		DEBUG_LOG(("DRAWBRIDGE: Selected an unsupported state for draw bridge."));
		return FALSE;
	}

	if (m_desiredState != oldstate) {
		//broadcast
	}

	return TRUE;
}

Bool DrawBridgeTowerUpdate::isPowerCurrentlyInUse(const CommandButton* command) const
{
	//@todo -- perhaps we may need this one day...
	return false;
}

//-------------------------------------------------------------------------------------------------
CommandOption DrawBridgeTowerUpdate::getCommandOption() const
{
	switch (m_desiredState)
	{
	case BRIDGESTATE_CLOSE:
		return OPTION_ONE;
	case BRIDGESTATE_OPEN:
		return OPTION_TWO;
	}
	return (CommandOption)0;
}

DrawBridgeTowerInfo DrawBridgeTowerUpdate::getTowerInfo() const
{
	auto ret = DrawBridgeTowerInfo();
	ret.currentState = m_currentState;
	ret.desiredState = m_desiredState;
	ret.transitionStatus = m_transitionStatus;
	ret.nextReadyFrame = m_nextReadyFrame;
	return ret;
}

void DrawBridgeTowerUpdate::updateFromTowerInfo(const DrawBridgeTowerInfo& info)
{
	m_currentState = info.currentState;
	m_desiredState = info.desiredState;
	m_transitionStatus = info.transitionStatus;
	m_nextReadyFrame = info.nextReadyFrame;
}

//-------------------------------------------------------------------------------------------------
/** The update callback. */
//-------------------------------------------------------------------------------------------------
UpdateSleepTime DrawBridgeTowerUpdate::update()
{

	UnsignedInt now = TheGameLogic->getFrame();

	if (m_nextReadyFrame <= now)
	{
		const auto* data = getDrawBridgeTowerUpdateModuleData();

		switch (m_transitionStatus)
		{
		case TRANSITIONSTATUS_CLOSED:

			//bridge is closed

			if (m_desiredState == BRIDGESTATE_OPEN) {
				setTransitionStatus(TRANSITIONSTATUS_OPENING);
				m_nextReadyFrame = now + data->m_openAnimationFrames;
			}

			break;
		case TRANSITIONSTATUS_CLOSING:

			setTransitionStatus(TRANSITIONSTATUS_CLOSED);
			m_nextReadyFrame = now + 10; //todo
			m_currentState = BRIDGESTATE_CLOSE;

			break;
		case TRANSITIONSTATUS_OPENED:

			if (m_desiredState == BRIDGESTATE_CLOSE) {
				setTransitionStatus(TRANSITIONSTATUS_CLOSING);
				m_nextReadyFrame = now + data->m_closeAnimationFrames;
			}

			break;
		case TRANSITIONSTATUS_OPENING:

			setTransitionStatus(TRANSITIONSTATUS_OPENED);
			m_nextReadyFrame = now + 10; //todo
			m_currentState = BRIDGESTATE_OPEN;

			break;
		}
	}

	return UPDATE_SLEEP_NONE;
}

//-------------------------------------------------------------------------------------------------
void DrawBridgeTowerUpdate::setTransitionStatus(BridgeTransitionStatus newStatus)
{

	if (m_transitionStatus == newStatus)
	{
		return;
	}

	//BridgeTransitionStatus oldStatus = m_transitionStatus;

	m_transitionStatus = newStatus;

	//TODO
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

	// current state
	xfer->xferUser(&m_currentState, sizeof(BridgeState));

	// desired state
	xfer->xferUser(&m_desiredState, sizeof(BridgeState));

	// status
	xfer->xferUser(&m_transitionStatus, sizeof(BridgeTransitionStatus));

	// next ready frame
	xfer->xferUnsignedInt(&m_nextReadyFrame);

}

//------------------------------------------------------------------------------------------------
void DrawBridgeTowerUpdate::loadPostProcess(void)
{
	
	// extend base class
	UpdateModule::loadPostProcess();

}
