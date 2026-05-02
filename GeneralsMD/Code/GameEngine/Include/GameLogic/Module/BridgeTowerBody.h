
// FILE: BridgeTowerBody.h ////////////////////////////////////////////////////////////////////////
// Desc:	 Takes damage propagated from bridge only.  Can die from Unresistable though
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/ActiveBody.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Object;

//-------------------------------------------------------------------------------------------------
/** Structure body module */
//-------------------------------------------------------------------------------------------------
class BridgeTowerBody : public ActiveBody
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(BridgeTowerBody, "BridgeTowerBody" )
	MAKE_STANDARD_MODULE_MACRO(BridgeTowerBody)

public:

	BridgeTowerBody( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual void attemptDamage( DamageInfo *damageInfo );		///< try to damage this object

protected:

};
