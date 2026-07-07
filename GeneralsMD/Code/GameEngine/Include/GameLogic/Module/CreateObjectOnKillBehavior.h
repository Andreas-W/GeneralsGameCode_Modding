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

// FILE: CreateObjectOnKillBehavior.h /////////////////////////////////////////////////////////////
// Desc:   Spawns an ObjectCreationList at the position of an object this object kills.
///////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/Module/OnKillModule.h"

class ObjectCreationList;

//-------------------------------------------------------------------------------------------------
class CreateObjectOnKillBehaviorModuleData : public BehaviorModuleData
{
public:
	UpgradeMuxData						m_upgradeMuxData;
	Bool											m_initiallyActive;
	KillMuxData								m_killMuxData;
	const ObjectCreationList* m_ocl;						///< spawn this OCL at the victim when we kill something
	Bool											m_createAtKillerLocation;	///< spawn at the killer's position instead of the victim's
	Bool											m_createObjectForVictim;	///< created object is owned by the victim instead of the killer

	CreateObjectOnKillBehaviorModuleData()
	{
		m_initiallyActive = false;
		m_ocl = nullptr;
		m_createAtKillerLocation = false;
		m_createObjectForVictim = false;
	}

	static void buildFieldParse(MultiIniFieldParse& p)
	{
		static const FieldParse dataFieldParse[] =
		{
			{ "StartsActive",	INI::parseBool,									nullptr, offsetof( CreateObjectOnKillBehaviorModuleData, m_initiallyActive ) },
			{ "CreationList",		INI::parseObjectCreationList,		nullptr, offsetof( CreateObjectOnKillBehaviorModuleData, m_ocl ) },
			{ "CreateAtKillerLocation",	INI::parseBool,				nullptr, offsetof( CreateObjectOnKillBehaviorModuleData, m_createAtKillerLocation ) },
			{ "CreateObjectForVictim",	INI::parseBool,				nullptr, offsetof( CreateObjectOnKillBehaviorModuleData, m_createObjectForVictim ) },
			{ 0, 0, 0, 0 }
		};

		BehaviorModuleData::buildFieldParse(p);
		p.add(dataFieldParse);
		p.add(UpgradeMuxData::getFieldParse(), offsetof( CreateObjectOnKillBehaviorModuleData, m_upgradeMuxData ));
		p.add(KillMuxData::getFieldParse(), offsetof( CreateObjectOnKillBehaviorModuleData, m_killMuxData ));
	}
};

//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
class CreateObjectOnKillBehavior : public BehaviorModule,
																	 public UpgradeMux,
																	 public OnKillModuleInterface
{

	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( CreateObjectOnKillBehavior, "CreateObjectOnKillBehavior" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( CreateObjectOnKillBehavior, CreateObjectOnKillBehaviorModuleData )

public:

	CreateObjectOnKillBehavior( Thing *thing, const ModuleData* moduleData );
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
		getCreateObjectOnKillBehaviorModuleData()->m_upgradeMuxData.getUpgradeActivationMasks(activation, conflicting);
	}

	virtual void performUpgradeFX()
	{
		getCreateObjectOnKillBehaviorModuleData()->m_upgradeMuxData.performUpgradeFX(getObject());
	}

	virtual void processUpgradeRemoval()
	{
		getCreateObjectOnKillBehaviorModuleData()->m_upgradeMuxData.muxDataProcessUpgradeRemoval(getObject());
	}

	virtual Bool requiresAllActivationUpgrades() const
	{
		return getCreateObjectOnKillBehaviorModuleData()->m_upgradeMuxData.m_requiresAllTriggers;
	}

	Bool isUpgradeActive() const { return isAlreadyUpgraded(); }

	virtual Bool isSubObjectsUpgrade() { return false; }

private:

	UnsignedInt m_lastTriggerFrame;		///< frame of the last trigger, for TriggerChance/CooldownTime

};
