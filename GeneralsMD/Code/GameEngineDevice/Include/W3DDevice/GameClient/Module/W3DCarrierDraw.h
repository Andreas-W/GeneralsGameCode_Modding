// FILE: W3DModelDraw.h /////////////////////////////////////////////////////////////////////////
// Desc:   Draw with stationed drones for drone carrier
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "Common/ModelState.h"
#include "Common/DrawModule.h"
#include "W3DDevice/GameClient/Module/W3DModelDraw.h"

//-------------------------------------------------------------------------------------------------
class W3DCarrierDrawModuleData : public W3DModelDrawModuleData
{
public:
	AsciiString				m_dockingbone;

	W3DCarrierDrawModuleData();
	~W3DCarrierDrawModuleData();

	static void buildFieldParse(MultiIniFieldParse& p);
};

//-------------------------------------------------------------------------------------------------
class W3DCarrierDraw : public W3DModelDraw
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(W3DCarrierDraw, "W3DCarrierDraw")
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA(W3DCarrierDraw, W3DCarrierDrawModuleData)

public:

	W3DCarrierDraw(Thing* thing, const ModuleData* moduleData);
	// virtual destructor prototype provided by memory pool declaration

	virtual void setHidden(Bool h);
	virtual void doDrawModule(const Matrix3D* transformMtx);

protected:

};
