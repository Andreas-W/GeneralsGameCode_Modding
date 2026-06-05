// FILE: CrateObjectUpgrade.cpp /////////////////////////////////////////////////////////////////////////////
// Desc:	 apply salvage crate effects on upgrade
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ModelState.h"
#include "Common/Player.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/Module/CrateApplyUpgrade.h"
#include "GameLogic/Object.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ArmorSet.h"
#include "GameLogic/ExperienceTracker.h"

#include "Common/AudioEventRTS.h"
#include "Common/MiscAudio.h"

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CrateApplyUpgradeModuleData::CrateApplyUpgradeModuleData( void )
{
	m_applySalvageUpgrade = false;
	m_applyLevelUp = false;
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
/* static */ void CrateApplyUpgradeModuleData::buildFieldParse(MultiIniFieldParse& p)
{
	UpgradeModuleData::buildFieldParse( p );

	static const FieldParse dataFieldParse[] =
	{
		//{ "UpgradeObject", INI::parseObjectCreationList, nullptr, offsetof( ObjectCreationUpgradeModuleData, m_ocl ) },
		{ "SalvageCrate", INI::parseBool, nullptr, offsetof(CrateApplyUpgradeModuleData, m_applySalvageUpgrade)},
		{ "LevelUp", INI::parseBool, nullptr, offsetof(CrateApplyUpgradeModuleData, m_applyLevelUp)},
		{ nullptr, nullptr, nullptr, 0 }
	};
	p.add(dataFieldParse);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CrateApplyUpgrade::CrateApplyUpgrade( Thing *thing, const ModuleData* moduleData ) :
							UpgradeModule( thing, moduleData )
{

}

Bool CrateApplyUpgrade::wouldUpgrade(const UpgradeMaskType& keyMask) const
{
	return UpgradeMux::wouldUpgrade(keyMask) && canApplyAnyCrateUpgade();
}

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
CrateApplyUpgrade::~CrateApplyUpgrade( void )
{

}

// ------------------------------------------------------------------------------------------------
static void applyWeaponSet(Object* unit)
{
	if (unit->testWeaponSetFlag(WEAPONSET_CRATEUPGRADE_ONE))
	{
		unit->clearWeaponSetFlag(WEAPONSET_CRATEUPGRADE_ONE);
		unit->setWeaponSetFlag(WEAPONSET_CRATEUPGRADE_TWO);
	}
	else
	{
		unit->setWeaponSetFlag(WEAPONSET_CRATEUPGRADE_ONE);
	}
}

// ------------------------------------------------------------------------------------------------
static void applyArmorSet(Object* unit)
{
	if (unit->testArmorSetFlag(ARMORSET_CRATE_UPGRADE_ONE))
	{
		unit->clearArmorSetFlag(ARMORSET_CRATE_UPGRADE_ONE);
		unit->setArmorSetFlag(ARMORSET_CRATE_UPGRADE_TWO);

		unit->clearAndSetModelConditionState(MODELCONDITION_ARMORSET_CRATEUPGRADE_ONE, MODELCONDITION_ARMORSET_CRATEUPGRADE_TWO);
	}
	else
	{
		unit->setArmorSetFlag(ARMORSET_CRATE_UPGRADE_ONE);

		unit->setModelConditionState(MODELCONDITION_ARMORSET_CRATEUPGRADE_ONE);
	}
}

// ------------------------------------------------------------------------------------------------
static void doLevelGain(Object* unit)
{
	unit->getExperienceTracker()->gainExpForLevel(1);
}

static void doSalvageEffect(Object* unit) {
	//Play the salvage installation crate pickup sound.
	AudioEventRTS soundToPlay = TheAudio->getMiscAudio()->m_crateSalvage;
	soundToPlay.setObjectID(unit->getID());
	TheAudio->addAudioEvent(&soundToPlay);
}
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void CrateApplyUpgrade::upgradeImplementation(void)
{
	Object* self = getObject();

	// check if armor salvager
	if (canApplyArmorUpgrade()) {
		applyArmorSet(self);
		doSalvageEffect(self);
	}
	// Check weapon salvager
	else if (canApplyWeaponUpgrade()) {
		applyWeaponSet(self);
		doSalvageEffect(self);
	}
	// Check Levelup
	else if (canApplyLevelUp()) {
		// set upgrade as already executed here, since leveling up a unit reevaluated upgrades and triggers a recursion
		setUpgradeExecuted(true);
		doLevelGain(self);
	}

}

Bool CrateApplyUpgrade::canApplyAnyCrateUpgade() const
{
	return canApplyWeaponUpgrade() || canApplyArmorUpgrade() || canApplyLevelUp();
}

Bool CrateApplyUpgrade::canApplyWeaponUpgrade() const
{
	const Object* self = getObject();
	const CrateApplyUpgradeModuleData* data = getCrateApplyUpgradeModuleData();
	return data->m_applySalvageUpgrade && self->isKindOf(KINDOF_WEAPON_SALVAGER) && !self->testWeaponSetFlag(WEAPONSET_CRATEUPGRADE_TWO);
}

Bool CrateApplyUpgrade::canApplyArmorUpgrade() const
{
	const Object* self = getObject();
	const CrateApplyUpgradeModuleData* data = getCrateApplyUpgradeModuleData();
	return data->m_applySalvageUpgrade && self->isKindOf(KINDOF_ARMOR_SALVAGER) && !self->testArmorSetFlag(ARMORSET_CRATE_UPGRADE_TWO);
}

Bool CrateApplyUpgrade::canApplyLevelUp() const
{
	const Object* self = getObject();
	const CrateApplyUpgradeModuleData* data = getCrateApplyUpgradeModuleData();
	return data->m_applyLevelUp && self->getExperienceTracker()->isTrainable() && self->getExperienceTracker()->getVeterancyLevel() < LEVEL_HEROIC;
}

// ------------------------------------------------------------------------------------------------
/** CRC */
// ------------------------------------------------------------------------------------------------
void CrateApplyUpgrade::crc( Xfer *xfer )
{

	// extend base class
	UpgradeModule::crc( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Xfer method
	* Version Info:
	* 1: Initial version */
// ------------------------------------------------------------------------------------------------
void CrateApplyUpgrade::xfer( Xfer *xfer )
{

	// version
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );

	// extend base class
	UpgradeModule::xfer( xfer );

}

// ------------------------------------------------------------------------------------------------
/** Load post process */
// ------------------------------------------------------------------------------------------------
void CrateApplyUpgrade::loadPostProcess( void )
{

	// extend base class
	UpgradeModule::loadPostProcess();

}
