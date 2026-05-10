// FILE: DrawBridgeTowerUpdate.h //////////////////////////////////////////////////////////////////////////
// Desc:   Update module to handle draw bridge toggling
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/KindOf.h"
#include "GameLogic/Module/SpecialPowerUpdateModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class SpecialPowerModule;
class ParticleSystem;
class FXList;
class AudioEventRTS;
enum  CommandOption CPP_11(: Int);

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class DrawBridgeTowerUpdateModuleData : public ModuleData
{
public:
	SpecialPowerTemplate *m_specialPowerTemplate;

  UnsignedInt m_openAnimationFrames;
  UnsignedInt m_closeAnimationFrames;
	UnsignedInt m_transitionIdleFrames;

	DrawBridgeTowerUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);

private:

};

enum BridgeTransitionStatus CPP_11(: Int)
{
	  TRANSITIONSTATUS_CLOSED,
		TRANSITIONSTATUS_OPENING,
		TRANSITIONSTATUS_OPENED,
		TRANSITIONSTATUS_CLOSING,

		TRANSITIONSTATUS_COUNT
};

enum BridgeState CPP_11(: Int)
{
	  BRIDGESTATE_CLOSE,
		BRIDGESTATE_OPEN,
							
		BRIDGESTATE_COUNT
};

struct DrawBridgeTowerInfo {
	BridgeState currentState;
	BridgeState desiredState;
	BridgeTransitionStatus transitionStatus;

	UnsignedInt nextReadyFrame;
};

//-------------------------------------------------------------------------------------------------
/** The default	update module */
//-------------------------------------------------------------------------------------------------
class DrawBridgeTowerUpdate : public SpecialPowerUpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( DrawBridgeTowerUpdate, "DrawBridgeTowerUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( DrawBridgeTowerUpdate, DrawBridgeTowerUpdateModuleData );

public:

	DrawBridgeTowerUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	// SpecialPowerUpdateInterface
	virtual Bool initiateIntentToDoSpecialPower(const SpecialPowerTemplate *specialPowerTemplate, const Object *targetObj, const Coord3D *targetPos, const Waypoint *way, UnsignedInt commandOptions );
	virtual Bool isSpecialAbility() const { return false; }
	virtual Bool isSpecialPower() const { return true; }
	virtual Bool isActive() const {return false;}
	virtual SpecialPowerUpdateInterface* getSpecialPowerUpdateInterface() { return this; }
	virtual Bool doesSpecialPowerHaveOverridableDestinationActive() const { return false; } //Is it active now?
	virtual Bool doesSpecialPowerHaveOverridableDestination() const { return false; }	//Does it have it, even if it's not active?
	virtual void setSpecialPowerOverridableDestination( const Coord3D *loc ) {}
	virtual Bool isPowerCurrentlyInUse( const CommandButton *command = nullptr ) const;

	virtual void onObjectCreated();
	virtual void onDelete();
	virtual UpdateSleepTime update();

	virtual CommandOption getCommandOption() const;

	DrawBridgeTowerInfo getTowerInfo() const;
	void updateFromTowerInfo(const DrawBridgeTowerInfo& info);

protected:
	void broadcastTowerState();
	void setTransitionStatus(BridgeTransitionStatus status );

	BridgeState m_currentState;	//The current state of the bridge
	BridgeState m_desiredState; //The user desired state of the bridge
	BridgeTransitionStatus m_transitionStatus;

	UnsignedInt m_nextReadyFrame;
	SpecialPowerModuleInterface *m_specialPowerModule;
};
