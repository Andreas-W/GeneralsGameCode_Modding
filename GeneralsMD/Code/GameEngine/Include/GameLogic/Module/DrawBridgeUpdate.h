// FILE: DrawBridgeUpdate.h //////////////////////////////////////////////////////////////////////////
// Desc:   Update module to handle draw bridge toggling
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/KindOf.h"
#include "GameLogic/Module/UpdateModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class SpecialPowerModule;
class ParticleSystem;
class FXList;
class AudioEventRTS;
enum  CommandOption CPP_11(: Int);

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class DrawBridgeUpdateModuleData : public ModuleData
{
public:
	//SpecialPowerTemplate *m_specialPowerTemplate;
	UnsignedInt m_openingDuration;
	UnsignedInt m_closingDuration;
	Real m_openingPushForce;
	UnsignedInt m_closingDamageTime;

	DrawBridgeUpdateModuleData();
	static void buildFieldParse(MultiIniFieldParse& p);

private:

};

//-------------------------------------------------------------------------------------------------
/** The default	update module */
//-------------------------------------------------------------------------------------------------
class DrawBridgeUpdate : public UpdateModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( DrawBridgeUpdate, "DrawBridgeUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( DrawBridgeUpdate, DrawBridgeUpdateModuleData );

public:

	DrawBridgeUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void onObjectCreated();
	virtual void onDelete();
	virtual UpdateSleepTime update();

	virtual CommandOption getCommandOption() const;

	bool setDrawBridgeState(bool opened, const Object* fromTower);

protected:
	void pushObjectsOnOpeningDrawbridge( void );
	void destroyObjectsUnderClosingDrawbridge (void );

	bool m_bridgeOpened;

	UnsignedInt m_nextReadyFrame;

	UnsignedInt m_openingFrame;            ///< frame bridge started to open
	UnsignedInt m_closingDamageFrame;      ///< frame damage will be applied when closing
};
