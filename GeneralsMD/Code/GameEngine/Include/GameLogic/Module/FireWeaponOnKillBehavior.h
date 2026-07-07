/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: FireWeaponOnKillBehavior.h ///////////////////////////////////////////////////////////////
// Desc:   Fires a weapon at the position of an object this object kills.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Module/OnKillModule.h"

class WeaponTemplate;

//-------------------------------------------------------------------------------------------------
class FireWeaponOnKillBehaviorModuleData : public BehaviorModuleData
{
public:
	UpgradeMuxData				m_upgradeMuxData;
	Bool									m_initiallyActive;
	KillMuxData						m_killMuxData;
	const WeaponTemplate* m_killWeapon;						///< fire this weapon at the victim when we kill something

	FireWeaponOnKillBehaviorModuleData()
	{
		m_initiallyActive = false;
		m_killWeapon = nullptr;
	}

	static void buildFieldParse(MultiIniFieldParse& p)
	{
		static const FieldParse dataFieldParse[] =
		{
			{ "StartsActive",	INI::parseBool,							nullptr, offsetof( FireWeaponOnKillBehaviorModuleData, m_initiallyActive ) },
			{ "KillWeapon",		INI::parseWeaponTemplate,		nullptr, offsetof( FireWeaponOnKillBehaviorModuleData, m_killWeapon ) },
			{ 0, 0, 0, 0 }
		};

		BehaviorModuleData::buildFieldParse(p);
		p.add(dataFieldParse);
		p.add(UpgradeMuxData::getFieldParse(), offsetof( FireWeaponOnKillBehaviorModuleData, m_upgradeMuxData ));
		p.add(KillMuxData::getFieldParse(), offsetof( FireWeaponOnKillBehaviorModuleData, m_killMuxData ));
	}
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class FireWeaponOnKillBehavior : public BehaviorModule,
																 public UpgradeMux,
																 public OnKillModuleInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( FireWeaponOnKillBehavior, "FireWeaponOnKillBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( FireWeaponOnKillBehavior, FireWeaponOnKillBehaviorModuleData )

public:

	FireWeaponOnKillBehavior( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	// module methods
	static Int getInterfaceMask() { return BehaviorModule::getInterfaceMask() | (MODULEINTERFACE_UPGRADE); }

	// BehaviorModule
	virtual UpgradeModuleInterface* getUpgrade() { return this; }
	virtual OnKillModuleInterface* getOnKill() { return this; }

	// OnKillModuleInterface
	virtual void onKilledObject( Object *victim, const DamageInfo *damageInfo );

protected:

	virtual void upgradeImplementation()
	{
		// nothing!
	}

	virtual void getUpgradeActivationMasks(UpgradeMaskType& activation, UpgradeMaskType& conflicting) const
	{
		getFireWeaponOnKillBehaviorModuleData()->m_upgradeMuxData.getUpgradeActivationMasks(activation, conflicting);
	}

	virtual void performUpgradeFX()
	{
		getFireWeaponOnKillBehaviorModuleData()->m_upgradeMuxData.performUpgradeFX(getObject());
	}

	virtual void processUpgradeRemoval()
	{
		getFireWeaponOnKillBehaviorModuleData()->m_upgradeMuxData.muxDataProcessUpgradeRemoval(getObject());
	}

	virtual Bool requiresAllActivationUpgrades() const
	{
		return getFireWeaponOnKillBehaviorModuleData()->m_upgradeMuxData.m_requiresAllTriggers;
	}

	Bool isUpgradeActive() const { return isAlreadyUpgraded(); }

	virtual Bool isSubObjectsUpgrade() { return false; }

private:

	UnsignedInt m_lastTriggerFrame;		///< frame of the last trigger, for TriggerChance/CooldownTime

};
