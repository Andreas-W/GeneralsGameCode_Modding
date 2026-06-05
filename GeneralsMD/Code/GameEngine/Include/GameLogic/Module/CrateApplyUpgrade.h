// FILE: CrateApplyUpgrade.h ///////////////////////////////////////////////////////////////////////////
// Desc:	 apply salvage crate effetcs on upgrade
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "GameLogic/Module/UpgradeModule.h"

// FORWARD REFERENCES /////////////////////////////////////////////////////////////////////////////
class Thing;
class Player;

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class CrateApplyUpgradeModuleData : public UpgradeModuleData
{

public:
	Bool m_applySalvageUpgrade;
	Bool m_applyLevelUp;

	CrateApplyUpgradeModuleData( void );

	static void buildFieldParse(MultiIniFieldParse& p);

};

//-------------------------------------------------------------------------------------------------
/** The upgrade module */
//-------------------------------------------------------------------------------------------------
class CrateApplyUpgrade : public UpgradeModule
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE(CrateApplyUpgrade, "CrateApplyUpgrade" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA(CrateApplyUpgrade, CrateApplyUpgradeModuleData);

public:

	CrateApplyUpgrade( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype defined by MemoryPoolObject

	virtual Bool wouldUpgrade(const UpgradeMaskType& keyMask) const override;

protected:

	virtual void upgradeImplementation( void ); ///< Here's the actual work of Upgrading
	virtual Bool isSubObjectsUpgrade() { return false; }

	Bool canApplyAnyCrateUpgade() const;

	Bool canApplyWeaponUpgrade() const;
	Bool canApplyArmorUpgrade() const;
	Bool canApplyLevelUp() const;

};
